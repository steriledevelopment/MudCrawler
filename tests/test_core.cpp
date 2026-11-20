#include "../src/telnet.h"
#include "../src/ansi.h"
#include <cstdio>
#include <cstring>
using namespace mc;
static int fails = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)

static void test_telnet() {
    TelnetParser p; std::vector<TelnetEvent> ev;
    const uint8_t a[] = {'h','i',255,255,'!',255,251,201,'x',255,250,201,'A','.','B',' ','{','}',255,240,255,249};
    size_t n = p.feed(a, sizeof a, ev);
    CHECK(n == sizeof a);
    CHECK(ev.size() == 5);
    CHECK(ev[0].type == TelnetEvent::Data && ev[0].data == std::string("hi\xff!", 4));
    CHECK(ev[1].type == TelnetEvent::Neg && ev[1].cmd == WILL && ev[1].opt == OPT_GMCP);
    CHECK(ev[2].type == TelnetEvent::Data && ev[2].data == "x");
    CHECK(ev[3].type == TelnetEvent::Sub && ev[3].opt == OPT_GMCP && ev[3].data == "A.B {}");
    CHECK(ev[4].type == TelnetEvent::Prompt);
    // split across feeds
    TelnetParser q; ev.clear();
    const uint8_t b1[] = {255}, b2[] = {253}, b3[] = {31};
    q.feed(b1, 1, ev); q.feed(b2, 1, ev); CHECK(ev.empty()); q.feed(b3, 1, ev);
    CHECK(ev.size() == 1 && ev[0].cmd == DO && ev[0].opt == OPT_NAWS);
    // MCCP2 start returns consumed length
    TelnetParser m; ev.clear();
    const uint8_t c[] = {'a',255,250,86,255,240,0x78,0x9c,1,2};
    n = m.feed(c, sizeof c, ev);
    CHECK(n == 6);
    CHECK(ev.back().type == TelnetEvent::Sub && ev.back().opt == OPT_MCCP2);
    // builders
    CHECK(buildNaws(255, 24) == std::string("\xff\xfa\x1f\x00\xff\xff\x00\x18\xff\xf0", 10));
    std::string pkg, js; splitGmcp("Room.Info {\"num\":1}", pkg, js);
    CHECK(pkg == "Room.Info" && js == "{\"num\":1}");
}

static void test_ansi() {
    AnsiParser a; std::string plain;
    auto r = a.parseLine("\x1b[1;31mred\x1b[0m plain \x1b[38;5;196mx\x1b[38;2;1;2;3my", &plain);
    CHECK(plain == "red plain xy");
    CHECK(r.size() == 4);
    CHECK(r[0].style.fg == 1 && r[0].style.bold);
    CHECK(r[1].style.fg == -1 && !r[1].style.bold);
    CHECK(r[2].style.fg == (int32_t)(0x01000000 | 0xff0000));
    CHECK(r[3].style.fg == (int32_t)(0x01000000 | 0x010203));
    // state persists across lines
    AnsiParser b; b.parseLine("\x1b[32m");
    r = b.parseLine("green");
    CHECK(r.size() == 1 && r[0].style.fg == 2);
    // peek does not mutate
    AnsiParser c; c.peekLine("\x1b[31mred"); r = c.parseLine("t");
    CHECK(r[0].style.fg == -1);
    // strips junk
    r = a.parseLine("a\x1b]0;title\x07" "b\x1b[2Jc\r", &plain);
    CHECK(plain == "abc");
}

int main() { test_telnet(); test_ansi(); printf(fails ? "%d FAILURES\n" : "core tests OK\n", fails); return fails ? 1 : 0; }
