#include "store.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <fstream>
#include <random>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <wincrypt.h>
#else
#include <sys/stat.h>
#endif

namespace mc {

static void mkdirs(const std::string& p) {
#ifdef _WIN32
    for (size_t i = 3; i <= p.size(); i++)
        if (i == p.size() || p[i] == '\\' || p[i] == '/') CreateDirectoryA(p.substr(0, i).c_str(), nullptr);
#else
    for (size_t i = 1; i <= p.size(); i++)
        if (i == p.size() || p[i] == '/') mkdir(p.substr(0, i).c_str(), 0700);
#endif
}

std::string dataDir() {
    static std::string d;
    if (d.empty()) {
#ifdef _WIN32
        char buf[MAX_PATH] = {0};
        if (SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf) != S_OK) strcpy(buf, ".");
        d = std::string(buf) + "\\TheMudCrawler";
        if (GetFileAttributesA(d.c_str()) == INVALID_FILE_ATTRIBUTES)       // earlier test builds used other folder names: carry the data over once
            for (const char* o : {"\\MudCrawler", "\\MudCrawler2"}) {
                std::string old = std::string(buf) + o;
                if (GetFileAttributesA(old.c_str()) != INVALID_FILE_ATTRIBUTES && MoveFileA(old.c_str(), d.c_str())) break;
            }
#else
        const char* t = getenv("MC_DATA"); const char* h = getenv("HOME");
        d = t ? t : std::string(h ? h : ".") + "/.themudcrawler";
#endif
        mkdirs(d);
    }
    return d;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
bool writeFile(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); if (!f) return false; f << data; if (!f) return false; }
#ifdef _WIN32
    return MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(tmp.c_str(), path.c_str()) == 0;
#endif
}
std::string safeName(const std::string& s) {
    std::string r;
    for (char c : s) r.push_back(isalnum((unsigned char)c) || c == '-' || c == '_' ? c : '_');
    return r.empty() ? "unnamed" : r;
}
std::string newId() {
    static std::mt19937_64 rng((uint64_t)time(nullptr) ^ (uint64_t)(size_t)&rng);
    char b[20]; snprintf(b, sizeof b, "%08x", (unsigned)(rng() & 0xffffffff)); return b;
}

// ---- password protection ----------------------------------------------------
static std::string toHex(const std::string& s) {
    static const char* h = "0123456789abcdef"; std::string r;
    for (unsigned char c : s) { r.push_back(h[c >> 4]); r.push_back(h[c & 15]); } return r;
}
static std::string fromHex(const std::string& s) {
    std::string r;
    for (size_t i = 0; i + 1 < s.size(); i += 2) r.push_back((char)strtol(s.substr(i, 2).c_str(), nullptr, 16));
    return r;
}
std::string Profile::password() const {
    if (passEnc.empty()) return "";
    std::string raw = fromHex(passEnc);
#ifdef _WIN32
    DATA_BLOB in{(DWORD)raw.size(), (BYTE*)raw.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return "";
    std::string r((char*)out.pbData, out.cbData); LocalFree(out.pbData); return r;
#else
    return raw;
#endif
}
void Profile::setPassword(const std::string& p) {
    if (p.empty()) { passEnc.clear(); return; }
#ifdef _WIN32
    DATA_BLOB in{(DWORD)p.size(), (BYTE*)p.data()}, out{};
    if (CryptProtectData(&in, L"MudCrawler", nullptr, nullptr, nullptr, 0, &out)) {
        passEnc = toHex(std::string((char*)out.pbData, out.cbData)); LocalFree(out.pbData); return;
    }
    passEnc.clear();
#else
    passEnc = toHex(p);
#endif
}

// ---- ini-ish persistence -----------------------------------------------------
static std::string esc(const std::string& s) {
    std::string r; for (char c : s) { if (c == '\\') r += "\\\\"; else if (c == '\n') r += "\\n"; else if (c != '\r') r.push_back(c); } return r;
}
static std::string unesc(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '\\' && i + 1 < s.size()) { r.push_back(s[i + 1] == 'n' ? '\n' : s[i + 1]); i++; } else r.push_back(s[i]); }
    return r;
}

