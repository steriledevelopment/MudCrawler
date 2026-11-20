#include "../src/rules.h"
#include <cstdio>
using namespace mc;
static int fails = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)

int main() {
    RuleEngine e;
    e.vars["target"] = "orc";
    Trigger t; t.pattern = "* hits you for * damage"; t.commands = "say ouch $2 from $1"; t.fg = 1;
    e.triggers.push_back(t);
    Trigger g; g.pattern = "^spam\\d+$"; g.type = MatchType::Regex; g.gag = true; g.pane = "chat";
    e.triggers.push_back(g);
    auto r = e.matchLine("The orc hits you for 12 damage");
    CHECK(r.commands.size() == 1 && r.commands[0] == "say ouch 12 from The orc");
    CHECK(r.fg == 1 && !r.gag);
    r = e.matchLine("spam42"); CHECK(r.gag && r.pane == "chat");
    r = e.matchLine("nothing"); CHECK(!r.gag && r.commands.empty());

    Alias a; a.pattern = "k *"; a.commands = "kill $1;look"; e.aliases.push_back(a);
    Alias a2; a2.pattern = "^tt$"; a2.type = MatchType::Regex; a2.commands = "k ${target}"; e.aliases.push_back(a2);
    auto acts = e.processInput("k goblin");
    CHECK(acts.size() == 2 && acts[0].text == "kill goblin" && acts[1].text == "look");
    acts = e.processInput("tt");           // alias chaining
    CHECK(acts.size() == 2 && acts[0].text == "kill orc");
    acts = e.processInput("#3 n;s");
    CHECK(acts.size() == 4 && acts[0].text == "n" && acts[2].text == "n" && acts[3].text == "s");
    acts = e.processInput("#set x a;b");
    CHECK(acts.size() == 2 && acts[0].client && acts[0].text == "#set x a");
    acts = e.processInput("");
    CHECK(acts.size() == 1 && !acts[0].client && acts[0].text.empty());

    e.timers.push_back({"t", "tick", 1.0, false, true, 0});
    CHECK(e.tick(0.5).empty()); CHECK(e.tick(0.6).size() == 1); CHECK(e.tick(5).empty());

    Trigger bad; bad.pattern = "(["; bad.type = MatchType::Regex; e.triggers.push_back(bad);
    e.matchLine("x"); CHECK(!e.lastError.empty());

    std::string s = e.serialize(); RuleEngine f; f.deserialize(s);
    CHECK(f.triggers.size() == 3 && f.aliases.size() == 2 && f.timers.size() == 1 && f.vars["target"] == "orc");
    CHECK(f.triggers[0].commands == "say ouch $2 from $1" && f.triggers[1].gag && f.triggers[1].pane == "chat");
    printf(fails ? "%d FAILURES\n" : "rules tests OK\n", fails); return fails ? 1 : 0;
}
