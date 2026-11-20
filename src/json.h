// Minimal JSON parser (enough for GMCP payloads).
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <cstdio>

namespace mc {

struct JVal {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<JVal> a; std::map<std::string, JVal> o;

    const JVal& operator[](const std::string& k) const {
        static const JVal nul;
        auto it = o.find(k); return it == o.end() ? nul : it->second;
    }
    bool has(const std::string& k) const { return o.count(k) > 0; }
    double num(double def = 0) const {
        if (t == Num) return n;
        if (t == Str && !s.empty()) return atof(s.c_str());
        if (t == Bool) return b ? 1 : 0;
        return def;
    }
    std::string str(const std::string& def = "") const {
        if (t == Str) return s;
        if (t == Num) { char buf[64]; snprintf(buf, sizeof buf, "%g", n); return buf; }
        if (t == Bool) return b ? "true" : "false";
        return def;
    }
};

class JsonParser {
public:
    static bool parse(const std::string& text, JVal& out) {
        JsonParser p(text);
        p.ws();
        if (!p.value(out, 0)) return false;
        p.ws();
        return p.i_ == text.size();
    }
private:
    explicit JsonParser(const std::string& s) : s_(s) {}
    const std::string& s_; size_t i_ = 0;
    void ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) i_++; }
    bool lit(const char* w) {
        size_t n = 0; while (w[n]) n++;
        if (s_.compare(i_, n, w) == 0) { i_ += n; return true; }
        return false;
    }
    bool value(JVal& v, int depth) {
        if (depth > 64 || i_ >= s_.size()) return false;
        char c = s_[i_];
        if (c == '{') {
            i_++; v.t = JVal::Obj; ws();
            if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
            for (;;) {
                ws(); std::string k; if (!str(k)) return false;
                ws(); if (i_ >= s_.size() || s_[i_++] != ':') return false;
                ws(); JVal child; if (!value(child, depth + 1)) return false;
                v.o[k] = std::move(child);
                ws(); if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { i_++; continue; }
                if (s_[i_] == '}') { i_++; return true; }
                return false;
            }
        }
        if (c == '[') {
            i_++; v.t = JVal::Arr; ws();
            if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
            for (;;) {
                ws(); JVal child; if (!value(child, depth + 1)) return false;
                v.a.push_back(std::move(child));
                ws(); if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { i_++; continue; }
                if (s_[i_] == ']') { i_++; return true; }
                return false;
            }
        }
        if (c == '"') { v.t = JVal::Str; return str(v.s); }
        if (lit("true")) { v.t = JVal::Bool; v.b = true; return true; }
        if (lit("false")) { v.t = JVal::Bool; v.b = false; return true; }
        if (lit("null")) { v.t = JVal::Null; return true; }
        char* end = nullptr;
        double d = strtod(s_.c_str() + i_, &end);
        if (end == s_.c_str() + i_) return false;
        v.t = JVal::Num; v.n = d; i_ = end - s_.c_str();
        return true;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o.push_back((char)cp);
        else if (cp < 0x800) { o.push_back((char)(0xC0 | (cp >> 6))); o.push_back((char)(0x80 | (cp & 63))); }
        else if (cp < 0x10000) { o.push_back((char)(0xE0 | (cp >> 12))); o.push_back((char)(0x80 | ((cp >> 6) & 63))); o.push_back((char)(0x80 | (cp & 63))); }
        else { o.push_back((char)(0xF0 | (cp >> 18))); o.push_back((char)(0x80 | ((cp >> 12) & 63))); o.push_back((char)(0x80 | ((cp >> 6) & 63))); o.push_back((char)(0x80 | (cp & 63))); }
    }
    bool str(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        i_++; out.clear();
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') { out.push_back(c); continue; }
            if (i_ >= s_.size()) return false;
            char e = s_[i_++];
            switch (e) {
            case 'n': out.push_back('\n'); break; case 't': out.push_back('\t'); break; case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break; case 'f': out.push_back('\f'); break;
            case 'u': {
                if (i_ + 4 > s_.size()) return false;
                unsigned cp = (unsigned)strtoul(s_.substr(i_, 4).c_str(), nullptr, 16); i_ += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i_ + 6 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                    unsigned lo = (unsigned)strtoul(s_.substr(i_ + 2, 4).c_str(), nullptr, 16);
                    if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i_ += 6; }
                }
                utf8(out, cp); break;
            }
            default: out.push_back(e);
            }
        }
        return false;
    }
};

} // namespace mc
