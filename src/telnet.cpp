#include "telnet.h"
namespace mc {

size_t TelnetParser::feed(const uint8_t* buf, size_t len, std::vector<TelnetEvent>& out) {
    std::string data;
    auto flush = [&] {
        if (!data.empty()) { out.push_back({TelnetEvent::Data, data, 0, 0}); data.clear(); }
    };
    for (size_t i = 0; i < len; i++) {
        uint8_t b = buf[i];
        switch (state_) {
        case 0:
            if (b == IAC) state_ = 1; else if (b != 0) data.push_back((char)b);
            break;
        case 1:
            if (b == IAC) { data.push_back((char)255); state_ = 0; }
            else if (b == DO || b == DONT || b == WILL || b == WONT) { cmd_ = b; state_ = 2; }
            else if (b == SB) { flush(); sb_.clear(); state_ = 3; }
            else { if (b == GA || b == EOR_CMD) { flush(); out.push_back({TelnetEvent::Prompt, "", 0, 0}); } state_ = 0; }
            break;
        case 2:
            flush(); out.push_back({TelnetEvent::Neg, "", cmd_, b}); state_ = 0;
            break;
        case 3:
            if (b == IAC) state_ = 4; else sb_.push_back((char)b);
            break;
        case 4:
            if (b == SE) {
                if (!sb_.empty()) {
                    uint8_t opt = (uint8_t)sb_[0];
                    out.push_back({TelnetEvent::Sub, sb_.substr(1), 0, opt});
                    state_ = 0;
                    if (opt == OPT_MCCP2) { flush(); return i + 1; }
                } else state_ = 0;
            } else if (b == IAC) { sb_.push_back((char)255); state_ = 3; }
            else state_ = 3;
            break;
        }
    }
    flush();
    return len;
}

std::string escapeIac(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (char c : s) { r.push_back(c); if ((uint8_t)c == IAC) r.push_back((char)IAC); }
    return r;
}
std::string buildNegotiation(uint8_t cmd, uint8_t opt) { return std::string{(char)IAC, (char)cmd, (char)opt}; }
std::string buildSub(uint8_t opt, const std::string& payload) {
    return std::string{(char)IAC, (char)SB, (char)opt} + escapeIac(payload) + std::string{(char)IAC, (char)SE};
}
std::string buildNaws(int c, int r) {
    std::string p{(char)(c >> 8), (char)(c & 255), (char)(r >> 8), (char)(r & 255)};
    return buildSub(OPT_NAWS, p);
}
std::string buildGmcp(const std::string& pkg, const std::string& json) {
    return buildSub(OPT_GMCP, json.empty() ? pkg : pkg + " " + json);
}
void splitGmcp(const std::string& p, std::string& pkg, std::string& json) {
    size_t sp = p.find(' ');
    if (sp == std::string::npos) { pkg = p; json.clear(); }
    else { pkg = p.substr(0, sp); json = p.substr(sp + 1); }
}

} // namespace mc
