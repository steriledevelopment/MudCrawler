#include "session.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <ctime>
#include <sstream>

namespace mc {

static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static std::string firstWord(const std::string& s, std::string* rest) {
    std::string t = trim(s); size_t sp = t.find_first_of(" \t");
    if (rest) *rest = sp == std::string::npos ? "" : trim(t.substr(sp + 1));
    return sp == std::string::npos ? t : t.substr(0, sp);
}

// ---- network thread -------------------------------------------------------
struct NetThreadArg { HWND wnd; int sid; unsigned gen; std::string host; int port; SOCKET* sockOut; };

static DWORD WINAPI netThread(LPVOID p) {
    std::unique_ptr<NetThreadArg> a((NetThreadArg*)p);
    auto post = [&](UINT m, NetMsg* msg) { if (!PostMessage(a->wnd, m, (WPARAM)a->sid, (LPARAM)msg)) delete msg; };
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    int rc = getaddrinfo(a->host.c_str(), std::to_string(a->port).c_str(), &hints, &res);
    if (rc != 0 || !res) { post(WM_NET_CLOSED, new NetMsg{a->gen, "Cannot resolve host '" + a->host + "'"}); return 0; }
    SOCKET s = INVALID_SOCKET; std::string err = "Connection failed";
    for (addrinfo* r = res; r; r = r->ai_next) {
        s = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, r->ai_addr, (int)r->ai_addrlen) == 0) break;
        err = "Connection failed (error " + std::to_string(WSAGetLastError()) + ")";
        closesocket(s); s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (s == INVALID_SOCKET) { post(WM_NET_CLOSED, new NetMsg{a->gen, err}); return 0; }
    *a->sockOut = s;   // publishes the socket (UI thread reads it on WM_NET_CONNECTED)
    BOOL nd = TRUE; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof nd);
    post(WM_NET_CONNECTED, new NetMsg{a->gen, ""});
    char buf[16384];
    for (;;) {
        int n = recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        post(WM_NET_DATA, new NetMsg{a->gen, std::string(buf, n)});
    }
    post(WM_NET_CLOSED, new NetMsg{a->gen, ""});
    return 0;
}

// ---- lifecycle -------------------------------------------------------------
Session::Session(int id_, HWND notify, const Profile& p, const Settings* s, UiHooks* hooks)
    : id(id_), profile(p), settings(s), notify_(notify), hooks_(hooks) {
    rules.deserialize(readFile(dataDir() + "\\rules_" + safeName(p.id) + ".rules"));
    mapper.deserialize(readFile(dataDir() + "\\map_" + safeName(p.id) + ".map"));
    mapper.current.clear();   // we don't know where we are after a restart
    try { blocked_ = std::regex(s->blockedPattern, std::regex::ECMAScript | std::regex::icase); blockedOk_ = !s->blockedPattern.empty(); } catch (...) {}
    memset(&zs_, 0, sizeof zs_);
}

Session::~Session() { userClosed_ = true; disconnect(); saveAll(); }

void Session::saveAll() {
    writeFile(dataDir() + "\\rules_" + safeName(profile.id) + ".rules", rules.serialize());
    writeFile(dataDir() + "\\map_" + safeName(profile.id) + ".map", mapper.serialize());
    mapDirty_ = false;
}

void Session::connect() {
    if (state != Disconnected) return;
    userClosed_ = false; reconnectAt_ = -1;
    gen_++;
    state = Connecting; statusText = "Connecting to " + profile.host + ":" + std::to_string(profile.port) + "...";
    say(statusText, 8);
    if (profile.tls) say("Note: TLS is not implemented yet; connecting without encryption.", 11);
    parser_ = TelnetParser(); remote_.clear(); local_.clear(); ttypeIdx_ = 0;
    stopMccp(); carry_.clear(); lineBuf_.clear(); partialShown_ = false;
    loginStage_ = profile.autoLogin ? 0 : 3; gmcpRoom = false; gmcp.clear(); vitals = Vitals(); mccp = false;
    ansi_.reset(); chatAnsi_.reset();
    sock_ = INVALID_SOCKET;
    auto* a = new NetThreadArg{notify_, id, gen_, profile.host, profile.port, &sock_};
    HANDLE h = CreateThread(nullptr, 0, netThread, a, 0, nullptr);
    if (h) CloseHandle(h); else { delete a; state = Disconnected; say("Could not start network thread.", 9); }
    touchStatus();
}

