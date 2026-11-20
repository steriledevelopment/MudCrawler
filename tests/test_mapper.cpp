#include "../src/mapper.h"
#include <cstdio>
using namespace mc;
static int fails = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)
int main() {
    CHECK(normDir("North") == "n" && normDir("sw") == "sw" && normDir("look").empty());
    Mapper m;
    // GMCP arrivals
    m.arrive("100", "Temple", "Town", {{"north", "101"}, {"east", "102"}});
    m.noteMove("n");
    m.arrive("101", "Square", "Town", {{"south", "100"}});
    CHECK(m.rooms["100"].x == 0 && m.rooms["100"].y == 0);
    CHECK(m.rooms["101"].y == -1 && m.rooms["101"].x == 0);
    m.noteMove("s"); m.arrive("100", "Temple", "Town", {});
    m.noteMove("e"); m.arrive("102", "Inn", "Town", {{"west", "100"}, {"up", "103"}});
    CHECK(m.rooms["102"].x == 1);
    m.noteMove("u"); m.arrive("103", "Attic", "Town", {});
    CHECK(m.rooms["103"].z == 1 && m.rooms["103"].exits["d"] == "102");
    bool ok; auto p = m.findPath("101", "103", &ok);
    CHECK(ok && p.size() == 3 && p[0] == "s" && p[1] == "e" && p[2] == "u");
    m.findPath("101", "nope", &ok); CHECK(!ok);
    CHECK(m.search("inn").size() == 1 && m.search("100").size() == 1);
    // collision nudging: a different room claiming an occupied spot
    m.current = "100"; m.noteMove("n"); m.arrive("200", "Clash", "Town", {});
    CHECK(!(m.rooms["200"].x == 0 && m.rooms["200"].y == -1));
    // manual mode
    Mapper g;
    std::string a = g.moveManual("north"), b = g.moveManual("e");
    CHECK(g.rooms.size() == 3 && g.rooms[b].x == 1 && g.rooms[b].y == -1);
    CHECK(g.moveManual("w") == a);          // follows existing exit back
    CHECK(g.moveManual("w") != a);          // new room
    CHECK(g.undoLast() && g.current == a);
    CHECK(g.moveManual("xyz").empty());
    // persistence
    Mapper h; h.deserialize(m.serialize());
    CHECK(h.rooms.size() == m.rooms.size() && h.current == m.current && h.rooms["103"].z == 1 && h.rooms["102"].exits["u"] == "103");
    CHECK(h.remove("102") && !h.rooms["100"].exits.count("e"));
    // room info: visits, description, note, describe(), persistence (incl. old 10-field format)
    CHECK(m.rooms["100"].visits >= 2 && m.rooms["103"].visits == 1);
    m.rooms["102"].desc = "A cozy room.\nWith\ttabs and \\ slashes."; m.rooms["102"].note = "buy ale";
    std::string d = m.describe("102");
    CHECK(d.find("Inn  [102]") == 0 && d.find("w -> Temple") != std::string::npos && d.find("u -> Attic") != std::string::npos);
    CHECK(d.find("Note: buy ale") != std::string::npos && d.find("A cozy room.") != std::string::npos && m.describe("zzz").empty());
    Mapper k; k.deserialize(m.serialize());
    CHECK(k.rooms["102"].desc == m.rooms["102"].desc && k.rooms["102"].note == "buy ale" && k.rooms["100"].visits == m.rooms["100"].visits);
    Mapper old; old.deserialize("R\t1\tOld\tA\t\t\t0\t0\t0\t1\n");   // saved by the previous version: no desc/visits fields
    CHECK(old.rooms.size() == 1 && old.rooms["1"].desc.empty() && old.rooms["1"].visits == 0);
    printf(fails ? "%d FAILURES\n" : "mapper tests OK\n", fails); return fails ? 1 : 0;
}
