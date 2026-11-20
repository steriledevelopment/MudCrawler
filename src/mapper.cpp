#include "mapper.h"
#include <algorithm>
#include <queue>
#include <sstream>
#include <cctype>
namespace mc {

static const struct { const char* name; const char* abbr; const char* opp; int dx, dy, dz; } kDirs[] = {
    {"north","n","s",0,-1,0}, {"south","s","n",0,1,0}, {"east","e","w",1,0,0}, {"west","w","e",-1,0,0},
    {"northeast","ne","sw",1,-1,0}, {"northwest","nw","se",-1,-1,0}, {"southeast","se","nw",1,1,0}, {"southwest","sw","ne",-1,1,0},
    {"up","u","d",0,0,1}, {"down","d","u",0,0,-1}, {"in","in","out",0,0,0}, {"out","out","in",0,0,0},
};

std::string normDir(const std::string& s) {
    std::string l; for (char c : s) l.push_back((char)tolower((unsigned char)c));
    for (auto& d : kDirs) if (l == d.name || l == d.abbr) return d.abbr;
    return "";
}
std::string oppositeDir(const std::string& d) { for (auto& k : kDirs) if (d == k.abbr) return k.opp; return ""; }
bool dirOffset(const std::string& d, int& dx, int& dy, int& dz) {
    for (auto& k : kDirs) if (d == k.abbr) { dx = k.dx; dy = k.dy; dz = k.dz; return true; }
    return false;
}

bool Mapper::occupied(const std::string& area, int x, int y, int z, const std::string& except) const {
    for (auto& kv : rooms) {
        const Room& r = kv.second;
        if (r.placed && r.id != except && r.area == area && r.x == x && r.y == y && r.z == z) return true;
    }
    return false;
}

void Mapper::place(Room& r, const Room* prev, const std::string& dir) {
    int bx = 0, by = 0, bz = 0;
    if (prev && prev->area == r.area) {
        int dx = 0, dy = 0, dz = 0;
        if (dirOffset(dir, dx, dy, dz)) { bx = prev->x + dx; by = prev->y + dy; bz = prev->z + dz; }
        else { bx = prev->x + 1; by = prev->y; bz = prev->z; }   // in/out: put it beside
    }
    // spiral out to find a free spot
    for (int rad = 0; rad < 60; rad++)
        for (int dy = -rad; dy <= rad; dy++)
            for (int dx = -rad; dx <= rad; dx++) {
                if (std::max(abs(dx), abs(dy)) != rad) continue;
                if (!occupied(r.area, bx + dx, by + dy, bz, r.id)) { r.x = bx + dx; r.y = by + dy; r.z = bz; r.placed = true; return; }
            }
    r.x = bx; r.y = by; r.z = bz; r.placed = true;
}

Room& Mapper::arrive(const std::string& id, const std::string& name, const std::string& area,
                     const std::map<std::string, std::string>& exits, const std::string& terrain) {
    bool isNew = rooms.find(id) == rooms.end();
    Room& r = rooms[id];
    r.id = id;
    if (!name.empty()) r.name = name;
    if (!area.empty()) r.area = area;
    if (!terrain.empty()) r.terrain = terrain;
    for (auto& e : exits) { std::string d = normDir(e.first); r.exits[d.empty() ? e.first : d] = e.second; }

    const Room* prev = (autoMap && !current.empty() && current != id) ? get(current) : nullptr;
    std::string dir = pending_;
    pending_.clear();
    if (!r.placed) place(r, prev && !dir.empty() ? prev : nullptr, dir);
    if (prev && !dir.empty()) {
        Room* p = get(current);
        if (p && (p->exits[dir].empty() || p->exits[dir] != id)) p->exits[dir] = id;
        std::string opp = oppositeDir(dir);
        if (!opp.empty() && r.exits.find(opp) == r.exits.end()) r.exits[opp] = current;
    }
    (void)isNew;
    if (current != id || isNew) r.visits++;
    current = id;
    return r;
}

std::string Mapper::moveManual(const std::string& dirIn) {
    std::string dir = normDir(dirIn);
    if (dir.empty()) return "";
    pending_.clear();
    if (current.empty()) {
        Room& r = rooms["m" + std::to_string(nextManual_++)];
        r.id = "m" + std::to_string(nextManual_ - 1); r.name = "Start"; r.area = "Manual"; r.placed = true;
        current = r.id; lastCreated_.clear();
    }
    Room& cur = rooms[current];
    auto it = cur.exits.find(dir);
    if (it != cur.exits.end() && !it->second.empty() && rooms.count(it->second)) {
        current = it->second; lastCreated_.clear();
        rooms[current].visits++;
        return current;
    }
    std::string id;
    do { id = "m" + std::to_string(nextManual_++); } while (rooms.count(id));
    Room& n = rooms[id];
    n.id = id; n.name = "Room " + id.substr(1); n.area = cur.area.empty() ? "Manual" : cur.area;
    place(n, &rooms[current], dir);
    Room& c2 = rooms[current];
    c2.exits[dir] = id;
    std::string opp = oppositeDir(dir);
    if (!opp.empty()) n.exits[opp] = current;
    lastCreated_ = id; lastFrom_ = current; lastDir_ = dir;
    n.visits = 1;
    current = id;
    return id;
}

bool Mapper::undoLast() {
    if (lastCreated_.empty() || !rooms.count(lastCreated_)) return false;
    rooms.erase(lastCreated_);
    auto it = rooms.find(lastFrom_);
    if (it != rooms.end()) it->second.exits.erase(lastDir_);
    current = lastFrom_;
    lastCreated_.clear();
    return true;
}

std::vector<std::string> Mapper::findPath(const std::string& from, const std::string& to, bool* found) const {
    std::vector<std::string> path;
    if (found) *found = false;
    if (from.empty() || to.empty() || !rooms.count(from) || !rooms.count(to)) return path;
    if (from == to) { if (found) *found = true; return path; }
    std::map<std::string, std::pair<std::string, std::string>> prev;   // id -> (parent, dir)
    std::queue<std::string> q; q.push(from); prev[from] = {"", ""};
    while (!q.empty()) {
        std::string c = q.front(); q.pop();
        const Room& r = rooms.at(c);
        for (auto& e : r.exits) {
            if (e.second.empty() || !rooms.count(e.second) || prev.count(e.second)) continue;
            prev[e.second] = {c, e.first};
            if (e.second == to) {
                for (std::string at = to; at != from; at = prev[at].first) path.push_back(prev[at].second);
                std::reverse(path.begin(), path.end());
                if (found) *found = true;
                return path;
            }
            q.push(e.second);
        }
    }
    return path;
}

std::vector<std::string> Mapper::search(const std::string& text) const {
    std::vector<std::string> out;
    if (rooms.count(text)) { out.push_back(text); return out; }
    auto low = [](std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; };
    std::string t = low(text);
    for (auto& kv : rooms) if (low(kv.second.name).find(t) != std::string::npos) out.push_back(kv.first);
    return out;
}

std::string Mapper::describe(const std::string& id) const {
    const Room* r = get(id);
    if (!r) return "";
    std::ostringstream o;
    o << r->name << "  [" << r->id << "]\n";
    o << "Area: " << (r->area.empty() ? "?" : r->area);
    if (!r->terrain.empty()) o << "   Terrain: " << r->terrain;
    o << "   Position: " << r->x << "," << r->y << "," << r->z;
    if (r->visits > 0) o << "   Visited: " << r->visits << (r->visits == 1 ? " time" : " times");
    o << "\nExits: ";
    if (r->exits.empty()) o << "none known";
    bool first = true;
    for (auto& e : r->exits) {
        if (!first) o << ", ";
        first = false;
        const Room* t = e.second.empty() ? nullptr : get(e.second);
        o << e.first << " -> " << (t ? t->name : e.second.empty() ? std::string("?") : e.second);
    }
    o << "\n";
    if (!r->note.empty()) o << "Note: " << r->note << "\n";
    if (!r->desc.empty()) o << "\n" << r->desc << "\n";
    return o.str();
}

bool Mapper::link(const std::string& from, const std::string& dir, const std::string& to) {
    std::string d = normDir(dir);
    if (d.empty() || !rooms.count(from) || !rooms.count(to)) return false;
    rooms[from].exits[d] = to;
    return true;
}

bool Mapper::remove(const std::string& id) {
    if (!rooms.erase(id)) return false;
    for (auto& kv : rooms) for (auto it = kv.second.exits.begin(); it != kv.second.exits.end();) {
        if (it->second == id) it = kv.second.exits.erase(it); else ++it;
    }
    if (current == id) current.clear();
    return true;
}

static std::string esc(const std::string& s) {
    std::string r; for (char c : s) { if (c == '\\') r += "\\\\"; else if (c == '\n') r += "\\n"; else if (c == '\t') r += "\\t"; else if (c != '\r') r.push_back(c); } return r;
}
static std::string unesc(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { char n = s[++i]; r.push_back(n == 'n' ? '\n' : n == 't' ? '\t' : n); } else r.push_back(s[i]);
    }
    return r;
}
static std::vector<std::string> splitTab(const std::string& s) {
    std::vector<std::string> v; std::string cur;
    for (char c : s) { if (c == '\t') { v.push_back(cur); cur.clear(); } else cur.push_back(c); }
    v.push_back(cur); return v;
}