void Session::disconnect() {
    userClosed_ = true;
    gen_++;                       // invalidate in-flight messages from the old thread
    SOCKET s = sock_; sock_ = INVALID_SOCKET;
    if (s != INVALID_SOCKET) { shutdown(s, SD_BOTH); closesocket(s); }
    bool was = state != Disconnected;
    state = Disconnected; stopMccp(); delayed_.clear();
    finalizeCapture(true);
    if (was) { flushPartial(true); statusText = "Disconnected"; say("Disconnected.", 8); touchStatus(); }
    if (passwordMode) { passwordMode = false; if (hooks_ && hooks_->passwordMode) hooks_->passwordMode(this, false); }
}

void Session::onNetMessage(UINT msg, LPARAM l) {
    std::unique_ptr<NetMsg> m((NetMsg*)l);
    if (m->gen != gen_) return;
    if (msg == WM_NET_CONNECTED) {
        state = Connected; statusText = "Connected";
        say("Connected.", 10);
        touchStatus();
    } else if (msg == WM_NET_DATA) {
        bytesIn += m->data.size();
        feedBytes((const uint8_t*)m->data.data(), m->data.size());
    } else if (msg == WM_NET_CLOSED) {
        SOCKET s = sock_; sock_ = INVALID_SOCKET;
        if (s != INVALID_SOCKET) closesocket(s);
        bool was = state == Connected;
        state = Disconnected; stopMccp(); flushPartial(true); delayed_.clear();
        statusText = m->data.empty() ? "Connection closed by server" : m->data;
        say(statusText + ".", 9);
        if (passwordMode) { passwordMode = false; if (hooks_ && hooks_->passwordMode) hooks_->passwordMode(this, false); }
        if (profile.autoReconnect && !userClosed_) { reconnectAt_ = now_ + (was ? 3 : 10); say("Reconnecting automatically...", 8); }
        saveAll();
        touchStatus();
    }
}

// ---- receive path ---------------------------------------------------------
void Session::feedBytes(const uint8_t* p, size_t n) {
    if (!zActive_) { feedTelnet(p, n); return; }
    zs_.next_in = (Bytef*)p; zs_.avail_in = (uInt)n;
    uint8_t out[16384];
    while (zs_.avail_in > 0) {
        zs_.next_out = out; zs_.avail_out = sizeof out;
        int rc = inflate(&zs_, Z_SYNC_FLUSH);
        size_t got = sizeof out - zs_.avail_out;
        if (got) feedTelnet(out, got);
        if (rc == Z_STREAM_END) {
            const uint8_t* rest = zs_.next_in; size_t left = zs_.avail_in;
            inflateEnd(&zs_); zActive_ = false; mccp = false; touchStatus();
            if (left) feedBytes(rest, left);
            return;
        }
        if (rc != Z_OK && rc != Z_BUF_ERROR) { say("MCCP decompression error; disconnecting.", 9); disconnect(); return; }
        if (rc == Z_BUF_ERROR && got == 0) break;
    }
}

void Session::startMccp(const uint8_t* p, size_t n) {
    stopMccp();
    memset(&zs_, 0, sizeof zs_);
    if (inflateInit(&zs_) != Z_OK) { say("Could not start MCCP.", 9); return; }
    zActive_ = true; mccp = true; touchStatus();
    feedBytes(p, n);
}
void Session::stopMccp() { if (zActive_) { inflateEnd(&zs_); zActive_ = false; } mccp = false; }

void Session::feedTelnet(const uint8_t* p, size_t n) {
    std::vector<TelnetEvent> ev;
    size_t used = parser_.feed(p, n, ev);
    // MCCP2 starts right after "IAC SB MCCP2 IAC SE", even if no compressed bytes are in this packet yet
    bool mccpStart = !ev.empty() && ev.back().type == TelnetEvent::Sub && ev.back().opt == OPT_MCCP2;
    for (auto& e : ev) handleEvent(e);
    if (mccpStart && state != Disconnected) startMccp(p + used, n - used);
}

