// Auto-mapper: room graph, GMCP/manual tracking, pathfinding, persistence.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>

namespace mc {

struct Room {
    std::string id, name, area, terrain, note, desc;   // desc = text the MUD showed when we last entered/looked here
    int x = 0, y = 0, z = 0, visits = 0;
    bool placed = false;
    std::map<std::string, std::string> exits;   // dir -> target room id ("" = unexplored)
};

std::string normDir(const std::string& s);       // "north" -> "n"; "" if not a direction
std::string oppositeDir(const std::string& d);
bool dirOffset(const std::string& d, int& dx, int& dy, int& dz);

class Mapper {
public:
    std::map<std::string, Room> rooms;
    std::string current;
    bool autoMap = true;

    // User sent a movement command (call before the move resolves).
    void noteMove(const std::string& dir) { pending_ = normDir(dir); }

    // GMCP-driven arrival (room ids supplied by the MUD).
    Room& arrive(const std::string& id, const std::string& name, const std::string& area,
                 const std::map<std::string, std::string>& exits, const std::string& terrain = "");

    // Manual/dead-reckoning mode: move in `dir`, creating a new room if the exit is unknown.
    // Returns the new current room id, or "" if dir is invalid.
    std::string moveManual(const std::string& dir);
    // Undo the last room created by moveManual (e.g. the move was blocked).
    bool undoLast();

    const Room* get(const std::string& id) const { auto it = rooms.find(id); return it == rooms.end() ? nullptr : &it->second; }
    Room* get(const std::string& id) { auto it = rooms.find(id); return it == rooms.end() ? nullptr : &it->second; }

    // Shortest path as a list of directions. Empty + found=false if unreachable.
    std::vector<std::string> findPath(const std::string& from, const std::string& to, bool* found = nullptr) const;
    // Resolve user text to a room id: exact id, then name substring (case-insensitive).
    std::vector<std::string> search(const std::string& text) const;

    // Human-readable summary of a room (used by the info card, #map info and Copy)
    std::string describe(const std::string& id) const;

    bool link(const std::string& from, const std::string& dir, const std::string& to);
    bool remove(const std::string& id);

    std::string serialize() const;
    void deserialize(const std::string& text);

private:
    void place(Room& r, const Room* prev, const std::string& dir);
    bool occupied(const std::string& area, int x, int y, int z, const std::string& except) const;
    std::string pending_;
    std::string lastCreated_, lastFrom_, lastDir_;
    int nextManual_ = 1;
};

} // namespace mc