std::vector<Profile> loadProfiles() {
    std::vector<Profile> out; Profile p; bool open = false;
    std::istringstream in(readFile(dataDir() + "/profiles.ini")); std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "[profile]") { if (open) out.push_back(p); p = Profile(); open = true; continue; }
        size_t eq = line.find('='); if (eq == std::string::npos || !open) continue;
        std::string k = line.substr(0, eq), v = unesc(line.substr(eq + 1));
        if (k == "id") p.id = v; else if (k == "name") p.name = v; else if (k == "host") p.host = v; else if (k == "port") p.port = atoi(v.c_str());
        else if (k == "tls") p.tls = v == "1"; else if (k == "user") p.user = v; else if (k == "pass") p.passEnc = v;
        else if (k == "userPrompt") p.userPrompt = v; else if (k == "passPrompt") p.passPrompt = v; else if (k == "postLogin") p.postLogin = v;
        else if (k == "autoLogin") p.autoLogin = v == "1"; else if (k == "autoReconnect") p.autoReconnect = v == "1";
    }
    if (open) out.push_back(p);
    for (auto& x : out) if (x.id.empty()) x.id = newId();
    return out;
}
void saveProfiles(const std::vector<Profile>& ps) {
    std::ostringstream o;
    for (auto& p : ps)
        o << "[profile]\nid=" << esc(p.id) << "\nname=" << esc(p.name) << "\nhost=" << esc(p.host) << "\nport=" << p.port << "\ntls=" << p.tls
          << "\nuser=" << esc(p.user) << "\npass=" << p.passEnc << "\nuserPrompt=" << esc(p.userPrompt) << "\npassPrompt=" << esc(p.passPrompt)
          << "\npostLogin=" << esc(p.postLogin) << "\nautoLogin=" << p.autoLogin << "\nautoReconnect=" << p.autoReconnect << "\n";
    writeFile(dataDir() + "/profiles.ini", o.str());
}

Settings loadSettings() {
    Settings s;
    std::istringstream in(readFile(dataDir() + "/settings.ini")); std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('='); if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = unesc(line.substr(eq + 1)); bool b = v == "1"; int n = atoi(v.c_str());
        if (k == "theme") s.theme = v; else if (k == "fontName") s.fontName = v; else if (k == "fontSize") s.fontSize = n > 5 ? n : 11;
        else if (k == "scrollback") s.scrollback = n >= 500 ? n : 5000; else if (k == "walkDelayMs") s.walkDelayMs = n;
        else if (k == "numpad") s.numpad = b; else if (k == "echoInput") s.echoInput = b; else if (k == "logging") s.logging = b;
        else if (k == "showMap") s.showMap = b; else if (k == "showChat") s.showChat = b;
        else if (k == "blockedPattern") s.blockedPattern = v; else if (k == "lastProfile") s.lastProfile = v;
        else if (k == "sidebarCollapsed") s.sidebarCollapsed = b; else if (k == "mapPopped") s.mapPopped = b; else if (k == "mapX") s.mapX = n; else if (k == "mapY") s.mapY = n;
        else if (k == "panelState") s.panelState = v;
        else if (k == "mapW") s.mapW = n >= 200 ? n : 420; else if (k == "mapH") s.mapH = n >= 200 ? n : 520;
    }
    return s;
}
void saveSettings(const Settings& s) {
    std::ostringstream o;
    o << "theme=" << esc(s.theme) << "\nfontName=" << esc(s.fontName) << "\nfontSize=" << s.fontSize << "\nscrollback=" << s.scrollback
      << "\nwalkDelayMs=" << s.walkDelayMs << "\nnumpad=" << s.numpad << "\nechoInput=" << s.echoInput << "\nlogging=" << s.logging
      << "\nshowMap=" << s.showMap << "\nshowChat=" << s.showChat << "\nblockedPattern=" << esc(s.blockedPattern)
      << "\nlastProfile=" << esc(s.lastProfile) << "\nsidebarCollapsed=" << s.sidebarCollapsed << "\nmapPopped=" << s.mapPopped << "\nmapX=" << s.mapX << "\nmapY=" << s.mapY
      << "\nmapW=" << s.mapW << "\nmapH=" << s.mapH << "\npanelState=" << esc(s.panelState) << "\n";
    writeFile(dataDir() + "/settings.ini", o.str());
}

} // namespace mc
