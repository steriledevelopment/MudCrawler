// Triggers, aliases, timers and variables.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <memory>

namespace mc {

enum class MatchType { Glob, Regex, Substring };

struct Trigger {
    std::string name, pattern, commands;
    MatchType type = MatchType::Glob;
    bool enabled = true, gag = false, once = false, stop = false;
    int fg = -1;                 // highlight colour (packed like ansi.h), -1 = none
    std::string pane = "main";   // "main" or "chat"
};
struct Alias {
    std::string name, pattern, commands;
    MatchType type = MatchType::Glob;
    bool enabled = true;
};
struct Timer {
    std::string name, commands;
    double seconds = 60;
    bool repeat = true, enabled = true;
    double elapsed = 0;
};

struct LineResult {
    bool gag = false;
    int fg = -1;
    std::string pane = "main";
    std::vector<std::string> commands;   // already expanded, unsplit (may contain ';' and '#' commands)
};

struct Action { bool client; std::string text; };   // client: '#...' command handled by the app

std::string globToRegex(const std::string& glob);
std::string escapeRegex(const std::string& s);

class RuleEngine {
public:
    std::vector<Trigger> triggers;
    std::vector<Alias> aliases;
    std::vector<Timer> timers;
    std::map<std::string, std::string> vars;
    char separator = ';';

    std::string lastError;   // set when a pattern fails to compile

    LineResult matchLine(const std::string& plain);
    // Expands aliases, splits on separator, handles "#N cmd" repeats. Never returns empty-text actions.
    std::vector<Action> processInput(const std::string& input, int depth = 0);
    // Advances timers; returns commands to execute (expanded).
    std::vector<std::string> tick(double dt);

    std::string expand(const std::string& tmpl, const std::vector<std::string>& caps) const;

    std::string serialize() const;
    void deserialize(const std::string& text);

private:
    const std::regex* compiled(MatchType t, const std::string& pattern);
    std::map<std::string, std::unique_ptr<std::regex>> cache_;
    std::map<std::string, bool> bad_;
};

} // namespace mc
