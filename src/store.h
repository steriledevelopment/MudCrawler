// Profiles, settings and file locations.
#pragma once
#include <string>
#include <vector>

namespace mc {

struct Profile {
    std::string id, name, host, userPrompt = "name:", passPrompt = "password:", user, passEnc, postLogin;
    int port = 4000;
    bool tls = false, autoLogin = false, autoReconnect = false;
    std::string password() const;            // decrypts passEnc
    void setPassword(const std::string& p);  // encrypts into passEnc
};

struct Settings {
    std::string theme = "Stone & Gold", fontName = "Consolas";
    int fontSize = 11, scrollback = 5000, walkDelayMs = 200;
    bool numpad = true, echoInput = true, logging = false, showMap = false, showChat = true;
    std::string blockedPattern = "can't go that way|cannot go that way|no exit|alas, you cannot|there is no way|you can't go";
    std::string lastProfile;
    bool sidebarCollapsed = false;          // profile list folded into a thin rail
    bool mapPopped = false;                 // map shown in its own window
    int mapX = -1, mapY = -1, mapW = 420, mapH = 520;
    std::string panelState;                 // pop-out state of the Menu / Settings panels ("menu=1,x,y,w,h;settings=0,...")
};

std::string dataDir();                        // created on demand
std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& data);   // atomic (tmp + rename)
std::string safeName(const std::string& s);

std::vector<Profile> loadProfiles();
void saveProfiles(const std::vector<Profile>& p);
Settings loadSettings();
void saveSettings(const Settings& s);
std::string newId();

} // namespace mc