void Session::handleEvent(const TelnetEvent& e) {
    switch (e.type) {
    case TelnetEvent::Data: handleText(e.data); break;
    case TelnetEvent::Prompt: flushPartial(true); break;
    case TelnetEvent::Neg: handleNeg(e.cmd, e.opt); break;
    case TelnetEvent::Sub: handleSub(e.opt, e.data); break;
    }
}

void Session::rawSend(const std::string& s) {
    SOCKET so = sock_; if (so == INVALID_SOCKET || state == Disconnected) return;
    size_t off = 0;
    while (off < s.size()) { int n = send(so, s.data() + off, (int)(s.size() - off), 0); if (n <= 0) break; off += n; }
}

void Session::handleNeg(uint8_t cmd, uint8_t opt) {
    if (cmd == WILL) {
        if (remote_[opt]) return;
        if (opt == OPT_ECHO) {
            remote_[opt] = true; rawSend(buildNegotiation(DO, opt));
            passwordMode = true; if (hooks_ && hooks_->passwordMode) hooks_->passwordMode(this, true);
        } else if (opt == OPT_GMCP || opt == OPT_MCCP2 || opt == OPT_EOR || opt == OPT_SGA || opt == OPT_MSDP) {
            remote_[opt] = true; rawSend(buildNegotiation(DO, opt));
            if (opt == OPT_GMCP) {
                rawSend(buildGmcp("Core.Hello", "{\"client\":\"MudCrawler\",\"version\":\"1.0\"}"));
                rawSend(buildGmcp("Core.Supports.Set", "[\"Char 1\",\"Char.Vitals 1\",\"Char.Status 1\",\"Room 1\",\"Room.Info 1\",\"Comm 1\",\"Comm.Channel 1\"]"));
            } else if (opt == OPT_MSDP) {
                for (const char* k : {"HEALTH", "HEALTH_MAX", "MANA", "MANA_MAX", "MOVEMENT", "MOVEMENT_MAX"}) {
                    std::string pl; pl.push_back(1); pl += "REPORT"; pl.push_back(2); pl += k;
                    rawSend(buildSub(OPT_MSDP, pl));
                }
            }
        } else rawSend(buildNegotiation(DONT, opt));
    } else if (cmd == WONT) {
        if (remote_[opt]) {
            remote_[opt] = false; rawSend(buildNegotiation(DONT, opt));
            if (opt == OPT_ECHO) { passwordMode = false; if (hooks_ && hooks_->passwordMode) hooks_->passwordMode(this, false); }
        }
    } else if (cmd == DO) {
        if (local_[opt]) return;
        if (opt == OPT_TTYPE || opt == OPT_NAWS) {
            local_[opt] = true; rawSend(buildNegotiation(WILL, opt));
            if (opt == OPT_NAWS) rawSend(buildNaws(cols_, rows_));
        } else rawSend(buildNegotiation(WONT, opt));
    } else if (cmd == DONT) {
        if (local_[opt]) { local_[opt] = false; rawSend(buildNegotiation(WONT, opt)); }
    }
}

void Session::setSize(int c, int r) {
    cols_ = std::max(20, std::min(c, 500)); rows_ = std::max(5, std::min(r, 200));
    if (local_[OPT_NAWS]) rawSend(buildNaws(cols_, rows_));
}

void Session::handleSub(uint8_t opt, const std::string& data) {
    if (opt == OPT_TTYPE && !data.empty() && data[0] == 1) {
        static const char* names[] = {"MudCrawler", "XTERM-256COLOR", "MTTS 269"};
        const char* nm = names[std::min(ttypeIdx_++, 2)];
        rawSend(buildSub(OPT_TTYPE, std::string(1, '\0') + nm));
    } else if (opt == OPT_GMCP) {
        std::string pkg, js; splitGmcp(data, pkg, js);
        onGmcp(pkg, js);
    } else if (opt == OPT_MSDP) {
        // flat string variables only: VAR name VAL value
        size_t i = 0;
        while (i < data.size()) {
            if (data[i] != 1) { i++; continue; }
            size_t k = ++i; while (i < data.size() && (unsigned char)data[i] > 6) i++;
            std::string name = data.substr(k, i - k);
            if (i < data.size() && data[i] == 2) {
                size_t v = ++i; while (i < data.size() && (unsigned char)data[i] > 6) i++;
                onMsdp(name, data.substr(v, i - v));
            }
        }
    }
}

