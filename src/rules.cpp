#include "rules.h"
#include <sstream>
#include <algorithm>
#include <cstdlib>
namespace mc {

std::string escapeRegex(const std::string& s) {
    static const std::string special = "\\^$.|?*+()[]{}";
    std::string r;
    for (char c : s) { if (special.find(c) != std::string::npos) r.push_back('\\'); r.push_back(c); }
    return r;
}
std::string globToRegex(const std::string& g) {
    std::string r = "^";
    for (char c : g) {
        if (c == '*') r += "(.*?)";
        else if (c == '?') r += "(.)";
        else r += escapeRegex(std::string(1, c));
    }
    return r + "$";
}

const std::regex* RuleEngine::compiled(MatchType t, const std::string& pattern) {
    std::string src = t == MatchType::Regex ? pattern : t == MatchType::Glob ? globToRegex(pattern) : escapeRegex(pattern);
    std::string key = std::string(1, (char)('0' + (int)t)) + src;
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.get();
    if (bad_.count(key)) return nullptr;
    try {
        auto re = std::make_unique<std::regex>(src, std::regex::ECMAScript | std::regex::icase);
        auto* p = re.get();
        cache_[key] = std::move(re);
        return p;
    } catch (const std::regex_error& e) {
        bad_[key] = true;
        lastError = "Bad pattern '" + pattern + "': " + e.what();
        return nullptr;
    }
}

std::string RuleEngine::expand(const std::string& t, const std::vector<std::string>& caps) const {
    std::string out;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '$' && i + 1 < t.size()) {
            char n = t[i + 1];
            if (n >= '0' && n <= '9') {
                size_t k = n - '0';
                if (k < caps.size()) out += caps[k];
                i++; continue;
            }
            if (n == '*') { if (!caps.empty()) out += caps[0]; i++; continue; }
            if (n == '{') {
                size_t e = t.find('}', i + 2);
                if (e != std::string::npos) {
                    auto it = vars.find(t.substr(i + 2, e - i - 2));
                    if (it != vars.end()) out += it->second;
                    i = e; continue;
                }
            }
            if (n == '$') { out.push_back('$'); i++; continue; }
        }
        out.push_back(t[i]);
    }
    return out;
}

static std::vector<std::string> captures(const std::smatch& m) {
    std::vector<std::string> c;
    for (size_t i = 0; i < m.size(); i++) c.push_back(m[i].str());
    return c;
}

LineResult RuleEngine::matchLine(const std::string& plain) {
    LineResult res;
    for (auto& t : triggers) {
        if (!t.enabled || t.pattern.empty()) continue;
        const std::regex* re = compiled(t.type, t.pattern);
        if (!re) continue;
        std::smatch m;
        bool hit = t.type == MatchType::Substring ? std::regex_search(plain, m, *re)
                 : t.type == MatchType::Glob ? std::regex_match(plain, m, *re)
                 : std::regex_search(plain, m, *re);
        if (!hit) continue;
        auto caps = captures(m);
        if (t.type == MatchType::Glob || t.type == MatchType::Substring) caps[0] = plain;
        if (t.gag) res.gag = true;
        if (t.fg != -1) res.fg = t.fg;
        if (t.pane != "main") res.pane = t.pane;
        if (!t.commands.empty()) res.commands.push_back(expand(t.commands, caps));
        if (t.once) t.enabled = false;
        if (t.stop) break;
    }
    return res;
}

static std::vector<std::string> splitCommands(const std::string& s, char sep) {
    std::vector<std::string> out; std::string cur;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == sep) { cur.push_back(sep); i++; }
        else if (s[i] == sep || s[i] == '\n') { out.push_back(cur); cur.clear(); }
        else cur.push_back(s[i]);
    }
    out.push_back(cur);
    return out;
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::vector<Action> RuleEngine::processInput(const std::string& input, int depth) {
    std::vector<Action> out;
    if (depth > 8) { out.push_back({true, "#echo alias recursion limit reached"}); return out; }
    // '#' client commands take the whole line (so '#set x a;b' keeps its separator)
    std::string whole = trim(input);
    for (auto& part : splitCommands(input, separator)) {
        std::string cmd = trim(part);
        if (cmd.empty() && whole.empty()) { out.push_back({false, ""}); continue; }   // bare Enter
        if (cmd.empty()) continue;
        if (cmd[0] == '#') {
            // "#N command" repeat
            size_t i = 1; while (i < cmd.size() && isdigit((unsigned char)cmd[i])) i++;
            if (i > 1 && i < cmd.size() && cmd[i] == ' ') {
                int n = std::min(atoi(cmd.substr(1, i - 1).c_str()), 200);
                std::string rest = cmd.substr(i + 1);
                for (int k = 0; k < n; k++) for (auto& a : processInput(rest, depth + 1)) out.push_back(a);
                continue;
            }
            out.push_back({true, cmd});
            continue;
        }
        bool matched = false;
        for (auto& a : aliases) {
            if (!a.enabled || a.pattern.empty()) continue;
            const std::regex* re = compiled(a.type, a.pattern);
            if (!re) continue;
            std::smatch m;
            bool hit = a.type == MatchType::Regex ? std::regex_search(cmd, m, *re) : std::regex_match(cmd, m, *re);
            if (!hit) continue;
            auto caps = captures(m);
            if (a.type != MatchType::Regex) caps[0] = cmd;
            // $* = everything after the first word
            size_t sp = cmd.find(' ');
            if (a.type == MatchType::Glob) {
                // keep caps[1..] from the glob wildcards
            }
            (void)sp;
            for (auto& sub : processInput(expand(a.commands, caps), depth + 1)) out.push_back(sub);
            matched = true;
            break;
        }
        if (!matched) out.push_back({false, expand(cmd, {cmd})});
    }
    return out;
}