// Format: "R<TAB>id<TAB>name<TAB>area<TAB>terrain<TAB>note<TAB>x<TAB>y<TAB>z<TAB>placed<TAB>desc<TAB>visits" (last two optional when reading) then "E<TAB>from<TAB>dir<TAB>to"
std::string Mapper::serialize() const {
    std::ostringstream o;
    o << "C\t" << esc(current) << "\t" << nextManual_ << "\n";
    for (auto& kv : rooms) {
        const Room& r = kv.second;
        o << "R\t" << esc(r.id) << "\t" << esc(r.name) << "\t" << esc(r.area) << "\t" << esc(r.terrain) << "\t" << esc(r.note)
          << "\t" << r.x << "\t" << r.y << "\t" << r.z << "\t" << r.placed << "\t" << esc(r.desc) << "\t" << r.visits << "\n";
        for (auto& e : r.exits) o << "E\t" << esc(r.id) << "\t" << esc(e.first) << "\t" << esc(e.second) << "\n";
    }
    return o.str();
}

void Mapper::deserialize(const std::string& text) {
    rooms.clear(); current.clear(); nextManual_ = 1;
    std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        auto f = splitTab(line);
        if (f.empty()) continue;
        if (f[0] == "C" && f.size() >= 3) { current = unesc(f[1]); nextManual_ = std::max(1, atoi(f[2].c_str())); }
        else if (f[0] == "R" && f.size() >= 10) {
            Room r; r.id = unesc(f[1]); r.name = unesc(f[2]); r.area = unesc(f[3]); r.terrain = unesc(f[4]); r.note = unesc(f[5]);
            r.x = atoi(f[6].c_str()); r.y = atoi(f[7].c_str()); r.z = atoi(f[8].c_str()); r.placed = f[9] == "1";
            if (f.size() >= 11) r.desc = unesc(f[10]);
            if (f.size() >= 12) r.visits = atoi(f[11].c_str());
            rooms[r.id] = r;
        } else if (f[0] == "E" && f.size() >= 4) {
            auto it = rooms.find(unesc(f[1]));
            if (it != rooms.end()) it->second.exits[unesc(f[2])] = unesc(f[3]);
        }
    }
}

} // namespace mc