void Session::handleText(const std::string& in) {
    carry_ += in;
    // keep an incomplete trailing UTF-8 sequence for the next chunk
    size_t n = carry_.size(), cut = n;
    for (size_t back = 1; back <= 3 && back <= n; back++) {
        unsigned char c = (unsigned char)carry_[n - back];
        if ((c & 0xC0) == 0x80) continue;
        size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (need > back) cut = n - back;
        break;
    }
    std::string text = carry_.substr(0, cut); carry_.erase(0, cut);
    lineBuf_ += text;
    size_t pos;
    processing_ = true;
    while ((pos = lineBuf_.find('\n')) != std::string::npos) {
        std::string line = lineBuf_.substr(0, pos); lineBuf_.erase(0, pos + 1);
        partialShown_ = false; shownPartial_.clear();   // the UI replaces the transient partial when a line is committed
        processLine(line);
    }
    processing_ = false;
    lastData_ = now_;
}

void Session::emitLine(const std::vector<Run>& runs, const std::string& pane, bool partial) {
    if (hooks_ && hooks_->line) hooks_->line(this, runs, pane, partial);
}

void Session::processLine(const std::string& raw, bool isPrompt) {
    std::string plain;
    std::vector<Run> runs = ansi_.parseLine(raw, &plain);
    LineResult r = rules.matchLine(plain);
    if (!rules.lastError.empty()) { say(rules.lastError, 9); rules.lastError.clear(); }
    if (blockedOk_ && now_ < moveWatchUntil_ && std::regex_search(plain, blocked_)) {
        moveWatchUntil_ = 0; capKind_ = 0; capLines_.clear(); capPending_ = false;
        if (!gmcpRoom && mapper.undoLast()) { say("(map) move blocked, undone", 8); mapRev++; touchStatus(); }
        delayed_.erase(std::remove_if(delayed_.begin(), delayed_.end(), [](const Delayed& d) { return d.walk; }), delayed_.end());
    }
    if (loginStage_ < 3) autoLogin(plain);
    if (!r.gag) {
        if (r.fg != -1) for (auto& run : runs) run.style.fg = r.fg;
        emitLine(runs, r.pane, false);
        logLine(plain);
    }
    if (capKind_ && !isPrompt && r.pane == "main") {   // collect the text the MUD shows for this room
        capLast_ = now_;
        if (capLines_.size() < 60 && !(plain.empty() && capLines_.empty())) capLines_.push_back(plain);
    }
    execCommands(r.commands);
}

void Session::flushPartial(bool commit) {
    if (lineBuf_.empty()) { return; }
    if (commit) {
        std::string line = lineBuf_; lineBuf_.clear(); partialShown_ = false; shownPartial_.clear();
        processLine(line, true);
        finalizeCapture(false);            // a prompt marks the end of the room text
    } else {
        std::string plain;
        finalizeCapture(false);
        auto runs = ansi_.peekLine(lineBuf_, &plain);
        emitLine(runs, "main", true);
        partialShown_ = true; shownPartial_ = lineBuf_;
        if (loginStage_ < 3) autoLogin(plain);
    }
}

void Session::logLine(const std::string& plain) {
    if (!settings->logging) { if (log_.is_open()) log_.close(); return; }
    if (!log_.is_open()) {
        time_t t = time(nullptr); char d[32]; strftime(d, sizeof d, "%Y-%m-%d", localtime(&t));
        CreateDirectoryA((dataDir() + "\\logs").c_str(), nullptr);
        log_.open(dataDir() + "\\logs\\" + safeName(profile.name) + "-" + d + ".log", std::ios::app | std::ios::binary);
    }
    if (log_) { time_t t = time(nullptr); char d[16]; strftime(d, sizeof d, "%H:%M:%S ", localtime(&t)); log_ << d << plain << "\n"; log_.flush(); }
}