std::vector<std::string> RuleEngine::tick(double dt) {
    std::vector<std::string> out;
    for (auto& t : timers) {
        if (!t.enabled) continue;
        t.elapsed += dt;
        if (t.elapsed >= t.seconds) {
            t.elapsed = 0;
            if (!t.repeat) t.enabled = false;
            out.push_back(expand(t.commands, {""}));
        }
    }
    return out;
}

// ---- persistence: simple "[section]\nkey=value" with \n and \\ escapes ----------
static std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) { if (c == '\\') r += "\\\\"; else if (c == '\n') r += "\\n"; else if (c != '\r') r.push_back(c); }
    return r;
}
static std::string unesc(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { r.push_back(s[i + 1] == 'n' ? '\n' : s[i + 1]); i++; }
        else r.push_back(s[i]);
    }
    return r;
}

std::string RuleEngine::serialize() const {
    std::ostringstream o;
    o << "separator=" << separator << "\n";
    for (auto& t : triggers)
        o << "[trigger]\nname=" << esc(t.name) << "\npattern=" << esc(t.pattern) << "\ntype=" << (int)t.type
          << "\ncommands=" << esc(t.commands) << "\nenabled=" << t.enabled << "\ngag=" << t.gag << "\nonce=" << t.once
          << "\nstop=" << t.stop << "\nfg=" << t.fg << "\npane=" << esc(t.pane) << "\n";
    for (auto& a : aliases)
        o << "[alias]\nname=" << esc(a.name) << "\npattern=" << esc(a.pattern) << "\ntype=" << (int)a.type
          << "\ncommands=" << esc(a.commands) << "\nenabled=" << a.enabled << "\n";
    for (auto& t : timers)
        o << "[timer]\nname=" << esc(t.name) << "\nseconds=" << t.seconds << "\nrepeat=" << t.repeat
          << "\ncommands=" << esc(t.commands) << "\nenabled=" << t.enabled << "\n";
    for (auto& v : vars) o << "[var]\nname=" << esc(v.first) << "\nvalue=" << esc(v.second) << "\n";
    return o.str();
}

void RuleEngine::deserialize(const std::string& text) {
    triggers.clear(); aliases.clear(); timers.clear(); vars.clear(); cache_.clear(); bad_.clear();
    std::istringstream in(text);
    std::string line, section;
    Trigger t; Alias a; Timer tm; std::string vname;
    auto commit = [&] {
        if (section == "trigger") triggers.push_back(t);
        else if (section == "alias") aliases.push_back(a);
        else if (section == "timer") timers.push_back(tm);
        section.clear();
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '[') {
            commit();
            section = line.substr(1, line.size() - 2);
            t = Trigger(); a = Alias(); tm = Timer(); vname.clear();
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = unesc(line.substr(eq + 1));
        if (section.empty()) { if (k == "separator" && !v.empty()) separator = v[0]; continue; }
        bool b = v == "1";
        if (section == "trigger") {
            if (k == "name") t.name = v; else if (k == "pattern") t.pattern = v; else if (k == "type") t.type = (MatchType)atoi(v.c_str());
            else if (k == "commands") t.commands = v; else if (k == "enabled") t.enabled = b; else if (k == "gag") t.gag = b;
            else if (k == "once") t.once = b; else if (k == "stop") t.stop = b; else if (k == "fg") t.fg = atoi(v.c_str()); else if (k == "pane") t.pane = v;
        } else if (section == "alias") {
            if (k == "name") a.name = v; else if (k == "pattern") a.pattern = v; else if (k == "type") a.type = (MatchType)atoi(v.c_str());
            else if (k == "commands") a.commands = v; else if (k == "enabled") a.enabled = b;
        } else if (section == "timer") {
            if (k == "name") tm.name = v; else if (k == "seconds") tm.seconds = atof(v.c_str()); else if (k == "repeat") tm.repeat = b;
            else if (k == "commands") tm.commands = v; else if (k == "enabled") tm.enabled = b;
        } else if (section == "var") {
            if (k == "name") vname = v; else if (k == "value" && !vname.empty()) vars[vname] = v;
        }
    }
    commit();
}

} // namespace mc
