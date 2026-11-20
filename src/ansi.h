// ANSI/SGR parser. Colours: 0-15 = theme palette index, otherwise packed 0x01RRGGBB; -1 = default.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace mc {

struct Style {
    int32_t fg = -1, bg = -1;
    bool bold = false, dim = false, italic = false, underline = false, inverse = false, strike = false;
    bool operator==(const Style& o) const {
        return fg == o.fg && bg == o.bg && bold == o.bold && dim == o.dim && italic == o.italic &&
               underline == o.underline && inverse == o.inverse && strike == o.strike;
    }
};

struct Run { std::string text; Style style; };

class AnsiParser {
public:
    // Parse one line (no newline). Updates persistent style state.
    std::vector<Run> parseLine(const std::string& line, std::string* plain = nullptr);
    // Same but leaves persistent state untouched (for partial/prompt lines).
    std::vector<Run> peekLine(const std::string& line, std::string* plain = nullptr);
    void reset() { st_ = Style(); }
    const Style& style() const { return st_; }
private:
    void applySgr(const std::string& params);
    Style st_;
};

uint32_t xterm256(int n);   // returns 0xRRGGBB

} // namespace mc