// ---- GMCP / MSDP -------------------------------------------------------------
static double pick(const JVal& v, std::initializer_list<const char*> keys, double def = -1) {
    for (auto k : keys) if (v.has(k)) return v[k].num(def);
    return def;
}

void Session::onGmcp(const std::string& pkg, const std::string& json) {
    std::string lp = lower(pkg);
    JVal v; if (!json.empty()) JsonParser::parse(json, v);
    gmcp[lp] = v;
    if (lp == "room.info") {
        std::string rid = v.has("num") ? v["num"].str() : v["id"].str();
        if (rid.empty()) return;
        std::map<std::string, std::string> exits;
        if (v["exits"].t == JVal::Obj) for (auto& e : v["exits"].o) exits[e.first] = e.second.str();
        else if (v["exits"].t == JVal::Arr) for (auto& e : v["exits"].a) exits[e.str()] = "";
        std::string area = v.has("area") ? v["area"].str() : v.has("zone") ? v["zone"].str() : v["details"].str();
        std::string terrain = v.has("terrain") ? v["terrain"].str() : v["environment"].str();
        gmcpRoom = true;
        mapper.arrive(rid, v["name"].str(), area.empty() ? "Unknown" : area, exits, terrain);
        mapDirty_ = true; mapSaveAt_ = now_ + 15; mapRev++;
        if (capKind_ == 1 && capPending_) finalizeCapture(false);
        touchStatus();
    } else if (lp == "char.vitals" || lp == "char.maxstats") {
        double x;
        if ((x = pick(v, {"hp", "health"})) >= 0) vitals.hp = x;
        if ((x = pick(v, {"maxhp", "max_hp", "maxhealth"})) >= 0) vitals.maxhp = x;
        if ((x = pick(v, {"mp", "mana", "sp", "ep"})) >= 0) vitals.mp = x;
        if ((x = pick(v, {"maxmp", "maxmana", "max_mana", "maxsp", "maxep"})) >= 0) vitals.maxmp = x;
        if ((x = pick(v, {"mv", "moves", "move", "movement", "wp", "end"})) >= 0) vitals.mv = x;
        if ((x = pick(v, {"maxmv", "maxmoves", "max_moves", "maxwp", "maxend"})) >= 0) vitals.maxmv = x;
        touchStatus();
    } else if (lp == "comm.channel.text" || lp == "comm.channel") {
        std::string chan = v.has("channel") ? v["channel"].str() : v["chan"].str();
        std::string who = v.has("talker") ? v["talker"].str() : v["player"].str();
        std::string msg = v.has("text") ? v["text"].str() : v["msg"].str();
        if (msg.empty()) return;
        std::string plain;
        auto runs = chatAnsi_.parseLine("[" + chan + "] " + (who.empty() ? "" : who + ": ") + msg, &plain);
        chatAnsi_.reset();
        emitLine(runs, "chat", false);
    }
}

void Session::onMsdp(const std::string& k, const std::string& val) {
    double x = atof(val.c_str());
    if (k == "HEALTH") vitals.hp = x; else if (k == "HEALTH_MAX") vitals.maxhp = x;
    else if (k == "MANA") vitals.mp = x; else if (k == "MANA_MAX") vitals.maxmp = x;
    else if (k == "MOVEMENT") vitals.mv = x; else if (k == "MOVEMENT_MAX") vitals.maxmv = x;
    else return;
    touchStatus();
}

// ---- auto login ------------------------------------------------------------------
static bool containsNoCase(const std::string& hay, const std::string& needle) {
    return !needle.empty() && lower(hay).find(lower(needle)) != std::string::npos;
}
void Session::autoLogin(const std::string& plain) {
    if (state != Connected) return;
    if (loginStage_ == 0 && containsNoCase(plain, profile.userPrompt)) {
        loginStage_ = 1; sendCommand(profile.user);
    } else if (loginStage_ <= 1 && containsNoCase(plain, profile.passPrompt)) {
        loginStage_ = 2;
        commitPartialForSend();
        rawSend(profile.password() + "\r\n");
        say("(auto-login: password sent)", 8);
        std::istringstream in(profile.postLogin); std::string l; double t = now_ + 1.5;
        while (std::getline(in, l)) { l = trim(l); if (l.empty()) continue; delayed_.push_back({t, l, false}); t += 0.4; }
        loginStage_ = 3;
    }
}

