#include "ansi.h"
#include <cstdlib>
namespace mc {

uint32_t xterm256(int n) {
    if (n < 16) {
        static const uint32_t base[16] = {0x000000,0xcd0000,0x00cd00,0xcdcd00,0x0000ee,0xcd00cd,0x00cdcd,0xe5e5e5,
                                          0x7f7f7f,0xff0000,0x00ff00,0xffff00,0x5c5cff,0xff00ff,0x00ffff,0xffffff};
        return base[n];
    }
    if (n < 232) {
        n -= 16;
        int r = n / 36, g = (n / 6) % 6, b = n % 6;
        auto c = [](int v) { return v ? 55 + v * 40 : 0; };
        return (c(r) << 16) | (c(g) << 8) | c(b);
    }
    int v = 8 + (n - 232) * 10;
    return (v << 16) | (v << 8) | v;
}

static int32_t pack(int r, int g, int b) {
    return (int32_t)(0x01000000 | ((r & 255) << 16) | ((g & 255) << 8) | (b & 255));
}

void AnsiParser::applySgr(const std::string& params) {
    std::vector<int> v;
    {
        std::string cur; bool any = false;
        for (char c : params) {
            if (c == ';' || c == ':') { v.push_back(cur.empty() ? 0 : atoi(cur.c_str())); cur.clear(); any = true; }
            else cur.push_back(c);
        }
        v.push_back(cur.empty() ? 0 : atoi(cur.c_str()));
        (void)any;
    }
    for (size_t i = 0; i < v.size(); i++) {
        int c = v[i];
        if (c == 0) st_ = Style();
        else if (c == 1) st_.bold = true;
        else if (c == 2) st_.dim = true;
        else if (c == 3) st_.italic = true;
        else if (c == 4) st_.underline = true;
        else if (c == 7) st_.inverse = true;
        else if (c == 9) st_.strike = true;
        else if (c == 22) { st_.bold = false; st_.dim = false; }
        else if (c == 23) st_.italic = false;
        else if (c == 24) st_.underline = false;
        else if (c == 27) st_.inverse = false;
        else if (c == 29) st_.strike = false;
        else if (c >= 30 && c <= 37) st_.fg = c - 30;
        else if (c == 39) st_.fg = -1;
        else if (c >= 40 && c <= 47) st_.bg = c - 40;
        else if (c == 49) st_.bg = -1;
        else if (c >= 90 && c <= 97) st_.fg = 8 + c - 90;
        else if (c >= 100 && c <= 107) st_.bg = 8 + c - 100;
        else if (c == 38 || c == 48) {
            int32_t* dst = (c == 38) ? &st_.fg : &st_.bg;
            if (i + 1 < v.size() && v[i + 1] == 5 && i + 2 < v.size()) {
                int n = v[i + 2]; i += 2;
                *dst = n < 16 ? n : pack((xterm256(n) >> 16), (xterm256(n) >> 8), xterm256(n));
            } else if (i + 1 < v.size() && v[i + 1] == 2 && i + 4 < v.size()) {
                *dst = pack(v[i + 2], v[i + 3], v[i + 4]); i += 4;
            }
        }
    }
}

std::vector<Run> AnsiParser::parseLine(const std::string& s, std::string* plain) {
    std::vector<Run> runs;
    std::string cur;
    if (plain) plain->clear();
    auto push = [&] {
        if (cur.empty()) return;
        if (!runs.empty() && runs.back().style == st_) runs.back().text += cur;
        else runs.push_back({cur, st_});
        if (plain) *plain += cur;
        cur.clear();
    };
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0x1b) {
            if (i + 1 >= s.size()) break;
            char n = s[i + 1];
            if (n == '[') {
                size_t j = i + 2;
                while (j < s.size() && ((s[j] >= '0' && s[j] <= '?'))) j++;   // params
                while (j < s.size() && (s[j] >= ' ' && s[j] <= '/')) j++;     // intermediates
                if (j >= s.size()) { i = s.size(); break; }
                if (s[j] == 'm') { push(); applySgr(s.substr(i + 2, j - i - 2)); }
                i = j;
            } else if (n == ']' || n == 'P') {
                size_t j = i + 2;
                while (j < s.size() && s[j] != 0x07 && !(s[j] == 0x1b && j + 1 < s.size() && s[j + 1] == '\\')) j++;
                i = (j < s.size() && s[j] == 0x1b) ? j + 1 : j;
            } else if (n == '(' || n == ')') i += 2;
            else i += 1;
            continue;
        }
        if (c == '\r') continue;
        if (c == '\t') { cur += "    "; continue; }
        if (c < 0x20 || c == 0x7f) continue;
        cur.push_back((char)c);
    }
    push();
    return runs;
}

std::vector<Run> AnsiParser::peekLine(const std::string& line, std::string* plain) {
    Style saved = st_;
    auto r = parseLine(line, plain);
    st_ = saved;
    return r;
}

} // namespace mc
