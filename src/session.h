// One MUD connection: socket thread, telnet, ANSI, rules, mapper, GMCP.
#pragma once
#include <winsock2.h>
#include <windows.h>
#include <zlib.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <fstream>
#include <regex>
#include "ansi.h"
#include "json.h"
#include "mapper.h"
#include "rules.h"
#include "store.h"
#include "telnet.h"

namespace mc {

enum : UINT { WM_NET_CONNECTED = WM_APP + 1, WM_NET_DATA, WM_NET_CLOSED };

struct NetMsg { unsigned gen; std::string data; };   // heap-allocated, owned by receiver

struct Vitals {
    double hp = -1, maxhp = -1, mp = -1, maxmp = -1, mv = -1, maxmv = -1;
    bool any() const { return hp >= 0 || mp >= 0 || mv >= 0; }
};

class Session;
struct UiHooks {
    // partial=true: transient prompt text (replaced by the next call / commit)
    std::function<void(Session*, const std::vector<Run>&, const std::string& pane, bool partial)> line;
    std::function<void(Session*)> status;            // connection/room/vitals changed
    std::function<void(Session*, bool)> passwordMode;
    std::function<void(Session*)> clear;
};

class Session {
public:
    enum State { Disconnected, Connecting, Connected };

    Session(int id, HWND notify, const Profile& p, const Settings* s, UiHooks* hooks);
    ~Session();

    int id;
    Profile profile;
    const Settings* settings;
    RuleEngine rules;
    Mapper mapper;
    State state = Disconnected;
    Vitals vitals;
    std::map<std::string, JVal> gmcp;     // lower-cased package -> last payload
    unsigned mapRev = 0;                  // bumped whenever the map data changes (so the UI repaints only when needed)
    bool passwordMode = false;
    bool gmcpRoom = false;                // MUD supplies Room.Info (GMCP mapping active)
    unsigned long long bytesIn = 0;
    bool mccp = false;
    std::string statusText;               // last connection message

    void connect();
    void disconnect();
    void userInput(const std::string& text);     // text typed by the user (aliases, ';', '#')
    void sendCommand(const std::string& text);   // raw command to the MUD (with tracking/echo)
    void onNetMessage(UINT msg, LPARAM l);
    void tick(double dt);
    void setSize(int cols, int rows);
    void say(const std::string& text, int fg = 10);   // local client message
    void saveAll();
    void clientCommand(const std::string& cmd);
    std::string roomName() const;
    void touchMap() { mapDirty_ = true; mapSaveAt_ = now_ + 2; mapRev++; }   // call after editing a room (note, name)

private:
    void feedBytes(const uint8_t* p, size_t n);
    void feedTelnet(const uint8_t* p, size_t n);
    void handleEvent(const TelnetEvent& e);
    void handleNeg(uint8_t cmd, uint8_t opt);
    void handleSub(uint8_t opt, const std::string& data);
    void handleText(const std::string& s);
    void processLine(const std::string& raw, bool isPrompt = false);
    void startCapture(int kind);
    void finalizeCapture(bool force);
    void flushPartial(bool commit);
    void onGmcp(const std::string& pkg, const std::string& json);
    void onMsdp(const std::string& k, const std::string& v);
    void autoLogin(const std::string& plain);
    void execAction(const Action& a);
    void execCommands(const std::vector<std::string>& cmds);
    void rawSend(const std::string& s);
    void startMccp(const uint8_t* p, size_t n);
    void stopMccp();
    void logLine(const std::string& plain);
    void scheduleWalk(const std::vector<std::string>& path);
    void emitLine(const std::vector<Run>& runs, const std::string& pane, bool partial);
    void touchStatus() { if (hooks_ && hooks_->status) hooks_->status(this); }

    HWND notify_;
    UiHooks* hooks_;
    SOCKET sock_ = INVALID_SOCKET;
    unsigned gen_ = 0;
    TelnetParser parser_;
    z_stream zs_{};
    bool zActive_ = false;
    std::map<uint8_t, bool> remote_, local_;
    int cols_ = 80, rows_ = 24, ttypeIdx_ = 0;
    std::string carry_;       // incomplete UTF-8 bytes
    std::string lineBuf_;     // text after the last newline
    bool partialShown_ = false;
    std::string shownPartial_;   // text currently shown as the transient prompt line
    bool processing_ = false;    // inside the line-splitting loop (don't commit the buffer from nested sends)
    void commitPartialForSend();
    double lastData_ = 0, now_ = 0;
    AnsiParser ansi_, chatAnsi_;
    std::ofstream log_;
    // auto-login
    int loginStage_ = 0;
    // delayed command queue ("walk" entries can be cancelled)
    struct Delayed { double due; std::string cmd; bool walk; };
    std::vector<Delayed> delayed_;
    // move tracking
    double moveWatchUntil_ = 0;
    std::regex blocked_;
    bool blockedOk_ = false;
    // room description capture: text shown after a move/look is stored on the room
    int capKind_ = 0;                  // 0 none, 1 expect room change (GMCP move), 2 expect same room (look / manual move)
    std::string capRoom_;
    std::vector<std::string> capLines_;
    double capLast_ = 0, capStart_ = 0;
    bool capPending_ = false;
    double reconnectAt_ = -1;
    bool userClosed_ = false;
    bool mapDirty_ = false;
    double mapSaveAt_ = 0;
};

} // namespace mc