// ---- room description capture ---------------------------------------------
void Session::startCapture(int kind) {
    finalizeCapture(true);
    capKind_ = kind; capRoom_ = mapper.current; capLines_.clear(); capLast_ = capStart_ = now_; capPending_ = false;
}

void Session::finalizeCapture(bool force) {
    if (!capKind_) return;
    int kind = capKind_;
    auto reset = [&] { capKind_ = 0; capLines_.clear(); capPending_ = false; };
    if (capLines_.empty()) { if (force) reset(); return; }
    bool ok = kind == 1 ? (mapper.current != capRoom_) : (mapper.current == capRoom_ && !mapper.current.empty());
    if (!ok) {
        // a GMCP room change may arrive just after the text; wait briefly before giving up
        if (kind == 1 && !force && now_ - capStart_ < 1.5) { capPending_ = true; return; }
        reset(); return;
    }
    Room* r = mapper.get(mapper.current);
    if (r) {
        while (!capLines_.empty() && trim(capLines_.back()).empty()) capLines_.pop_back();
        std::string text; int blanks = 0;
        for (auto& l : capLines_) {
            if (trim(l).empty()) { if (++blanks > 1) continue; } else blanks = 0;
            text += l + "\n";
            if (text.size() > 4000) break;
        }
        while (!text.empty() && text.back() == '\n') text.pop_back();
        if (!text.empty() && r->desc != text) { r->desc = text; touchMap(); }
    }
    reset();
}

// ---- tick ----------------------------------------------------------------------------
void Session::tick(double dt) {
    now_ += dt;
    if (state == Connected) {
        execCommands(rules.tick(dt));
        // transient prompt text: show it once the stream has gone quiet
        if (!lineBuf_.empty() && now_ - lastData_ > 0.15 && lineBuf_ != shownPartial_) flushPartial(false);
    }
    for (size_t i = 0; i < delayed_.size();) {
        if (delayed_[i].due <= now_) { Delayed d = delayed_[i]; delayed_.erase(delayed_.begin() + i); sendCommand(d.cmd); }
        else i++;
    }
    if (capKind_) {
        if (capPending_ || (now_ - capLast_ > 0.8 && !capLines_.empty())) finalizeCapture(false);
        if (capKind_ && now_ - capStart_ > 4.0) finalizeCapture(true);
    }
    if (reconnectAt_ >= 0 && now_ >= reconnectAt_ && state == Disconnected) { reconnectAt_ = -1; connect(); }
    if (mapDirty_ && now_ >= mapSaveAt_) saveAll();
}

// ---- input ------------------------------------------------------------------------------
void Session::say(const std::string& text, int fg) {
    Style st; st.fg = fg;
    emitLine({Run{"[MC] " + text, st}}, "main", false);
}

void Session::execCommands(const std::vector<std::string>& cmds) {
    for (auto& c : cmds) for (auto& a : rules.processInput(c)) execAction(a);
}
void Session::commitPartialForSend() {
    // A prompt ("Name: ") has no newline. When the player answers, it becomes a normal line and the next
    // server output starts on a fresh line. Skipped while the line-splitting loop is still running.
    if (!processing_ && !lineBuf_.empty()) flushPartial(true);
}

void Session::userInput(const std::string& text) {
    if (passwordMode) { commitPartialForSend(); rawSend(text + "\r\n"); return; }     // never alias/echo/log passwords
    auto actions = rules.processInput(text);
    for (auto& a : actions) execAction(a);
}
void Session::execAction(const Action& a) {
    if (a.client) clientCommand(a.text); else sendCommand(a.text);
}

