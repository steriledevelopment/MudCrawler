// Telnet protocol parser: platform independent, no sockets.
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace mc {

enum : uint8_t { IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240, GA = 249, EOR_CMD = 239 };
enum : uint8_t { OPT_ECHO = 1, OPT_SGA = 3, OPT_TTYPE = 24, OPT_EOR = 25, OPT_NAWS = 31, OPT_MSDP = 69, OPT_MCCP2 = 86, OPT_GMCP = 201 };

struct TelnetEvent {
    enum Type { Data, Prompt, Neg, Sub } type;
    std::string data;   // Data: text bytes; Sub: payload after option byte
    uint8_t cmd = 0;    // Neg: DO/DONT/WILL/WONT
    uint8_t opt = 0;    // Neg/Sub: option
};

class TelnetParser {
public:
    // Parses bytes. If MCCP2 compression starts, returns the number of bytes consumed
    // (remaining bytes are compressed); otherwise returns len.
    size_t feed(const uint8_t* buf, size_t len, std::vector<TelnetEvent>& out);
private:
    int state_ = 0;
    uint8_t cmd_ = 0;
    std::string sb_;
};

std::string escapeIac(const std::string& s);
std::string buildNegotiation(uint8_t cmd, uint8_t opt);
std::string buildSub(uint8_t opt, const std::string& payload);   // IAC SB opt payload IAC SE (IAC-escaped)
std::string buildNaws(int cols, int rows);
std::string buildGmcp(const std::string& pkg, const std::string& json);

// GMCP payload "Pkg.Name {json}" -> (pkg, json)
void splitGmcp(const std::string& payload, std::string& pkg, std::string& json);

} // namespace mc