void Session::sendCommand(const std::string& text) {
    if (state != Connected) { if (state == Disconnected) say("Not connected. Use File > Connect.", 9); return; }
    commitPartialForSend();
    std::string dir = normDir(text);
    if (!dir.empty() && mapper.autoMap) {
        if (gmcpRoom) mapper.noteMove(dir);
        else { if (!mapper.moveManual(dir).empty()) { moveWatchUntil_ = now_ + 2.0; mapDirty_ = true; mapSaveAt_ = now_ + 15; mapRev++; touchStatus(); } }
    }
    {   // remember what the MUD says next, so the room's description can be stored on the map
        std::string low = lower(trim(text));
        if (!dir.empty() && mapper.autoMap) startCapture(gmcpRoom ? 1 : 2);
        else if (low == "look" || low == "l") startCapture(2);
    }
    if (settings->echoInput) { Style st; st.fg = 14; emitLine({Run{text, st}}, "main", false); }
    logLine("> " + text);
    rawSend(escapeIac(text) + "\r\n");
}

void Session::scheduleWalk(const std::vector<std::string>& path) {
    delayed_.erase(std::remove_if(delayed_.begin(), delayed_.end(), [](const Delayed& d) { return d.walk; }), delayed_.end());
    double t = now_ + 0.05, step = std::max(50, settings->walkDelayMs) / 1000.0;
    for (auto& d : path) { delayed_.push_back({t, d, true}); t += step; }
}

std::string Session::roomName() const {
    const Room* r = mapper.get(mapper.current);
    return r ? r->name : "";
}

void Session::clientCommand(const std::string& cmdline) {
    std::string rest; std::string cmd = lower(firstWord(cmdline.substr(1), &rest));
    if (cmd == "help" || cmd == "?") {
        static const char* h[] = {
            "Commands (separate several with ';', '#3 n' repeats):",
            "  #set <name> <value> | #unset <name> | #vars        variables (use ${name} in commands)",
            "  #trigger <pattern> = <commands>                     quick trigger ('*' wildcards, $1.. captures)",
            "  #alias <pattern> = <commands>                       quick alias (e.g. #alias k * = kill $1)",
            "  #echo <text>   #clear   #log on|off   #reconnect    misc",
            "  #go <room id or name>   #stop                       auto-walk using the map",
            "  #map [here|info [id]|name <t>|note <t>|link <dir> <id>|undo|find <t>|list|delete [id]|auto on|off|clear]",
            "  #gmcp [package]                                     show received GMCP data",
            "Keys: Up/Down history, PgUp/PgDn scroll, numpad = walk (8/2/4/6/7/9/1/3, +/- = up/down), Ctrl+L clear",
        };
        for (auto l : h) say(l, 6);
    } else if (cmd == "echo") say(rules.expand(rest, {rest}), 7);
    else if (cmd == "set") {
        std::string v; std::string k = firstWord(rest, &v);
        if (k.empty()) say("usage: #set <name> <value>", 9); else { rules.vars[k] = v; say(k + " = " + v, 7); }
    } else if (cmd == "unset") { rules.vars.erase(rest); say("unset " + rest, 7); }
    else if (cmd == "vars") { if (rules.vars.empty()) say("(no variables)", 7); for (auto& kv : rules.vars) say(kv.first + " = " + kv.second, 7); }
    else if (cmd == "trigger" || cmd == "alias") {
        size_t eq = rest.find('=');
        if (eq == std::string::npos) { say("usage: #" + cmd + " <pattern> = <commands>", 9); return; }
        std::string pat = trim(rest.substr(0, eq)), cmds = trim(rest.substr(eq + 1));
        if (pat.empty()) { say("empty pattern", 9); return; }
        if (cmd == "trigger") { Trigger t; t.name = pat; t.pattern = pat; t.commands = cmds; rules.triggers.push_back(t); }
        else { Alias a; a.name = pat; a.pattern = pat; a.commands = cmds; rules.aliases.push_back(a); }
        saveAll(); say(cmd + " added: " + pat, 7);
    } else if (cmd == "clear") { if (hooks_ && hooks_->clear) hooks_->clear(this); }
    else if (cmd == "log") {
        Settings* s = const_cast<Settings*>(settings);
        s->logging = lower(rest) != "off"; saveSettings(*s); say(std::string("logging ") + (s->logging ? "on" : "off"), 7);
    } else if (cmd == "reconnect") { disconnect(); connect(); }
    else if (cmd == "stop") {
        delayed_.erase(std::remove_if(delayed_.begin(), delayed_.end(), [](const Delayed& d) { return d.walk; }), delayed_.end());
        say("walk stopped", 7);
    } else if (cmd == "go" || cmd == "walk") {
        if (rest.empty()) { say("usage: #go <room id or part of its name>", 9); return; }
        auto hits = mapper.search(rest);
        if (hits.empty()) { say("no room matches '" + rest + "'", 9); return; }
        if (hits.size() > 1) {
            say(std::to_string(hits.size()) + " rooms match; be more specific or use the id:", 11);
            for (size_t i = 0; i < hits.size() && i < 10; i++) { auto* r = mapper.get(hits[i]); say("  " + hits[i] + "  " + r->name + " (" + r->area + ")", 7); }
            return;
        }
        bool ok; auto path = mapper.findPath(mapper.current, hits[0], &ok);
        if (mapper.current.empty()) { say("current location unknown", 9); return; }
        if (!ok) { say("no known path to " + mapper.get(hits[0])->name, 9); return; }
        if (path.empty()) { say("already there", 7); return; }
        say("walking " + std::to_string(path.size()) + " steps to " + mapper.get(hits[0])->name, 7);
        scheduleWalk(path);
    } else if (cmd == "map") {
        std::string arg; std::string sub = lower(firstWord(rest, &arg));
        Room* cur = mapper.get(mapper.current);
        if (sub == "info") {
            std::string id = arg.empty() ? mapper.current : arg;
            std::string d = mapper.describe(id);
            if (d.empty()) say("no such room", 9);
            else { std::istringstream is(d); std::string l; while (std::getline(is, l)) say(l, 7); }
        } else if (sub.empty() || sub == "here") {
            if (!cur) say("no current room", 9);
            else { say("[" + cur->id + "] " + cur->name + " - " + cur->area + "  (" + std::to_string(cur->x) + "," + std::to_string(cur->y) + "," + std::to_string(cur->z) + ")", 7);
                   std::string ex; for (auto& e : cur->exits) ex += e.first + " "; say("exits: " + ex, 7); }
        } else if (sub == "name" && cur) { cur->name = arg; mapDirty_ = true; touchStatus(); }
        else if (sub == "note" && cur) { cur->note = arg; mapDirty_ = true; }
        else if (sub == "link") {
            std::string id; std::string d = firstWord(arg, &id);
            if (!cur || !mapper.link(cur->id, d, id)) say("usage: #map link <dir> <room id>", 9); else { mapDirty_ = true; touchStatus(); }
        } else if (sub == "undo") say(mapper.undoLast() ? "undone" : "nothing to undo", 7), touchStatus();
        else if (sub == "find") { auto h = mapper.search(arg); for (size_t i = 0; i < h.size() && i < 20; i++) say("  " + h[i] + "  " + mapper.get(h[i])->name + " (" + mapper.get(h[i])->area + ")", 7); if (h.empty()) say("none", 7); }
        else if (sub == "list") { say(std::to_string(mapper.rooms.size()) + " rooms known", 7); }
        else if (sub == "delete") { std::string id = arg.empty() ? mapper.current : arg; say(mapper.remove(id) ? "deleted " + id : "no such room", 7); touchStatus(); }
        else if (sub == "auto") { mapper.autoMap = lower(arg) != "off"; say(std::string("auto-mapping ") + (mapper.autoMap ? "on" : "off"), 7); }
        else if (sub == "clear") { mapper = Mapper(); say("map cleared", 7); touchStatus(); }
        else say("unknown #map option; see #help", 9);
        mapSaveAt_ = now_ + 5; mapRev++;
    } else if (cmd == "gmcp") {
        if (gmcp.empty()) say("no GMCP data received" , 7);
        for (auto& kv : gmcp) if (rest.empty() || lower(rest) == kv.first) say(kv.first, 6);
    } else say("unknown command '#" + cmd + "'; try #help", 9);
}

} // namespace mc
