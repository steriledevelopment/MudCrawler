// The Mud Crawler - main window.
#include "ui.h"
#include "privacy.h"
#include <deque>
#include <map>
#include <set>
#include <functional>
#include <algorithm>
using std::min; using std::max;
#include <gdiplus.h>
#include <cmath>
#include <memory>
#include <shellapi.h>

using namespace mc;

namespace {

enum {
    ID_FILE_CONNECT = 1001, ID_FILE_PROFILES, ID_FILE_RECONNECT, ID_FILE_DISCONNECT, ID_FILE_CLOSETAB, ID_FILE_NEWWIN, ID_FILE_EXIT,
    ID_EDIT_CLEAR, ID_EDIT_RULES, ID_EDIT_COPYALL,
    ID_VIEW_MAP, ID_VIEW_CHAT, ID_VIEW_FONT, ID_VIEW_LOG, ID_VIEW_ECHO, ID_VIEW_NUMPAD, ID_VIEW_NEXTTAB, ID_VIEW_PREVTAB, ID_VIEW_POPMAP, ID_MAP_POP, ID_MAP_INFO, ID_MAP_RENAME, ID_MAP_NOTE, ID_SET_SCROLL_UP, ID_SET_SCROLL_DOWN, ID_SET_WALK_UP, ID_SET_WALK_DOWN, ID_OPEN_SETTINGS, ID_MAP_FULL,
    ID_MAP_RECENTER, ID_MAP_WALK, ID_MAP_DELETE, ID_PROF_CONNECT, ID_PROF_EDIT, ID_PROF_DELETE, ID_PROF_NEW, ID_PROF_MANAGE, ID_VIEW_SIDEBAR, ID_ZOOM_IN, ID_ZOOM_OUT, ID_PASSWORD,
    ID_HELP_COMMANDS, ID_HELP_ABOUT,
    ID_THEME_BASE = 1100,   // + index
    ID_SET_SEC_BASE = 1120, // Settings window sections: + index 0..5
    ID_OPEN_DATA = 1130, ID_OPEN_MAIL, ID_OPEN_DONATE, ID_COPY_PRIVACY,
    ID_INPUT = 2001, ID_OUT, ID_CHAT, ID_PROFILE_POPUP_BASE = 3000,
};

struct Line { std::vector<Run> runs; int wlen = 0; };

struct PendingLine { bool chat; std::vector<Run> runs; bool partial; };

struct View {
    std::unique_ptr<Session> s;
    std::vector<PendingLine> pending;      // output waiting for the next batched redraw
    HWND out = nullptr, chat = nullptr;
    std::deque<Line> outLines, chatLines;
    int partialStart = -1;
    std::vector<std::string> history; int histPos = -1; std::string draft;
    std::string inputDraft;
};

struct App {
    HWND wnd = nullptr, input = nullptr, vitals = nullptr, mapwnd = nullptr;
    HMENU mFile = nullptr, mEdit = nullptr, mView = nullptr, mHelp = nullptr;
    int dpi = 96;
    // owner-drawn chrome
    enum HotId { H_PROFILE = 1, H_NEWPROFILE, H_MENU, H_TAB, H_TABCLOSE, H_TABADD, H_SETTINGS, H_MACROS, H_ZOOMOUT, H_ZOOMIN, H_CONN, H_SEND, H_DOCKTAB, H_DOCKTABCLOSE, H_SIDECOLLAPSE, H_SIDEEXPAND };
    struct Hot { RECT r; int id; int arg; };
    std::vector<Hot> hots;
    int hoverId = 0, hoverArg = -1;
    RECT rSidebar{}, rPanel{}, rTabs{}, rOut{}, rBar{}, rInput{}, rInputBox{}, rStatus{};
    int profSel = -1;
    int dockTab = 0;                          // which docked panel is showing: 0 = map, k+1 = panel k
    bool dockFull = false;                    // docked: the dock is expanded over the game text
    RECT rDockStrip{};
    HWND mapPop = nullptr; bool mapPopped = false; int mapBtnHover = 0, mapBtnDown = 0;     // 0 none, 1 pop-out button, 2 expand/fullscreen button
    bool mapFull = false;                    // docked: map expanded over the output area; popped: fullscreen
    LONG_PTR popSavedStyle = 0; RECT popSavedRect{};
    Settings settings;
    std::vector<Profile> profiles;
    std::vector<std::unique_ptr<View>> views;
    int active = -1, nextId = 1;
    UiHooks hooks;
    HFONT mono = nullptr, inputFont = nullptr, promptFont = nullptr;
    int inputTextH = 18, inputCharW = 8;
    HBRUSH inputBrush = nullptr;
    int charW = 8, charH = 16;
    ULONGLONG lastTick = 0;
    HACCEL accel = nullptr;
    // map view state
    int mapCell = 34, panX = 0, panY = 0, zoff = 0;
    std::string lastMapRoom;
    struct Hit { RECT r; std::string id; };
    // selected room + its info card on the map
    std::string selRoom;
    int descScroll = 0, descMax = 0, cardDown = 0;
    RECT cardRect{};
    struct CardHit { RECT r; int id; };
    std::vector<CardHit> cardHits;
    std::vector<Hit> hits;
    std::string hover;
    bool swallowChar = false;
} A;

// Tool panels (Menu, Settings): docked as tabs beside the map, or popped out into their own window.
enum PanelId { PN_SETTINGS = 0, PN_MACROS = 1, PN_PROFILES = 2, PN_COUNT = 3 };
enum { PC_CLOSE = -1, PC_POP = -2, PC_EXPAND = -3 };      // header button commands
struct Panel {
    const wchar_t* title; const char* key;
    HWND wnd = nullptr; HWND formWnd = nullptr; bool open = false, popped = false, full = false;
    HWND pop = nullptr; LONG_PTR savedStyle = 0; RECT savedRect{};
    int x = -1, y = -1, w = 400, h = 660;
    int scroll = 0, contentH = 0, hover = -1, down = -1;
    bool wantPop = false;                                 // restore as popped-out at startup
    struct PH { RECT r; int cmd; int arg; };
    std::vector<PH> hots;
};
Panel gPanels[PN_COUNT] = {{L"Settings", "settings"}, {L"Macros", "macros"}, {L"Profiles", "profiles"}};
bool isFormPanel(int k) { return k == PN_MACROS || k == PN_PROFILES; }
bool isWindowOnly(int k) { return k == PN_SETTINGS || k == PN_MACROS; }      // these live in their own window and never dock

const wchar_t* kMapClass = L"McMap";
const wchar_t* kVitalsClass = L"McVitals";
const int kMapWidth = 300;
int px(int v) { return MulDiv(v, A.dpi, 96); }

const Theme& theme() { return findTheme(A.settings.theme); }
View* activeView() { return A.active >= 0 && A.active < (int)A.views.size() ? A.views[A.active].get() : nullptr; }
View* viewFor(Session* s) { for (auto& v : A.views) if (v->s.get() == s) return v.get(); return nullptr; }
View* viewById(int id) { for (auto& v : A.views) if (v->s->id == id) return v.get(); return nullptr; }

// ---------------------------------------------------------------------------
// Rich edit output
// ---------------------------------------------------------------------------
struct RoGuard {
    HWND h; CHARRANGE sel{}; bool keepSel = false;
    explicit RoGuard(HWND hh, bool keep = true) : h(hh) {
        SendMessageW(h, EM_SETREADONLY, FALSE, 0);
        if (keep) { SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&sel); keepSel = sel.cpMin != sel.cpMax; }
    }
    ~RoGuard() {
        if (keepSel) SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&sel);
        SendMessageW(h, EM_SETREADONLY, TRUE, 0);
    }
};

int textLen(HWND re) {
    GETTEXTLENGTHEX g{GTL_NUMCHARS, 1200};
    return (int)SendMessageW(re, EM_GETTEXTLENGTHEX, (WPARAM)&g, 0);
}
bool atBottom(HWND re) {
    SCROLLINFO si{sizeof si, SIF_ALL};
    if (!GetScrollInfo(re, SB_VERT, &si)) return true;
    return si.nPos + (int)si.nPage >= si.nMax - 2;
}
void scrollBottom(HWND re) { SendMessageW(re, WM_VSCROLL, SB_BOTTOM, 0); }

// Appends runs at the end (the caller holds a RoGuard). Returns the number of characters inserted.
int renderRuns(HWND re, const std::vector<Run>& runs, bool newline) {
    const Theme& t = theme();
    SendMessageW(re, EM_SETSEL, -1, -1);
    int total = 0;
    CHARFORMAT2W cf{}; cf.cbSize = sizeof cf;
    auto setFmt = [&](const Style& st) {
        cf.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_BOLD | CFM_FACE | CFM_SIZE | CFM_CHARSET;
        cf.dwEffects = (st.italic ? CFE_ITALIC : 0) | (st.underline ? CFE_UNDERLINE : 0) | (st.strike ? CFE_STRIKEOUT : 0);
        COLORREF fg = resolveColor(st.fg, t, true, st.bold), bg = resolveColor(st.bg, t, false, false);
        if (st.inverse) std::swap(fg, bg);
        if (st.dim && !st.inverse) fg = RGB((GetRValue(fg) + GetRValue(bg)) / 2, (GetGValue(fg) + GetGValue(bg)) / 2, (GetBValue(fg) + GetBValue(bg)) / 2);
        cf.crTextColor = fg; cf.crBackColor = bg;
        cf.yHeight = A.settings.fontSize * 20; cf.bCharSet = DEFAULT_CHARSET;
        wcsncpy(cf.szFaceName, W(A.settings.fontName).c_str(), LF_FACESIZE - 1);
        SendMessageW(re, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    };
    for (auto& r : runs) {
        setFmt(r.style);
        std::wstring w = W(r.text);
        SendMessageW(re, EM_REPLACESEL, FALSE, (LPARAM)w.c_str());
        total += (int)w.size();
    }
    if (newline) {
        if (runs.empty()) setFmt(Style()); // blank line still needs a format
        SendMessageW(re, EM_REPLACESEL, FALSE, (LPARAM)L"\r");
        total += 1;
    }
    return total;
}

void layout();
void applyRichBase(HWND re) {
    SendMessageW(re, EM_SETBKGNDCOLOR, 0, theme().bg);
}

// WM_SETREDRAW(TRUE) on a hidden window makes Windows show it, so only suspend drawing for windows that are visible.
struct NoRedraw {
    HWND h; bool vis;
    explicit NoRedraw(HWND w) : h(w), vis(IsWindowVisible(w) != 0) { if (vis) SendMessageW(h, WM_SETREDRAW, FALSE, 0); }
    ~NoRedraw() { if (vis) { SendMessageW(h, WM_SETREDRAW, TRUE, 0); RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW); } }
};

void removePartialNG(View& v) {            // caller holds a RoGuard on v.out
    if (v.partialStart < 0) return;
    SendMessageW(v.out, EM_SETSEL, v.partialStart, -1);
    SendMessageW(v.out, EM_REPLACESEL, FALSE, (LPARAM)L"");
    v.partialStart = -1;
}

void trimView(View& v, HWND re, std::deque<Line>& lines, bool isOut) {
    size_t cap = (size_t)A.settings.scrollback;
    if (lines.size() <= cap + 300) return;
    int chars = 0; size_t n = lines.size() - cap;
    for (size_t i = 0; i < n; i++) chars += lines[i].wlen;
    lines.erase(lines.begin(), lines.begin() + n);
    RoGuard g(re, false);
    SendMessageW(re, EM_SETSEL, 0, chars);
    SendMessageW(re, EM_REPLACESEL, FALSE, (LPARAM)L"");
    if (isOut && v.partialStart >= 0) v.partialStart = std::max(0, v.partialStart - chars);
}

// Output from the network is queued and drawn in batches (every ~30 ms) with redraw switched off while the
// text is inserted. That removes the flashing and the per-line cost that made typing lag during bursts.
void flushView(View& v) {
    if (v.pending.empty()) return;
    std::vector<PendingLine> q; q.swap(v.pending);
    int lastMain = -1;
    for (int i = 0; i < (int)q.size(); i++) if (!q[i].chat) lastMain = i;
    std::vector<PendingLine> mainQ, chatQ;
    for (int i = 0; i < (int)q.size(); i++) {
        if (q[i].chat) chatQ.push_back(std::move(q[i]));
        else if (!q[i].partial || i == lastMain) mainQ.push_back(std::move(q[i]));   // superseded prompt drafts are dropped
    }
    bool revealChat = false;
    auto run = [&](HWND re, std::vector<PendingLine>& items, bool chat) {
        if (items.empty()) return;
        auto& lines = chat ? v.chatLines : v.outLines;
        bool wasEmpty = lines.empty();
        bool bottom = atBottom(re);
        NoRedraw nr(re);
        {
            RoGuard g(re);
            if (!chat) removePartialNG(v);
            for (auto& it : items) {
                if (it.partial) { v.partialStart = textLen(re); renderRuns(re, it.runs, false); }
                else { Line ln; ln.runs = std::move(it.runs); ln.wlen = renderRuns(re, ln.runs, true); lines.push_back(std::move(ln)); }
            }
        }
        if (chat && wasEmpty && !lines.empty()) revealChat = true;
        trimView(v, re, lines, !chat);
        if (bottom) scrollBottom(re);
    };
    run(v.out, mainQ, false);
    run(v.chat, chatQ, true);
    if (revealChat && &v == activeView()) layout();   // the chat pane appears once there is chat
}

void queueLine(View& v, bool chat, const std::vector<Run>& runs, bool partial) {
    v.pending.push_back({chat, runs, partial});
    if (v.pending.size() > 3000) flushView(v);       // never let a huge burst pile up
}

void rebuildView(View& v) {
    flushView(v);
    for (int pane = 0; pane < 2; pane++) {
        HWND re = pane ? v.chat : v.out;
        auto& lines = pane ? v.chatLines : v.outLines;
        applyRichBase(re);
        {
            NoRedraw nr(re);
            { RoGuard g(re, false); SendMessageW(re, EM_SETSEL, 0, -1); SendMessageW(re, EM_REPLACESEL, FALSE, (LPARAM)L"");
              for (auto& l : lines) l.wlen = renderRuns(re, l.runs, true); }
            scrollBottom(re);
        }
        if (IsWindowVisible(re)) InvalidateRect(re, nullptr, TRUE);
    }
    v.partialStart = -1;
}

LRESULT CALLBACK richSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_MOUSEWHEEL) { POINT p{(short)LOWORD(l), (short)HIWORD(l)}; if (WindowFromPoint(p) != h) return SendMessageW(A.wnd, WM_MOUSEWHEEL, w, l); }
    // Typing while the output has focus goes to the input line.
    if (m == WM_CHAR && w >= 32 && A.input) { SetFocus(A.input); SendMessageW(A.input, WM_CHAR, w, l); return 0; }
    if (m == WM_KEYDOWN && (w == VK_RETURN) && A.input) { SetFocus(A.input); return 0; }
    return DefSubclassProc(h, m, w, l);
}

void darkScroll(HWND h) {
    using Fn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static Fn fn = [] { HMODULE m = LoadLibraryW(L"uxtheme.dll"); return m ? (Fn)GetProcAddress(m, "SetWindowTheme") : nullptr; }();
    if (fn) fn(h, theme().light() ? L"Explorer" : L"DarkMode_Explorer", nullptr);
}

HWND makeRich(HWND parent, int id) {
    HWND re = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL | WS_CLIPSIBLINGS,
                              0, 0, 100, 100, parent, (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
    SendMessageW(re, EM_SETUNDOLIMIT, 0, 0);
    SendMessageW(re, EM_SETLANGOPTIONS, 0, SendMessageW(re, EM_GETLANGOPTIONS, 0, 0) & ~IMF_AUTOFONT);
    SendMessageW(re, EM_SETBKGNDCOLOR, 0, theme().bg);
    SendMessageW(re, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
    SetWindowSubclass(re, richSub, 1, 0);
    darkScroll(re);
    return re;
}

// ---------------------------------------------------------------------------
// Fonts / layout
// ---------------------------------------------------------------------------
void rebuildFont() {
    if (A.mono) DeleteObject(A.mono);
    HDC dc = GetDC(nullptr);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    A.mono = CreateFontW(-MulDiv(A.settings.fontSize, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                         W(A.settings.fontName).c_str());
    HGDIOBJ old = SelectObject(dc, A.mono);
    TEXTMETRICW tm{}; GetTextMetricsW(dc, &tm);
    A.charW = std::max<int>(4, tm.tmAveCharWidth); A.charH = std::max<int>(8, tm.tmHeight);
    SelectObject(dc, old); ReleaseDC(nullptr, dc);
    // dedicated, slightly larger font for the input line
    if (A.inputFont) DeleteObject(A.inputFont);
    A.inputFont = CreateFontW(-MulDiv(A.settings.fontSize + 2, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                              W(A.settings.fontName).c_str());
    if (A.promptFont) DeleteObject(A.promptFont);
    A.promptFont = CreateFontW(-MulDiv(A.settings.fontSize + 4, dpi, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                               W(A.settings.fontName).c_str());
    { HDC d2 = GetDC(nullptr); HGDIOBJ o2 = SelectObject(d2, A.inputFont); TEXTMETRICW t2{}; GetTextMetricsW(d2, &t2);
      A.inputTextH = t2.tmHeight; A.inputCharW = t2.tmAveCharWidth; SelectObject(d2, o2); ReleaseDC(nullptr, d2); }
    if (A.input) SendMessageW(A.input, WM_SETFONT, (WPARAM)A.inputFont, TRUE);
}

void reportSize() {
    View* v = activeView(); if (!v) return;
    RECT r; GetClientRect(v->out, &r);
    v->s->setSize(std::max(20, (int)((r.right - r.left - 24) / A.charW)), std::max(5, (int)((r.bottom - r.top) / A.charH)));
}

// ---------------------------------------------------------------------------
// Chrome: fonts, layout and painting (owner-drawn, to match the Stone & Gold design)
// ---------------------------------------------------------------------------
HFONT gBody = nullptr, gTitle = nullptr, gBold = nullptr, gHead = nullptr;
void buildChromeFonts() {
    auto mk = [](int pt, int weight) {
        return CreateFontW(-MulDiv(pt, A.dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    };
    gBody = mk(10, FW_NORMAL); gTitle = mk(15, FW_SEMIBOLD); gBold = mk(10, FW_SEMIBOLD); gHead = mk(12, FW_SEMIBOLD);
}
int textW(HFONT f, const std::wstring& t) {
    HDC dc = GetDC(A.wnd); HGDIOBJ o = SelectObject(dc, f); SIZE sz{}; GetTextExtentPoint32W(dc, t.c_str(), (int)t.size(), &sz); SelectObject(dc, o); ReleaseDC(A.wnd, dc);
    return sz.cx;
}

enum { STA_NONE = 0, STA_CONNECTING, STA_CONNECTED, STA_DOWN };
int profileState(const Profile& p) {
    int best = STA_NONE;
    for (auto& v : A.views) if (v->s->profile.id == p.id) {
        auto st = v->s->state;
        int x = st == Session::Connected ? STA_CONNECTED : st == Session::Connecting ? STA_CONNECTING : STA_DOWN;
        if (best == STA_NONE || x == STA_CONNECTED || (x == STA_CONNECTING && best != STA_CONNECTED)) best = x;
    }
    return best;
}
int viewState(const View& v) {
    return v.s->state == Session::Connected ? STA_CONNECTED : v.s->state == Session::Connecting ? STA_CONNECTING : STA_DOWN;
}
COLORREF stateColor(int st, const Theme& t) {
    switch (st) { case STA_CONNECTED: return RGB(0x4c, 0xd9, 0x5c); case STA_CONNECTING: return RGB(0xe8, 0xb0, 0x30); case STA_DOWN: return RGB(0xe5, 0x5a, 0x5a); }
    return t.muted;
}

void layout() {
    if (!A.wnd || !gBody) return;
    RECT rc; GetClientRect(A.wnd, &rc);
    int Wd = rc.right, Ht = rc.bottom, M = px(10);
    if (Wd < 50 || Ht < 50) return;
    View* av = activeView();
    bool sideCollapsed = A.settings.sidebarCollapsed;
    A.rSidebar = {M, M, M + (sideCollapsed ? px(46) : px(250)), Ht - M};
    A.rPanel = {A.rSidebar.right + M, M, Wd - M, Ht - M};
    int tabH = px(46), inputH = px(62), barH = px(54);
    bool hasVit = av && av->s->vitals.any(); int vitH = hasVit ? px(24) : 0;
    A.rTabs = {A.rPanel.left, A.rPanel.top, A.rPanel.right, A.rPanel.top + tabH};
    A.rInput = {A.rPanel.left, A.rPanel.bottom - inputH, A.rPanel.right, A.rPanel.bottom};
    A.rBar = {A.rPanel.left, A.rInput.top - barH, A.rPanel.right, A.rInput.top};
    int vitTop = A.rBar.top - vitH;
    A.rOut = {A.rPanel.left + 1, A.rTabs.bottom, A.rPanel.right - 1, vitTop};

    A.hots.clear();
    auto hot = [&](RECT r, int id, int arg = -1) { A.hots.push_back({r, id, arg}); };
    // sidebar
    if (sideCollapsed) {   // thin rail: menu + expand arrow
        hot({A.rSidebar.left + px(6), A.rSidebar.top + px(14), A.rSidebar.right - px(6), A.rSidebar.top + px(48)}, A.H_SIDEEXPAND);
    } else {
        hot({A.rSidebar.right - px(48), A.rSidebar.top + px(16), A.rSidebar.right - px(14), A.rSidebar.top + px(50)}, A.H_SIDECOLLAPSE);
    }
    int y = A.rSidebar.top + px(66), rowH = px(46), limit = A.rSidebar.bottom - px(16);
    for (size_t i = 0; i < A.profiles.size() && !sideCollapsed; i++, y += rowH) {
        if (y + rowH > limit) break;
        hot({A.rSidebar.left + px(10), y, A.rSidebar.right - px(10), y + rowH - px(4)}, A.H_PROFILE, (int)i);
    }
    // tabs
    int x = A.rTabs.left + px(14), top = A.rTabs.top + px(12), avail = A.rTabs.right - px(20) - x;
    int ntab = (int)A.views.size();
    int wantTotal = 0; std::vector<int> ws;
    for (auto& v : A.views) { int w = std::min(px(240), textW(gBody, W(v->s->profile.name)) + px(84)); ws.push_back(w); wantTotal += w + px(4); }
    double sc = wantTotal > avail && wantTotal > 0 ? (double)avail / wantTotal : 1.0;
    for (int i = 0; i < ntab; i++) {
        int w = std::max(px(90), (int)(ws[i] * sc));
        RECT r = {x, top, x + w, A.rTabs.bottom};
        hot({r.right - px(34), r.top + px(6), r.right - px(8), r.top + px(32)}, A.H_TABCLOSE, i);
        hot(r, A.H_TAB, i);
        x += w + px(4);
    }
    // action bar
    int by = A.rBar.top + (barH - px(36)) / 2, bx = A.rBar.left + px(16);
    struct Btn { const wchar_t* t; int id; } btns[] = {{L"⚙  Settings", A.H_SETTINGS}, {L"✎  Macros", A.H_MACROS}};
    for (auto& b : btns) { int w = textW(gBody, b.t) + px(34); hot({bx, by, bx + w, by + px(36)}, b.id); bx += w + px(8); }
    int rx = A.rBar.right - px(16);
    int cw = px(118); hot({rx - cw, by, rx, by + px(36)}, A.H_CONN); rx -= cw + px(14);
    int zw = px(122); hot({rx - zw, by, rx - zw + px(34), by + px(36)}, A.H_ZOOMOUT); hot({rx - px(34), by, rx, by + px(36)}, A.H_ZOOMIN);
    A.rStatus = {bx + px(10), by, rx - zw - px(14), by + px(36)};
    // input row
    int ih = px(42), iy = A.rInput.top + (inputH - ih) / 2, sendW = px(100);
    A.rInputBox = {A.rInput.left + px(16), iy, A.rInput.right - px(16) - sendW - px(12), iy + ih};
    hot({A.rInputBox.right + px(12), iy, A.rInput.right - px(16), iy + ih}, A.H_SEND);

    // dock: the map and any docked panels share the right-hand area as tabs
    std::vector<int> docked;                         // 0 = map, k+1 = panel k
    bool mapDocked = A.settings.showMap && !A.mapPopped;
    if (mapDocked) docked.push_back(0);
    for (int k = 0; k < PN_COUNT; k++) if (gPanels[k].open && !gPanels[k].popped) docked.push_back(k + 1);
    if (!docked.empty() && std::find(docked.begin(), docked.end(), A.dockTab) == docked.end()) A.dockTab = docked[0];
    bool hasDock = !docked.empty();
    if (!hasDock) A.dockFull = false;
    bool expanded = A.dockFull && hasDock;
    int gap = px(2);
    int dockW = hasDock ? std::min<int>(px(340), (int)(A.rOut.right - A.rOut.left) / 2) : 0;
    RECT dockR = expanded ? A.rOut : RECT{A.rOut.right - dockW, A.rOut.top, A.rOut.right, A.rOut.bottom};
    int stripH = docked.size() >= 2 ? px(38) : 0;
    A.rDockStrip = {dockR.left, dockR.top, dockR.right, dockR.top + stripH};
    RECT content = {dockR.left, dockR.top + stripH, dockR.right, dockR.bottom};
    int outRight = A.rOut.right - ((hasDock && !expanded) ? dockW + gap : 0);
    if (stripH) {
        int tx = dockR.left + px(8);
        for (int id : docked) {
            std::wstring name = id == 0 ? L"Map" : gPanels[id - 1].title;
            int w = textW(gBody, name) + px(34) + (id ? px(22) : 0);
            RECT tr = {tx, dockR.top + px(4), tx + w, dockR.top + stripH};
            if (id) hot({tr.right - px(28), tr.top + px(6), tr.right - px(8), tr.top + px(26)}, A.H_DOCKTABCLOSE, id);
            hot(tr, A.H_DOCKTAB, id);
            tx += w + px(4);
        }
    }
    if (!A.mapPopped) {
        bool show = hasDock && mapDocked && A.dockTab == 0;
        ShowWindow(A.mapwnd, show ? SW_SHOW : SW_HIDE);
        if (show) MoveWindow(A.mapwnd, content.left, content.top, content.right - content.left, content.bottom - content.top, TRUE);
    }
    for (int k = 0; k < PN_COUNT; k++) {
        Panel& p = gPanels[k]; if (!p.wnd || p.popped) continue;
        bool show = p.open && hasDock && A.dockTab == k + 1;
        ShowWindow(p.wnd, show ? SW_SHOW : SW_HIDE);
        if (show) MoveWindow(p.wnd, content.left, content.top, content.right - content.left, content.bottom - content.top, TRUE);
    }
    int bodyH = std::max(30, (int)(A.rOut.bottom - A.rOut.top));
    for (size_t i = 0; i < A.views.size(); i++) {
        View& v = *A.views[i]; bool act = (int)i == A.active;
        int chatW = (A.settings.showChat && !v.chatLines.empty()) ? std::max<int>(180, (int)(outRight - A.rOut.left) * 30 / 100) : 0;
        ShowWindow(v.out, act && !expanded ? SW_SHOW : SW_HIDE);
        ShowWindow(v.chat, act && chatW && !expanded ? SW_SHOW : SW_HIDE);
        if (!act || expanded) continue;
        MoveWindow(v.out, A.rOut.left, A.rOut.top, outRight - A.rOut.left - (chatW ? chatW + gap : 0), bodyH, TRUE);
        if (chatW) MoveWindow(v.chat, outRight - chatW, A.rOut.top, chatW, bodyH, TRUE);
    }
    ShowWindow(A.vitals, hasVit ? SW_SHOW : SW_HIDE);
    if (hasVit) MoveWindow(A.vitals, A.rPanel.left + px(12), vitTop + px(2), A.rPanel.right - A.rPanel.left - px(24), vitH - px(4), TRUE);
    int eh = A.inputTextH;   // exactly one text line, centred in the box
    MoveWindow(A.input, A.rInputBox.left + px(38), A.rInputBox.top + (ih - eh) / 2 + px(2), A.rInputBox.right - A.rInputBox.left - px(38) - px(14), eh, TRUE);
    reportSize();
    InvalidateRect(A.wnd, nullptr, FALSE);
}

void updateStatus() { InvalidateRect(A.wnd, &A.rBar, FALSE); }
void updateTabs() {
    std::wstring title = L"The Mud Crawler";
    if (View* v = activeView()) title = W(v->s->profile.name) + L" - " + title;
    SetWindowTextW(A.wnd, title.c_str());
    InvalidateRect(A.wnd, nullptr, FALSE);
}

// ---- drawing helpers
Gdiplus::Graphics* gG = nullptr; HDC gGdc = nullptr;   // shared GDI+ context for one paint pass
void rrect(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border, int bw = 1) {
    using namespace Gdiplus;
    std::unique_ptr<Graphics> own;
    if (!(gG && gGdc == dc)) { own.reset(new Graphics(dc)); own->SetSmoothingMode(SmoothingModeAntiAlias); own->SetPixelOffsetMode(PixelOffsetModeHalf); }
    Graphics& g = (gG && gGdc == dc) ? *gG : *own;
    float inset = border == (COLORREF)-1 ? 0.f : bw / 2.f;
    float x = r.left + inset, y = r.top + inset, w = (r.right - r.left) - 2 * inset - 1, h = (r.bottom - r.top) - 2 * inset - 1;
    float d = std::min<float>(rad * 2.f, std::min(w, h));
    GraphicsPath path;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    if (fill != (COLORREF)-1) { SolidBrush b(Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill))); g.FillPath(&b, &path); }
    if (border != (COLORREF)-1) { Pen p(Color(255, GetRValue(border), GetGValue(border), GetBValue(border)), (REAL)bw); g.DrawPath(&p, &path); }
}
void dtext(HDC dc, HFONT f, COLORREF c, const std::wstring& t, RECT r, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    HGDIOBJ o = SelectObject(dc, f); SetTextColor(dc, c); DrawTextW(dc, t.c_str(), -1, &r, fmt | DT_NOPREFIX); SelectObject(dc, o);   // '&' is literal text
}
void dot(HDC dc, int cx, int cy, int rad, COLORREF c) {
    using namespace Gdiplus;
    std::unique_ptr<Graphics> own;
    if (!(gG && gGdc == dc)) { own.reset(new Graphics(dc)); own->SetSmoothingMode(SmoothingModeAntiAlias); }
    Graphics& g = (gG && gGdc == dc) ? *gG : *own;
    SolidBrush b(Color(255, GetRValue(c), GetGValue(c), GetBValue(c)));
    g.FillEllipse(&b, (REAL)(cx - rad), (REAL)(cy - rad), (REAL)(rad * 2 + 1), (REAL)(rad * 2 + 1));
}
bool isHover(int id, int arg = -1) { return A.hoverId == id && (arg < 0 || A.hoverArg == arg); }
int gDownId = 0;
COLORREF mix(COLORREF a, COLORREF b, int pa) {
    return RGB((GetRValue(a) * pa + GetRValue(b) * (100 - pa)) / 100, (GetGValue(a) * pa + GetGValue(b) * (100 - pa)) / 100, (GetBValue(a) * pa + GetBValue(b) * (100 - pa)) / 100);
}
void invalidateHot(int id, int arg);
RECT hotRect(int id, int arg = -1) { for (auto& h : A.hots) if (h.id == id && (arg < 0 || h.arg == arg)) return h.r; return RECT{0, 0, 0, 0}; }

void invalidateHot(int id, int arg) {
    for (auto& h : A.hots) if (h.id == id && (arg < 0 || h.arg == arg)) { RECT r = h.r; InflateRect(&r, px(3), px(3)); InvalidateRect(A.wnd, &r, FALSE); return; }
}

void paintChrome(HWND hw) {
    PAINTSTRUCT ps; HDC wdc = BeginPaint(hw, &ps);
    RECT rc; GetClientRect(hw, &rc);
    static HBITMAP sBmp = nullptr; static int sW = 0, sH = 0;      // back buffer reused between paints
    if (!sBmp || sW != rc.right || sH != rc.bottom) { if (sBmp) DeleteObject(sBmp); sBmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); sW = rc.right; sH = rc.bottom; }
    HDC dc = CreateCompatibleDC(wdc); HGDIOBJ obmp = SelectObject(dc, sBmp);
    RECT cp = ps.rcPaint;
    IntersectClipRect(dc, cp.left, cp.top, cp.right, cp.bottom);   // only redraw what is dirty
    Gdiplus::Graphics gfx(dc); gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    gfx.SetClip(Gdiplus::Rect(cp.left, cp.top, cp.right - cp.left, cp.bottom - cp.top));
    gG = &gfx; gGdc = dc;
    const Theme& t = theme();
    SetBkMode(dc, TRANSPARENT);
    HBRUSH wb = CreateSolidBrush(t.win); FillRect(dc, &rc, wb); DeleteObject(wb);
    int R = px(14);
    // ---- sidebar
    rrect(dc, A.rSidebar, R, t.panel, t.border);
    bool sideCollapsed = A.settings.sidebarCollapsed;
    if (!sideCollapsed) dtext(dc, gTitle, t.text, L"Profiles", {A.rSidebar.left + px(24), A.rSidebar.top + px(14), A.rSidebar.right - px(100), A.rSidebar.top + px(54)});
    auto chevron = [&](RECT r, bool right, bool hv) {          // collapse / expand arrow
        int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, d = px(4);
        if (hv) rrect(dc, r, px(8), t.panelSel, -1);
        HPEN pn = CreatePen(PS_SOLID, px(2), hv ? t.accent : t.muted); HGDIOBJ op = SelectObject(dc, pn);
        int s = right ? 1 : -1;
        MoveToEx(dc, cx - s * d, cy - px(7), nullptr); LineTo(dc, cx + s * d, cy); LineTo(dc, cx - s * d, cy + px(7));
        SelectObject(dc, op); DeleteObject(pn);
    };
    if (sideCollapsed) chevron(hotRect(A.H_SIDEEXPAND), true, isHover(A.H_SIDEEXPAND)); else chevron(hotRect(A.H_SIDECOLLAPSE), false, isHover(A.H_SIDECOLLAPSE));
    View* av = activeView();
    for (auto& h : A.hots) if (h.id == A.H_PROFILE) {
        const Profile& p = A.profiles[h.arg];
        bool sel = av && av->s->profile.id == p.id, hv = isHover(A.H_PROFILE, h.arg);
        if (sel || hv) rrect(dc, h.r, px(10), sel ? t.panelSel : mix(t.panelSel, t.panel, 50), -1);
        if (sel) { RECT bar = {h.r.left, h.r.top + px(10), h.r.left + px(3), h.r.bottom - px(10)}; HBRUSH b = CreateSolidBrush(t.accent); FillRect(dc, &bar, b); DeleteObject(b); }
        dot(dc, h.r.left + px(26), (h.r.top + h.r.bottom) / 2, px(6), stateColor(profileState(p), t));
        dtext(dc, gBody, t.text, W(p.name.empty() ? "(unnamed)" : p.name), {h.r.left + px(48), h.r.top, h.r.right - px(10), h.r.bottom});
    }
    if (A.profiles.empty() && !sideCollapsed) dtext(dc, gBody, t.muted, L"No profiles yet.\nRight-click here and choose \"New profile\" to add your first MUD.", {A.rSidebar.left + px(24), A.rSidebar.top + px(70), A.rSidebar.right - px(20), A.rSidebar.top + px(160)}, DT_WORDBREAK);

    // ---- main panel
    rrect(dc, A.rPanel, R, t.panel, t.border);
    // tabs
    for (auto& h : A.hots) {
        if (h.id == A.H_TAB) {
            bool sel = h.arg == A.active, hv = isHover(A.H_TAB, h.arg) || isHover(A.H_TABCLOSE, h.arg);
            const View& v = *A.views[h.arg];
            if (sel) { RECT r = h.r; r.bottom += px(10); rrect(dc, r, px(10), t.bg, -1); RECT sq = {r.left, h.r.bottom - px(2), r.right, h.r.bottom + 2}; (void)sq; }
            dot(dc, h.r.left + px(18), (h.r.top + h.r.bottom) / 2 + 1, px(5), stateColor(viewState(v), t));
            dtext(dc, gBody, sel ? t.text : (hv ? t.text : t.muted), W(v.s->profile.name), {h.r.left + px(34), h.r.top, h.r.right - px(40), h.r.bottom});
        } else if (h.id == A.H_TABCLOSE) {
            bool hv = isHover(A.H_TABCLOSE, h.arg);
            if (hv) rrect(dc, h.r, px(6), t.panelSel, -1);
            dtext(dc, gBody, hv ? t.text : t.muted, L"✕", h.r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (h.id == A.H_DOCKTAB) {
            bool sel = h.arg == A.dockTab, hv = isHover(A.H_DOCKTAB, h.arg) || isHover(A.H_DOCKTABCLOSE, h.arg);
            std::wstring name = h.arg == 0 ? L"Map" : gPanels[h.arg - 1].title;
            if (sel) { rrect(dc, h.r, px(8), t.panelSel, -1); RECT ul = {h.r.left + px(10), h.r.bottom - px(3), h.r.right - px(10), h.r.bottom - px(1)}; HBRUSH b = CreateSolidBrush(t.accent); FillRect(dc, &ul, b); DeleteObject(b); }
            else if (hv) rrect(dc, h.r, px(8), mix(t.panelSel, t.panel, 50), -1);
            dtext(dc, gBody, sel || hv ? t.text : t.muted, name, {h.r.left + px(14), h.r.top, h.r.right - (h.arg ? px(30) : px(8)), h.r.bottom});
        } else if (h.id == A.H_DOCKTABCLOSE) {
            bool hv = isHover(A.H_DOCKTABCLOSE, h.arg);
            if (hv) rrect(dc, h.r, px(5), t.border, -1);
            dtext(dc, gBody, hv ? t.text : t.muted, L"\u2715", h.r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    if (A.views.empty() && !A.dockFull) dtext(dc, gBody, t.muted, L"Pick a profile on the left (or press Ctrl+O) to start playing.", {A.rOut.left + px(30), A.rOut.top + px(30), A.rOut.right - px(30), A.rOut.top + px(80)}, DT_WORDBREAK);
    // action bar
    { HPEN p = CreatePen(PS_SOLID, 1, t.border); HGDIOBJ op = SelectObject(dc, p); MoveToEx(dc, A.rBar.left + px(1), A.rBar.top, nullptr); LineTo(dc, A.rBar.right - px(1), A.rBar.top); SelectObject(dc, op); DeleteObject(p); }
    const wchar_t* labels[] = {L"⚙  Settings", L"✎  Macros"};
    int ids[] = {A.H_SETTINGS, A.H_MACROS};
    for (int i = 0; i < 2; i++) { RECT r = hotRect(ids[i]); bool hv = isHover(ids[i]);
        rrect(dc, r, px(9), hv ? mix(t.panelSel, t.accent, 85) : t.panelSel, hv ? t.accent : t.border);
        dtext(dc, gBody, t.text, labels[i], r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
    { // status
        int st = av ? viewState(*av) : STA_NONE;
        std::wstring txt;
        if (!av) txt = L"No session";
        else {
            Session* s = av->s.get();
            if (st == STA_CONNECTED) txt = L"Connected — " + W(s->profile.host + ":" + std::to_string(s->profile.port));
            else if (st == STA_CONNECTING) txt = L"Connecting…";
            else txt = W(s->statusText.empty() ? "Disconnected" : s->statusText);
        }
        dot(dc, A.rStatus.left + px(4), (A.rStatus.top + A.rStatus.bottom) / 2, px(4), stateColor(st, t));
        dtext(dc, gBody, t.muted, txt, {A.rStatus.left + px(16), A.rStatus.top, A.rStatus.right, A.rStatus.bottom});
    }
    { RECT o = hotRect(A.H_ZOOMOUT), i = hotRect(A.H_ZOOMIN); RECT pill = {o.left, o.top, i.right, o.bottom};
      rrect(dc, pill, px(18), t.bg, t.border);
      dtext(dc, gBody, isHover(A.H_ZOOMOUT) ? t.accent : t.text, L"−", o, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      dtext(dc, gBody, isHover(A.H_ZOOMIN) ? t.accent : t.text, L"+", i, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      dtext(dc, gBody, t.muted, std::to_wstring(A.settings.fontSize * 100 / 11) + L"%", {o.right, o.top, i.left, o.bottom}, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
    { RECT r = hotRect(A.H_CONN); bool hv = isHover(A.H_CONN); int st = av ? viewState(*av) : STA_NONE;
      bool live = st == STA_CONNECTED || st == STA_CONNECTING;
      COLORREF fill = live ? t.dangerFill : t.accentFill, border = live ? mix(RGB(0xd0, 0x50, 0x50), t.dangerFill, 70) : t.accent;
      rrect(dc, r, px(9), hv ? mix(border, fill, 25) : fill, border);
      dtext(dc, gBold, live ? RGB(0xf4, 0xc0, 0xc0) : t.text, live ? L"Disconnect" : L"Connect", r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
    // input row
    rrect(dc, A.rInputBox, px(10), t.inputBg, t.accent, px(2));
    dtext(dc, A.promptFont, t.accent, L">", {A.rInputBox.left + px(10), A.rInputBox.top, A.rInputBox.left + px(34), A.rInputBox.bottom}, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    { RECT r = hotRect(A.H_SEND); bool hv = isHover(A.H_SEND), down = gDownId == A.H_SEND && hv;
      COLORREF fill = down ? mix(t.accent, t.accentFill, 55) : hv ? mix(t.accent, t.accentFill, 30) : t.accentFill;
      rrect(dc, r, px(10), fill, t.accent, px(2));
      dtext(dc, gBold, (hv || down) ? RGB(0x10, 0x0c, 0x06) : t.text, L"Send", r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }

    gG = nullptr; gGdc = nullptr;
    BitBlt(wdc, cp.left, cp.top, cp.right - cp.left, cp.bottom - cp.top, dc, cp.left, cp.top, SRCCOPY);
    SelectObject(dc, obmp); DeleteDC(dc);
    EndPaint(hw, &ps);
}

void setPasswordMode(bool on) { SendMessageW(A.input, EM_SETPASSWORDCHAR, on ? L'●' : 0, 0); InvalidateRect(A.input, nullptr, TRUE); }

void macrosRebind();
void macrosFlush();
void macrosDetach();
void selectTab(int i) {
    if (View* v = activeView()) v->inputDraft = getText(A.input);
    A.active = i;
    A.selRoom.clear();
    macrosRebind();
    A.zoff = 0; A.panX = A.panY = 0;
    layout();
    View* v = activeView();
    setText(A.input, v ? v->inputDraft : "");
    setPasswordMode(v && v->s->passwordMode);
    updateTabs(); updateStatus();
    InvalidateRect(A.mapwnd, nullptr, TRUE); InvalidateRect(A.vitals, nullptr, TRUE);
    if (v) { scrollBottom(v->out); SetFocus(A.input); }
    InvalidateRect(A.wnd, nullptr, FALSE);
}

View* openSession(const Profile& p, bool connectNow = true) {
    // reuse a disconnected tab of the same profile
    for (size_t i = 0; i < A.views.size(); i++)
        if (A.views[i]->s->profile.id == p.id) {
            if (A.views[i]->s->state == Session::Disconnected) { A.views[i]->s->profile = p; selectTab((int)i); if (connectNow) A.views[i]->s->connect(); return A.views[i].get(); }
        }
    auto v = std::make_unique<View>();
    v->out = makeRich(A.wnd, ID_OUT); v->chat = makeRich(A.wnd, ID_CHAT);
    v->s = std::make_unique<Session>(A.nextId++, A.wnd, p, &A.settings, &A.hooks);
    int idx = (int)A.views.size();
    A.views.push_back(std::move(v));
    selectTab(idx);
    View* nv = A.views[idx].get();
    nv->s->say("The Mud Crawler - type #help for commands.", 8);
    A.settings.lastProfile = p.id; saveSettings(A.settings);
    if (connectNow) nv->s->connect();
    return nv;
}

void closeTab(int i) {
    if (i < 0 || i >= (int)A.views.size()) return;
    View* v = A.views[i].get();
    if (v->s->state != Session::Disconnected &&
        MessageBoxW(A.wnd, L"This tab is connected. Close it and disconnect?", L"Close tab", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    macrosDetach();      // the session's rules are about to be destroyed
    DestroyWindow(v->out); DestroyWindow(v->chat);
    A.views.erase(A.views.begin() + i);
    A.active = -1;
    if (!A.views.empty()) selectTab(std::min(i, (int)A.views.size() - 1)); else { macrosRebind(); layout(); updateTabs(); updateStatus(); InvalidateRect(A.mapwnd, nullptr, TRUE); InvalidateRect(A.vitals, nullptr, TRUE); }
}

// ---------------------------------------------------------------------------
// Input line
// ---------------------------------------------------------------------------
void submitInput() {
    View* v = activeView(); if (!v) return;
    std::string text = getText(A.input);
    setText(A.input, "");
    bool pw = v->s->passwordMode;
    if (!pw && !text.empty() && (v->history.empty() || v->history.back() != text)) { v->history.push_back(text); if (v->history.size() > 500) v->history.erase(v->history.begin()); }
    v->histPos = -1;
    v->s->userInput(text);
    scrollBottom(v->out);
}

void historyMove(int dir) {
    View* v = activeView(); if (!v || v->history.empty()) return;
    if (v->histPos < 0) { if (dir > 0) return; v->draft = getText(A.input); v->histPos = (int)v->history.size(); }
    int np = v->histPos + dir;
    if (np < 0) np = 0;
    if (np >= (int)v->history.size()) { v->histPos = -1; setText(A.input, v->draft); }
    else { v->histPos = np; setText(A.input, v->history[np]); }
    SendMessageW(A.input, EM_SETSEL, -1, -1);
}

const char* numpadDir(WPARAM vk) {
    switch (vk) {
    case VK_NUMPAD8: return "n"; case VK_NUMPAD2: return "s"; case VK_NUMPAD4: return "w"; case VK_NUMPAD6: return "e";
    case VK_NUMPAD7: return "nw"; case VK_NUMPAD9: return "ne"; case VK_NUMPAD1: return "sw"; case VK_NUMPAD3: return "se";
    case VK_ADD: return "u"; case VK_SUBTRACT: return "d"; case VK_NUMPAD5: return "look";
    }
    return nullptr;
}

void toggleMapFull();
LRESULT CALLBACK inputSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    View* v = activeView();
    if (m == WM_MOUSEWHEEL) {        // the focused control gets the wheel first; pass it on if the pointer is elsewhere
        POINT p{(short)LOWORD(l), (short)HIWORD(l)};
        if (WindowFromPoint(p) != h) return SendMessageW(A.wnd, WM_MOUSEWHEEL, w, l);
    }
    if (m == WM_CHAR) {
        if (A.swallowChar) { A.swallowChar = false; return 0; }
        if (w == '\r') { submitInput(); return 0; }
        if (w == 27) {
            if (A.dockFull) { A.dockFull = false; layout(); return 0; }       // Esc also shrinks an expanded dock
            setText(A.input, ""); if (v) v->s->userInput("#stop"); return 0;
        }
        if (w == 12) return 0;   // Ctrl+L handled by accelerator
    }
    if (m == WM_KEYDOWN) {
        if (w == VK_UP) { historyMove(-1); return 0; }
        if (w == VK_DOWN) { historyMove(+1); return 0; }
        if ((w == VK_PRIOR || w == VK_NEXT) && v) { SendMessageW(v->out, WM_VSCROLL, w == VK_PRIOR ? SB_PAGEUP : SB_PAGEDOWN, 0); return 0; }
        if (w == VK_HOME && (GetKeyState(VK_CONTROL) & 0x8000) && v) { SendMessageW(v->out, WM_VSCROLL, SB_TOP, 0); return 0; }
        if (w == VK_END && (GetKeyState(VK_CONTROL) & 0x8000) && v) { scrollBottom(v->out); return 0; }
        if (A.settings.numpad && v && GetWindowTextLengthW(h) == 0 && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            if (const char* d = numpadDir(w)) { A.swallowChar = (w >= VK_NUMPAD0 && w <= VK_NUMPAD9) || w == VK_ADD || w == VK_SUBTRACT; v->s->userInput(d); return 0; }
        }
    }
    return DefSubclassProc(h, m, w, l);
}

// ---------------------------------------------------------------------------
// Map panel
// ---------------------------------------------------------------------------
COLORREF hsl(double hh, double s, double l) {
    auto f = [&](double n) { double k = fmod(n + hh / 30.0, 12.0), a = s * std::min(l, 1 - l); return l - a * std::max(-1.0, std::min({k - 3, 9 - k, 1.0})); };
    return RGB((int)(f(0) * 255), (int)(f(8) * 255), (int)(f(4) * 255));
}
COLORREF terrainColor(const std::string& t, const Theme& th) {
    if (t.empty()) return th.panel;
    unsigned h = 2166136261u; for (unsigned char c : t) { h ^= c; h *= 16777619u; }
    return hsl(h % 360, 0.40, th.bg == RGB(0xfd, 0xfd, 0xf8) ? 0.72 : 0.30);
}

extern bool gDirty;
RECT mapBtnRect(const RECT& rc) { return RECT{rc.right - px(30), px(3), rc.right - px(6), px(3) + px(24)}; }
RECT mapFullBtnRect(const RECT& rc) { return RECT{rc.right - px(58), px(3), rc.right - px(34), px(3) + px(24)}; }
void toggleMapFull();
bool mapIsFull();
void popOutMap();
void dockMap();
void applyDwm();
void applyDwmTo(HWND w);

// ---------------------------------------------------------------------------
// Themed pop-over menu (replaces every native Windows menu)
// ---------------------------------------------------------------------------
struct MItem { int id = 0; std::wstring text, hint; bool enabled = true, checked = false, sep = false; };
struct PopupState { std::vector<MItem> items; std::vector<RECT> rects; int hover = -1, result = 0; bool done = false; std::wstring header; RECT headerRc{}; };

LRESULT CALLBACK popupProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    PopupState* s = (PopupState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) { s = (PopupState*)((CREATESTRUCT*)l)->lpCreateParams; SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s); }
    if (!s) return DefWindowProcW(h, m, w, l);
    auto itemAt = [&](POINT p) { for (size_t i = 0; i < s->rects.size(); i++) if (!s->items[i].sep && PtInRect(&s->rects[i], p)) return (int)i; return -1; };
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC dc = CreateCompatibleDC(wdc); HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(dc, bmp);
        const Theme& t = theme();
        HBRUSH wb = CreateSolidBrush(t.win); FillRect(dc, &rc, wb); DeleteObject(wb);
        SetBkMode(dc, TRANSPARENT);
        rrect(dc, rc, px(10), t.panel, t.accent);
        if (!s->header.empty()) {
            dtext(dc, gBody, t.fgBright, s->header, s->headerRc, DT_WORDBREAK | DT_NOPREFIX);
            HPEN pn = CreatePen(PS_SOLID, 1, t.border); HGDIOBJ op = SelectObject(dc, pn); MoveToEx(dc, px(14), s->headerRc.bottom + px(4), nullptr); LineTo(dc, rc.right - px(14), s->headerRc.bottom + px(4)); SelectObject(dc, op); DeleteObject(pn);
        }
        for (size_t i = 0; i < s->items.size(); i++) {
            const MItem& it = s->items[i]; RECT r = s->rects[i];
            if (it.sep) { HPEN pn = CreatePen(PS_SOLID, 1, t.border); HGDIOBJ op = SelectObject(dc, pn); MoveToEx(dc, r.left + px(10), (r.top + r.bottom) / 2, nullptr); LineTo(dc, r.right - px(10), (r.top + r.bottom) / 2); SelectObject(dc, op); DeleteObject(pn); continue; }
            bool hv = (int)i == s->hover && it.enabled;
            if (hv) rrect(dc, r, px(7), t.panelSel, -1);
            COLORREF tc = !it.enabled ? t.muted : hv ? t.fgBright : t.text;
            if (it.checked) dtext(dc, gBold, t.accent, L"✓", {r.left + px(8), r.top, r.left + px(28), r.bottom}, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            dtext(dc, gBody, tc, it.text, {r.left + px(32), r.top, r.right - px(10), r.bottom});
            if (!it.hint.empty()) dtext(dc, gBody, t.muted, it.hint, {r.left + px(32), r.top, r.right - px(12), r.bottom}, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob); DeleteObject(bmp); DeleteDC(dc); EndPaint(h, &ps); return 0;
    }
    case WM_MOUSEMOVE: { POINT p{(short)LOWORD(l), (short)HIWORD(l)}; int i = itemAt(p); if (i != s->hover) { s->hover = i; InvalidateRect(h, nullptr, FALSE); } return 0; }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: {
        POINT p{(short)LOWORD(l), (short)HIWORD(l)}; RECT rc; GetClientRect(h, &rc);
        if (!PtInRect(&rc, p)) { s->done = true; s->result = 0; }          // click outside = cancel
        return 0;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: {
        POINT p{(short)LOWORD(l), (short)HIWORD(l)}; int i = itemAt(p);
        if (i >= 0 && s->items[i].enabled) { s->result = s->items[i].id; s->done = true; }
        return 0;
    }
    case WM_CAPTURECHANGED: s->done = true; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Shows the menu at a screen position; returns the chosen id, or 0 if cancelled.
int popupMenu(POINT scr, const std::vector<MItem>& items, const std::wstring& header = L"") {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc = popupProc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"McPopup";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc); reg = true;
    }
    PopupState st; st.items = items;
    int rowH = px(34), sepH = px(11), pad = px(6), width = px(190);
    for (auto& it : items) if (!it.sep) width = std::max(width, textW(gBody, it.text) + (it.hint.empty() ? 0 : textW(gBody, it.hint) + px(30)) + px(58));
    int y = pad;
    if (!header.empty()) {
        st.header = header; width = std::max(width, px(330));
        RECT hr = {0, 0, width - px(28), 0}; HDC sdc = GetDC(nullptr); HGDIOBJ of = SelectObject(sdc, gBody);
        DrawTextW(sdc, header.c_str(), -1, &hr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX); SelectObject(sdc, of); ReleaseDC(nullptr, sdc);
        st.headerRc = {px(14), y + px(8), width - px(14), y + px(8) + hr.bottom};
        y = st.headerRc.bottom + px(10);
    }
    for (auto& it : items) { int hh = it.sep ? sepH : rowH; st.rects.push_back({pad, y, width - pad, y + hh}); y += hh; }
    int height = y + pad;
    HMONITOR mon = MonitorFromPoint(scr, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi);
    int x = scr.x, yy = scr.y;
    if (x + width > mi.rcWork.right) x = mi.rcWork.right - width;
    if (yy + height > mi.rcWork.bottom) yy = std::max<int>(mi.rcWork.top, (int)mi.rcWork.bottom - height);
    HWND pw = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"McPopup", L"", WS_POPUP, x, yy, width, height, A.wnd, nullptr, GetModuleHandle(nullptr), &st);
    ShowWindow(pw, SW_SHOWNOACTIVATE);
    SetCapture(pw);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_ESCAPE) { st.done = true; continue; }
            if (msg.wParam == VK_DOWN || msg.wParam == VK_UP) {
                int n = (int)st.items.size(), i = st.hover, dir = msg.wParam == VK_DOWN ? 1 : -1;
                for (int k = 0; k < n; k++) { i = (i + dir + n) % n; if (!st.items[i].sep && st.items[i].enabled) break; }
                st.hover = i; InvalidateRect(pw, nullptr, FALSE); continue;
            }
            if (msg.wParam == VK_RETURN) { if (st.hover >= 0) st.result = st.items[st.hover].id; st.done = true; continue; }
        }
        if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); break; }
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (GetCapture() == pw) ReleaseCapture();
    DestroyWindow(pw);
    return st.result;
}

// ---------------------------------------------------------------------------
// Toast: a small themed notice that fades away by itself (replaces message boxes for simple info)
// ---------------------------------------------------------------------------
LRESULT CALLBACK toastProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: DestroyWindow(h); return 0;
    case WM_LBUTTONDOWN: DestroyWindow(h); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        const Theme& t = theme();
        HBRUSH wb = CreateSolidBrush(t.win); FillRect(wdc, &rc, wb); DeleteObject(wb);
        SetBkMode(wdc, TRANSPARENT);
        rrect(wdc, rc, px(12), t.panel, t.accent, px(2));
        wchar_t buf[512]; GetWindowTextW(h, buf, 512);
        bool oneLine = textW(gBody, buf) <= rc.right - px(36);
        dtext(wdc, gBody, t.text, buf, {px(18), 0, rc.right - px(18), rc.bottom}, oneLine ? (DT_CENTER | DT_VCENTER | DT_SINGLELINE) : (DT_CENTER | DT_WORDBREAK));
        EndPaint(h, &ps); return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

void showToast(const std::wstring& text) {
    static bool reg = false; static HWND cur = nullptr;
    if (!reg) { WNDCLASSW wc{}; wc.lpfnWndProc = toastProc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"McToast"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc); reg = true; }
    if (cur && IsWindow(cur)) DestroyWindow(cur);
    int w = std::min(px(520), std::max(px(260), textW(gBody, text) + px(60))), h = textW(gBody, text) > px(460) ? px(76) : px(52);
    RECT mw; GetWindowRect(A.wnd, &mw);
    int x = (mw.left + mw.right - w) / 2, y = mw.bottom - px(150) - h;
    cur = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"McToast", text.c_str(), WS_POPUP, x, y, w, h, A.wnd, nullptr, GetModuleHandle(nullptr), nullptr);
    ShowWindow(cur, SW_SHOWNOACTIVATE);
    SetTimer(cur, 1, 4500, nullptr);
}

// The profile to use when there is no open session: the last one used, else the first.
const Profile* pickProfile() {
    for (auto& p : A.profiles) if (p.id == A.settings.lastProfile) return &p;
    return A.profiles.empty() ? nullptr : &A.profiles[0];
}

// ---- room info card (shown when a room is clicked) ----------------------------------
int cardHeightFor(const RECT& rc) { return std::max<int>(px(170), std::min<int>(px(300), (int)(rc.bottom - rc.top) * 48 / 100)); }

void drawRoomCard(HDC m, const RECT& rc, Mapper& mp, const Room& r, int cardH) {
    const Theme& t = theme();
    A.cardHits.clear();
    RECT card = {px(6), rc.bottom - cardH, std::min<int>((int)rc.right - px(6), px(6) + px(520)), rc.bottom - px(6)};   // capped width so it stays tidy when the map is large
    A.cardRect = card;
    rrect(m, card, px(10), t.panel, t.border);
    int pad = px(12), x0 = card.left + pad, x1 = card.right - pad, y = card.top + px(8);
    RECT cb = {card.right - px(32), card.top + px(6), card.right - px(8), card.top + px(30)};
    A.cardHits.push_back({cb, 5});
    dtext(m, gBody, t.muted, L"✕", cb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    dtext(m, gBold, t.text, W(r.name), {x0, y, cb.left - px(4), y + px(24)});
    y += px(24);
    std::string meta = "[" + r.id + "]";
    if (!r.area.empty()) meta += "   " + r.area;
    if (!r.terrain.empty()) meta += "   " + r.terrain;
    meta += "   (" + std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.z) + ")";
    if (r.visits > 0) meta += "   visited " + std::to_string(r.visits) + "x";
    dtext(m, gBody, t.muted, W(meta), {x0, y, x1, y + px(18)});
    y += px(22);
    std::wstring ex = L"Exits: ";
    if (r.exits.empty()) ex += L"none known";
    bool first = true;
    for (auto& e : r.exits) {
        if (!first) ex += L",  ";
        first = false;
        const Room* tg = e.second.empty() ? nullptr : mp.get(e.second);
        ex += W(e.first) + L" → " + (tg ? W(tg->name) : e.second.empty() ? L"?" : W(e.second));
    }
    { RECT er = {x0, y, x1, y + px(36)}; RECT calc = er; HGDIOBJ o = SelectObject(m, gBody);
      DrawTextW(m, ex.c_str(), -1, &calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX); SelectObject(m, o);
      int h = std::min<int>(px(36), (int)(calc.bottom - calc.top));
      er.bottom = er.top + h; dtext(m, gBody, t.text, ex, er, DT_WORDBREAK | DT_NOPREFIX); y += h + px(4); }
    if (!r.note.empty()) { std::string flat = r.note; for (auto& c : flat) if (c == '\n') c = ' ';
        dtext(m, gBody, t.accent, L"Note: " + W(flat), {x0, y, x1, y + px(20)}, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX); y += px(22); }
    // buttons
    int bh = px(28), by = card.bottom - px(8) - bh, bx = x0;
    struct Bt { const wchar_t* t; int id; } bts[] = {{L"Walk here", 1}, {L"Note\u2026", 2}, {L"Rename\u2026", 3}, {L"Copy", 4}};
    int gapB = px(6), padB = px(14), availB = x1 - x0, total = 0;
    for (auto& b : bts) total += textW(gBody, b.t);
    while (padB > px(4) && total + 4 * 2 * padB + 3 * gapB > availB) padB--;      // shrink padding so all four fit
    POINT cp; GetCursorPos(&cp); ScreenToClient(A.mapwnd, &cp);
    for (auto& b : bts) {
        int w = textW(gBody, b.t) + 2 * padB; RECT br = {bx, by, bx + w, by + bh};
        bool hv = PtInRect(&br, cp) != 0;
        rrect(m, br, px(8), hv ? mix(t.panelSel, t.accent, 85) : t.panelSel, hv ? t.accent : t.border);
        dtext(m, gBody, t.text, b.t, br, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        A.cardHits.push_back({br, b.id}); bx += w + gapB;
    }
    // description (scrollable with the mouse wheel)
    RECT dr = {x0, y, x1, by - px(8)};
    if (dr.bottom - dr.top > px(18)) {
        bool has = !r.desc.empty();
        std::wstring txt = has ? W(r.desc) : L"No description saved for this room yet. It is captured the next time you enter or look in it.";
        RECT calc = {0, 0, dr.right - dr.left, 10}; HGDIOBJ o = SelectObject(m, gBody);
        DrawTextW(m, txt.c_str(), -1, &calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX); SelectObject(m, o);
        int th = calc.bottom - calc.top, vis = dr.bottom - dr.top;
        A.descMax = std::max(0, th - vis);
        A.descScroll = std::max(0, std::min(A.descScroll, A.descMax));
        HRGN rg = CreateRectRgn(dr.left, dr.top, dr.right, dr.bottom); SelectClipRgn(m, rg);
        RECT tr = {dr.left, dr.top - A.descScroll, dr.right, dr.top - A.descScroll + th + px(4)};
        dtext(m, gBody, has ? t.fg : t.muted, txt, tr, DT_WORDBREAK | DT_NOPREFIX);
        SelectClipRgn(m, nullptr); DeleteObject(rg);
        if (A.descMax > 0) {   // slim scroll indicator
            int trk = vis, hh = std::max(px(18), trk * vis / (vis + A.descMax)), yy = dr.top + (trk - hh) * A.descScroll / A.descMax;
            RECT sb = {dr.right + px(2), yy, dr.right + px(5), yy + hh}; HBRUSH b = CreateSolidBrush(t.muted); FillRect(m, &sb, b); DeleteObject(b);
        }
    } else A.descMax = 0;
}

void copyText(HWND hw, const std::string& utf8) {
    std::wstring w = W(utf8), out;
    for (wchar_t c : w) { if (c == L'\n') out += L"\r\n"; else out.push_back(c); }
    if (!OpenClipboard(hw)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (out.size() + 1) * sizeof(wchar_t));
    if (g) { memcpy(GlobalLock(g), out.c_str(), (out.size() + 1) * sizeof(wchar_t)); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); }
    CloseClipboard();
}

// act: 1 walk, 2 note, 3 rename, 4 copy, 5 close card
void roomAction(HWND hw, int act, const std::string& id) {
    View* v = activeView(); if (!v) return;
    Mapper& mp = v->s->mapper; Room* r = mp.get(id); if (!r) return;
    HWND owner = GetAncestor(hw, GA_ROOT);
    if (act == 1) v->s->userInput("#go " + id);
    else if (act == 2) {
        std::string t = r->note, nm = r->name;
        if (showTextPrompt(owner, "Room note", "Note for \"" + nm + "\":", t, true)) { while (!t.empty() && (t.back() == '\n' || t.back() == ' ' || t.back() == '\t')) t.pop_back();
            if ((r = mp.get(id))) { r->note = t; v->s->touchMap(); } }
    } else if (act == 3) {
        std::string t = r->name;
        if (showTextPrompt(owner, "Rename room", "Room name:", t, false) && !t.empty()) { if ((r = mp.get(id))) { r->name = t; v->s->touchMap(); } }
    } else if (act == 4) { copyText(hw, mp.describe(id)); v->s->say("Room info copied to the clipboard.", 7); }
    else if (act == 5) A.selRoom.clear();
    InvalidateRect(hw, nullptr, FALSE);
    gDirty = true;
}

void paintMap(HWND hw) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(hw, &ps);
    RECT rc; GetClientRect(hw, &rc);
    HDC m = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(m, bmp);
    const Theme& t = theme();
    HBRUSH bg = CreateSolidBrush(t.bg); FillRect(m, &rc, bg); DeleteObject(bg);
    SetBkMode(m, TRANSPARENT); SelectObject(m, uiFont());
    A.hits.clear(); A.cardHits.clear(); A.cardRect = RECT{0, 0, 0, 0};
    View* v = activeView();
    const Room* cur = v ? v->s->mapper.get(v->s->mapper.current) : nullptr;
    const Room* sel = nullptr;
    if (v && !A.selRoom.empty()) { sel = v->s->mapper.get(A.selRoom); if (!sel) A.selRoom.clear(); }
    int cardH = (sel && cur) ? cardHeightFor(rc) : 0;
    if (!cur) {
        SetTextColor(m, t.fg);
        RECT tr = {10, 10, rc.right - 10, rc.bottom};
        DrawTextW(m, v ? L"No map yet.\n\nMove around, or connect to a MUD that sends GMCP room info, and rooms appear here as you explore.\n\nClick a room for its info, double-click to walk there. Wheel zooms, drag pans, right-click for more." : L"Not connected.", -1, &tr, DT_WORDBREAK);
    } else {
        Mapper& mp = v->s->mapper;
        if (A.lastMapRoom != cur->id) { A.lastMapRoom = cur->id; A.panX = A.panY = 0; if (v->s->gmcpRoom || true) A.zoff = 0; }
        int z = cur->z + A.zoff; const std::string& area = cur->area;
        int cell = A.mapCell, half = std::max(5, cell * 3 / 10);
        auto sx = [&](const Room& r) { return rc.right / 2 + (r.x - cur->x) * cell + A.panX; };
        auto sy = [&](const Room& r) { return (rc.bottom - cardH) / 2 + (r.y - cur->y) * cell + A.panY; };
        HPEN line = CreatePen(PS_SOLID, 2, t.pal[8]), unx = CreatePen(PS_SOLID, 1, t.pal[8]);
        auto onScreen = [&](int x, int y) { return x > -cell && y > -cell && x < rc.right + cell && y < rc.bottom + cell; };
        for (auto& kv : mp.rooms) {
            const Room& r = kv.second;
            if (!r.placed || r.area != area || r.z != z) continue;
            int x = sx(r), y = sy(r); if (!onScreen(x, y)) continue;
            for (auto& e : r.exits) {
                int dx, dy, dz; if (!dirOffset(e.first, dx, dy, dz) || (dx == 0 && dy == 0)) continue;
                const Room* tg = e.second.empty() ? nullptr : mp.get(e.second);
                if (tg && tg->placed && tg->area == area && tg->z == z) {
                    SelectObject(m, line); MoveToEx(m, x, y, nullptr); LineTo(m, sx(*tg), sy(*tg));
                } else {
                    SelectObject(m, e.second.empty() ? unx : line);
                    MoveToEx(m, x, y, nullptr); LineTo(m, x + dx * cell * 45 / 100, y + dy * cell * 45 / 100);
                }
            }
        }
        for (auto& kv : mp.rooms) {
            const Room& r = kv.second;
            if (!r.placed || r.area != area || r.z != z) continue;
            int x = sx(r), y = sy(r); if (!onScreen(x, y)) continue;
            bool isCur = r.id == cur->id;
            HBRUSH fill = CreateSolidBrush(isCur ? t.accent : terrainColor(r.terrain, t));
            HPEN pen = CreatePen(PS_SOLID, isCur ? 3 : 1, isCur ? t.fgBright : t.pal[8]);
            SelectObject(m, fill); SelectObject(m, pen);
            Rectangle(m, x - half, y - half, x + half + 1, y + half + 1);
            DeleteObject(fill); DeleteObject(pen);
            if (r.id == A.selRoom) {   // selection ring
                HPEN sp = CreatePen(PS_SOLID, 2, t.accent); HGDIOBJ op2 = SelectObject(m, sp), ob3 = SelectObject(m, GetStockObject(NULL_BRUSH));
                Rectangle(m, x - half - 4, y - half - 4, x + half + 5, y + half + 5); SelectObject(m, op2); SelectObject(m, ob3); DeleteObject(sp);
            }
            bool up = r.exits.count("u") != 0, down = r.exits.count("d") != 0;
            SetTextColor(m, t.fgBright);
            if (up || down) { std::wstring g = std::wstring(up ? L"▲" : L"") + (down ? L"▼" : L""); RECT gr = {x - half, y - half, x + half + 1, y + half + 1}; DrawTextW(m, g.c_str(), -1, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
            if (!r.note.empty()) { RECT nr = {x + half - 4, y - half - 2, x + half + 8, y - half + 10}; DrawTextW(m, L"*", -1, &nr, DT_LEFT | DT_SINGLELINE); }
            A.hits.push_back({{x - half, y - half, x + half + 1, y + half + 1}, r.id});
        }
        DeleteObject(line); DeleteObject(unx);
        SetTextColor(m, t.fg);
        std::wstring head = W(area) + L"  (level " + std::to_wstring(z) + L")";
        RECT hr = {6, 4, rc.right - 64, 22}; DrawTextW(m, head.c_str(), -1, &hr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        std::wstring foot = A.hover.empty() ? W(cur->name) : W(mp.rooms.count(A.hover) ? mp.rooms[A.hover].name + "  [" + A.hover + "]" : A.hover);
        if (!sel) { RECT fr = {6, rc.bottom - 22, rc.right - 6, rc.bottom - 2}; DrawTextW(m, foot.c_str(), -1, &fr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS); }
        else drawRoomCard(m, rc, mp, *sel, cardH);
    }
    // pop-out / dock button and expand / fullscreen button (top-right)
    { const Theme& th = theme();
      auto drawBtn = [&](RECT b, int which) {
          bool hv = A.mapBtnHover == which;
          if (hv) rrect(m, b, px(5), th.panelSel, th.border);
          COLORREF col = hv ? th.accent : th.muted;
          HPEN pn = CreatePen(PS_SOLID, 1, col); HGDIOBJ op = SelectObject(m, pn), ob2 = SelectObject(m, GetStockObject(NULL_BRUSH));
          int l = b.left + px(6), tp = b.top + px(6), sz = px(12);
          if (which == 1) {
              if (!A.mapPopped) { Rectangle(m, l, tp + px(3), l + sz - px(3), tp + sz); Rectangle(m, l + px(3), tp, l + sz, tp + sz - px(3)); }   // two windows: pop out
              else { Rectangle(m, l, tp, l + sz, tp + sz); HBRUSH fb = CreateSolidBrush(col); RECT in = {l + sz / 2, tp + sz / 2, l + sz - 1, tp + sz - 1}; FillRect(m, &in, fb); DeleteObject(fb); }  // dock
          } else {   // four corner brackets: expand (or shrink when already expanded)
              int c = px(4);
              if (!mapIsFull()) { MoveToEx(m, l, tp + c, nullptr); LineTo(m, l, tp); LineTo(m, l + c + 1, tp); MoveToEx(m, l + sz - c, tp, nullptr); LineTo(m, l + sz, tp); LineTo(m, l + sz, tp + c + 1);
                                MoveToEx(m, l, tp + sz - c, nullptr); LineTo(m, l, tp + sz); LineTo(m, l + c + 1, tp + sz); MoveToEx(m, l + sz - c, tp + sz, nullptr); LineTo(m, l + sz, tp + sz); LineTo(m, l + sz, tp + sz - c - 1); }
              else { int i = px(3); MoveToEx(m, l + i, tp, nullptr); LineTo(m, l + i, tp + i); LineTo(m, l, tp + i); MoveToEx(m, l + sz - i, tp, nullptr); LineTo(m, l + sz - i, tp + i); LineTo(m, l + sz, tp + i);
                     MoveToEx(m, l + i, tp + sz, nullptr); LineTo(m, l + i, tp + sz - i); LineTo(m, l, tp + sz - i); MoveToEx(m, l + sz - i, tp + sz, nullptr); LineTo(m, l + sz - i, tp + sz - i); LineTo(m, l + sz, tp + sz - i); }
          }
          SelectObject(m, op); SelectObject(m, ob2); DeleteObject(pn);
      };
      drawBtn(mapFullBtnRect(rc), 2); drawBtn(mapBtnRect(rc), 1); }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, m, 0, 0, SRCCOPY);
    SelectObject(m, ob); DeleteObject(bmp); DeleteDC(m);
    EndPaint(hw, &ps);
}

std::string hitRoom(int x, int y) {
    POINT p{x, y};
    for (auto it = A.hits.rbegin(); it != A.hits.rend(); ++it) if (PtInRect(&it->r, p)) return it->id;
    return "";
}

LRESULT CALLBACK mapProc(HWND hw, UINT m, WPARAM w, LPARAM l) {
    static bool drag = false; static POINT start, last; static bool moved = false;
    switch (m) {
    case WM_PAINT: paintMap(hw); return 0;
    case WM_SIZE: InvalidateRect(hw, nullptr, FALSE); return 0;      // a resize must repaint everything, or old pixels remain
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN: {
        RECT rc0; GetClientRect(hw, &rc0); POINT bp{(short)LOWORD(l), (short)HIWORD(l)}; RECT br = mapBtnRect(rc0), fr = mapFullBtnRect(rc0);
        SetFocus(hw);      // so F11 / Esc work while the map is in use
        if (PtInRect(&br, bp)) { A.mapBtnDown = 1; SetCapture(hw); return 0; }
        if (PtInRect(&fr, bp)) { A.mapBtnDown = 2; SetCapture(hw); return 0; }
        if (A.cardRect.right > A.cardRect.left && PtInRect(&A.cardRect, bp)) {   // click inside the info card
            A.cardDown = -1; for (auto& ch : A.cardHits) if (PtInRect(&ch.r, bp)) A.cardDown = ch.id;
            SetCapture(hw); return 0;
        }
        SetCapture(hw); drag = true; moved = false; start = last = {(short)LOWORD(l), (short)HIWORD(l)}; return 0; }
    case WM_MOUSEMOVE: {
        POINT p{(short)LOWORD(l), (short)HIWORD(l)};
        { RECT rc0; GetClientRect(hw, &rc0); RECT br = mapBtnRect(rc0), fr = mapFullBtnRect(rc0);
          int hv = PtInRect(&br, p) ? 1 : PtInRect(&fr, p) ? 2 : 0;
          if (hv != A.mapBtnHover) { A.mapBtnHover = hv; InvalidateRect(hw, nullptr, FALSE); if (hv) { TRACKMOUSEEVENT te{sizeof te, TME_LEAVE, hw, 0}; TrackMouseEvent(&te); } } }
        if (A.mapBtnDown) return 0;
        if (drag) {
            if (abs(p.x - start.x) + abs(p.y - start.y) > 4) moved = true;
            if (moved) { A.panX += p.x - last.x; A.panY += p.y - last.y; InvalidateRect(hw, nullptr, FALSE); }
            last = p;
        } else { std::string h = hitRoom(p.x, p.y); if (h != A.hover) { A.hover = h; InvalidateRect(hw, nullptr, FALSE); } }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_F11 || (w == VK_ESCAPE && mapIsFull())) { toggleMapFull(); return 0; }
        if (w == VK_RETURN && !A.mapPopped && A.input) { SetFocus(A.input); return 0; }
        break;
    case WM_CHAR:
        if (w >= 32 && !A.mapPopped && A.input) { SetFocus(A.input); SendMessageW(A.input, WM_CHAR, w, l); return 0; }   // typing goes to the input line
        break;
    case WM_MOUSELEAVE: if (A.mapBtnHover) { A.mapBtnHover = 0; InvalidateRect(hw, nullptr, FALSE); } return 0;
    case WM_LBUTTONUP: {
        if (A.mapBtnDown) {
            int which = A.mapBtnDown; A.mapBtnDown = 0; ReleaseCapture();
            RECT rc0; GetClientRect(hw, &rc0); POINT bp{(short)LOWORD(l), (short)HIWORD(l)}; RECT br = which == 1 ? mapBtnRect(rc0) : mapFullBtnRect(rc0);
            if (PtInRect(&br, bp)) { if (which == 2) toggleMapFull(); else if (A.mapPopped) dockMap(); else popOutMap(); }
            return 0;
        }
        if (A.cardDown != 0) {
            int id = A.cardDown; A.cardDown = 0; ReleaseCapture();
            POINT bp{(short)LOWORD(l), (short)HIWORD(l)};
            if (id > 0) for (auto& ch : A.cardHits) if (ch.id == id && PtInRect(&ch.r, bp)) { roomAction(hw, id, A.selRoom); break; }
            return 0;
        }
        if (drag) { ReleaseCapture(); drag = false; if (!moved) {
            std::string id = hitRoom((short)LOWORD(l), (short)HIWORD(l));
            A.selRoom = id; A.descScroll = 0;      // click a room = show its info; click empty space = hide the card
            InvalidateRect(hw, nullptr, FALSE);
        } }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT bp{(short)LOWORD(l), (short)HIWORD(l)};
        if (A.cardRect.right > A.cardRect.left && PtInRect(&A.cardRect, bp)) return 0;
        std::string id = hitRoom(bp.x, bp.y); View* v = activeView();
        if (!id.empty()) { A.selRoom = id; if (v && id != v->s->mapper.current) v->s->userInput("#go " + id); }
        else { A.panX = A.panY = 0; A.zoff = 0; }
        InvalidateRect(hw, nullptr, FALSE); return 0;
    }
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1;
        { POINT sp{(short)LOWORD(l), (short)HIWORD(l)}; ScreenToClient(hw, &sp);
          if (A.cardRect.right > A.cardRect.left && PtInRect(&A.cardRect, sp)) { A.descScroll = std::max(0, std::min(A.descMax, A.descScroll - d * px(28))); InvalidateRect(hw, nullptr, FALSE); return 0; } }
        if (LOWORD(w) & MK_SHIFT) A.zoff += d; else A.mapCell = std::max(14, std::min(90, A.mapCell + d * 4));
        InvalidateRect(hw, nullptr, FALSE); return 0;
    }
    case WM_RBUTTONUP: {
        POINT p{(short)LOWORD(l), (short)HIWORD(l)}; std::string id = hitRoom(p.x, p.y);
        bool none = id.empty();
        std::vector<MItem> mi = {
            {ID_MAP_INFO, L"Room info", L"", !none}, {ID_MAP_WALK, L"Walk here", L"", !none}, {ID_MAP_NOTE, L"Edit note\u2026", L"", !none},
            {ID_MAP_RENAME, L"Rename room\u2026", L"", !none}, {ID_MAP_DELETE, L"Delete room", L"", !none}, {0, L"", L"", true, false, true},
            {ID_MAP_RECENTER, L"Recenter"}, {ID_MAP_FULL, mapIsFull() ? L"Shrink map" : L"Expand map", L"F11"}, {ID_MAP_POP, A.mapPopped ? L"Dock map" : L"Pop out map"}};
        ClientToScreen(hw, &p);
        int cmd = popupMenu(p, mi);
        View* v = activeView();
        if (cmd == ID_MAP_POP) { if (A.mapPopped) dockMap(); else popOutMap(); return 0; }
        if (cmd == ID_MAP_FULL) { toggleMapFull(); return 0; }
        if (cmd == ID_MAP_RECENTER) { A.panX = A.panY = 0; A.zoff = 0; }
        else if (cmd == ID_MAP_INFO) { A.selRoom = id; A.descScroll = 0; }
        else if (cmd == ID_MAP_NOTE) roomAction(hw, 2, id);
        else if (cmd == ID_MAP_RENAME) roomAction(hw, 3, id);
        else if (cmd == ID_MAP_WALK && v) v->s->userInput("#go " + id);
        else if (cmd == ID_MAP_DELETE && v) v->s->userInput("#map delete " + id);
        InvalidateRect(hw, nullptr, FALSE); return 0;
    }
    }
    return DefWindowProcW(hw, m, w, l);
}

// ---------------------------------------------------------------------------
// Vitals bar
// ---------------------------------------------------------------------------
LRESULT CALLBACK vitalsProc(HWND hw, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hw, &ps); RECT rc; GetClientRect(hw, &rc);
        const Theme& t = theme();
        HBRUSH bg = CreateSolidBrush(t.panel); FillRect(dc, &rc, bg); DeleteObject(bg);
        SetBkMode(dc, TRANSPARENT); SelectObject(dc, uiFont());
        View* v = activeView();
        if (v && v->s->vitals.any()) {
            const Vitals& vt = v->s->vitals;
            struct Bar { const wchar_t* n; double cur, max; COLORREF c; } bars[3] = {
                {L"HP", vt.hp, vt.maxhp, RGB(0xc0, 0x30, 0x30)}, {L"MP", vt.mp, vt.maxmp, RGB(0x30, 0x60, 0xd0)}, {L"MV", vt.mv, vt.maxmv, RGB(0xc0, 0xa0, 0x20)}};
            int n = 0; for (auto& b : bars) if (b.cur >= 0) n++;
            int bw = (rc.right - 8 * (n + 1)) / std::max(1, n), x = 8;
            for (auto& b : bars) {
                if (b.cur < 0) continue;
                RECT r = {x, 3, x + bw, rc.bottom - 3};
                HBRUSH back = CreateSolidBrush(t.bg); FillRect(dc, &r, back); DeleteObject(back);
                double frac = b.max > 0 ? std::max(0.0, std::min(1.0, b.cur / b.max)) : 1.0;
                RECT f = r; f.right = r.left + (int)((r.right - r.left) * frac);
                HBRUSH fb = CreateSolidBrush(b.c); FillRect(dc, &f, fb); DeleteObject(fb);
                wchar_t buf[64]; if (b.max > 0) swprintf(buf, 64, L"%ls  %.0f / %.0f", b.n, b.cur, b.max); else swprintf(buf, 64, L"%ls  %.0f", b.n, b.cur);
                SetTextColor(dc, RGB(255, 255, 255));
                DrawTextW(dc, buf, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                x += bw + 8;
            }
        }
        EndPaint(hw, &ps); return 0;
    }
    return DefWindowProcW(hw, m, w, l);
}

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Map pop-out: the map window is re-parented into its own top-level window
// ---------------------------------------------------------------------------
void saveMapRect() {
    if (!A.mapPop) return;
    RECT r; GetWindowRect(A.mapPop, &r);
    A.settings.mapX = r.left; A.settings.mapY = r.top; A.settings.mapW = r.right - r.left; A.settings.mapH = r.bottom - r.top;
}

void layoutMapPop(HWND h) {
    RECT cr; GetClientRect(h, &cr);
    int inset = A.mapFull ? 0 : px(10) + px(8);    // window margin + panel padding, matching the main window (none in fullscreen)
    if (A.mapwnd && A.mapPopped) MoveWindow(A.mapwnd, inset, inset, std::max<int>(10, (int)cr.right - 2 * inset), std::max<int>(10, (int)cr.bottom - 2 * inset), TRUE);
}

LRESULT CALLBACK mapPopProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE: layoutMapPop(h); InvalidateRect(h, nullptr, TRUE); if (A.mapwnd) RedrawWindow(A.mapwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC dc = CreateCompatibleDC(wdc); HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(dc, bmp);
        const Theme& t = theme();
        HBRUSH wb = CreateSolidBrush(A.mapFull ? t.bg : t.win); FillRect(dc, &rc, wb); DeleteObject(wb);
        if (!A.mapFull) { RECT panel = {px(10), px(10), rc.right - px(10), rc.bottom - px(10)}; rrect(dc, panel, px(14), t.panel, t.border); }
        BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob); DeleteObject(bmp); DeleteDC(dc); EndPaint(h, &ps); return 0;
    }
    case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize = {px(240), px(240)}; return 0;
    case WM_EXITSIZEMOVE: saveMapRect(); saveSettings(A.settings); return 0;
    case WM_KEYDOWN: if (w == VK_F11 || (w == VK_ESCAPE && A.mapFull)) { toggleMapFull(); return 0; } break;
    case WM_CLOSE: dockMap(); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Docked: the map expands over the output area (the game text is hidden but keeps running; the input stays usable).
// Popped out: the map window goes borderless fullscreen on its monitor. F11 / Esc / the corner button toggle it.
bool mapIsFull() { return A.mapPopped ? A.mapFull : A.dockFull; }

void toggleMapFull() {
    if (A.mapPopped && A.mapPop) {
        HWND p = A.mapPop;
        if (!A.mapFull) {
            A.popSavedStyle = GetWindowLongPtrW(p, GWL_STYLE); GetWindowRect(p, &A.popSavedRect);
            MONITORINFO mi{sizeof mi}; GetMonitorInfoW(MonitorFromWindow(p, MONITOR_DEFAULTTONEAREST), &mi);
            A.mapFull = true;
            SetWindowLongPtrW(p, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
            SetWindowPos(p, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        } else {
            A.mapFull = false;
            SetWindowLongPtrW(p, GWL_STYLE, A.popSavedStyle);
            RECT r = A.popSavedRect;
            SetWindowPos(p, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        layoutMapPop(p); InvalidateRect(p, nullptr, TRUE);
    } else {
        if (!A.settings.showMap) return;
        A.dockTab = 0; A.dockFull = !A.dockFull;      // docked: the dock (showing the map) expands over the game text
        layout();
    }
    A.mapBtnHover = 0;
    InvalidateRect(A.mapwnd, nullptr, FALSE);
    SetFocus(A.mapwnd);
}

void popOutMap() {
    if (A.mapPopped || !A.wnd) return;
    A.mapFull = false; if (A.dockTab == 0) A.dockFull = false;      // leave 'expanded' before the map moves to its own window
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc = mapPopProc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"McMapPop";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(1));
        wc.hbrBackground = nullptr; wc.style = CS_HREDRAW | CS_VREDRAW; RegisterClassW(&wc); reg = true;
    }
    RECT mw; GetWindowRect(A.wnd, &mw);
    int w = std::max(px(260), A.settings.mapW), hh = std::max(px(260), A.settings.mapH);
    int x = A.settings.mapX, y = A.settings.mapY;
    RECT want = {x, y, x + w, y + hh};
    if (x == -1 || !MonitorFromRect(&want, MONITOR_DEFAULTTONULL)) { x = mw.right - w - px(30); y = mw.top + px(90); }   // default: over the right side of the window
    A.settings.showMap = true;
    A.mapPop = CreateWindowExW(WS_EX_TOOLWINDOW * 0, L"McMapPop", L"Map", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, hh, A.wnd, nullptr, GetModuleHandle(nullptr), nullptr);
    A.mapPopped = true;
    applyDwmTo(A.mapPop);
    SetParent(A.mapwnd, A.mapPop);
    layoutMapPop(A.mapPop);
    ShowWindow(A.mapwnd, SW_SHOW);
    ShowWindow(A.mapPop, SW_SHOWNOACTIVATE);
    A.settings.mapPopped = true; saveMapRect(); saveSettings(A.settings);
    layout();
    InvalidateRect(A.mapwnd, nullptr, FALSE);
}

void dockMap() {
    if (!A.mapPopped) return;
    if (A.mapFull && A.mapPop) { A.mapFull = false; SetWindowLongPtrW(A.mapPop, GWL_STYLE, A.popSavedStyle); }   // leave fullscreen first
    saveMapRect();
    SetParent(A.mapwnd, A.wnd);                 // move the map back before the popup is destroyed
    HWND pop = A.mapPop; A.mapPop = nullptr; A.mapPopped = false;
    DestroyWindow(pop);
    A.settings.mapPopped = false; saveSettings(A.settings);
    A.mapBtnHover = 0; A.mapFull = false; A.dockTab = 0;
    layout();
    InvalidateRect(A.mapwnd, nullptr, FALSE);
    if (A.input) SetFocus(A.input);
}

void updateMapTitle() {
    if (!A.mapPopped || !A.mapPop) return;
    std::wstring t = L"Map";
    if (View* v = activeView()) {
        const Room* r = v->s->mapper.get(v->s->mapper.current);
        t = L"Map — " + W(v->s->profile.name) + (r ? L" — " + W(r->area.empty() ? r->name : r->area + ": " + r->name) : L"");
    }
    SetWindowTextW(A.mapPop, t.c_str());
}

void applyDwmTo(HWND w) {
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static Fn fn = [] { HMODULE m = LoadLibraryW(L"dwmapi.dll"); return m ? (Fn)GetProcAddress(m, "DwmSetWindowAttribute") : nullptr; }();
    if (!fn || !w) return;
    const Theme& t = theme();
    BOOL dark = t.light() ? FALSE : TRUE;
    fn(w, 20, &dark, sizeof dark);                    // immersive dark title bar
    COLORREF cap = t.win, txt = t.text;
    fn(w, 35, &cap, sizeof cap);                      // caption colour (Windows 11)
    fn(w, 36, &txt, sizeof txt);                      // caption text colour
}
void doCommand(int id);
void layoutPanelPop(int k);
void openPanel(int k); void closePanel(int k); void togglePanel(int k);
void popOutPanel(int k); void dockPanel(int k); void togglePanelFull(int k);
void savePanelState();

void applyDwm() {
    applyDwmTo(A.wnd); applyDwmTo(A.mapPop);
    if (A.mapPop) InvalidateRect(A.mapPop, nullptr, TRUE);
    for (auto& p : gPanels) { if (p.pop) { applyDwmTo(p.pop); InvalidateRect(p.pop, nullptr, TRUE); } if (p.wnd) InvalidateRect(p.wnd, nullptr, FALSE); }
}

int panelIndex(const Panel* p) { return (int)(p - gPanels); }

// small vector icons for panel header buttons: 0 close, 1 expand/shrink, 2 pop-out/dock
void drawPanelIcon(HDC dc, RECT b, int kind, bool hv, bool flag) {
    const Theme& t = theme();
    if (hv) rrect(dc, b, px(6), t.panelSel, t.border);
    COLORREF col = hv ? t.accent : t.muted;
    HPEN pn = CreatePen(PS_SOLID, 1, col); HGDIOBJ op = SelectObject(dc, pn), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int sz = px(12), l = b.left + (b.right - b.left - sz) / 2, tp = b.top + (b.bottom - b.top - sz) / 2;
    if (kind == 0) { MoveToEx(dc, l, tp, nullptr); LineTo(dc, l + sz, tp + sz); MoveToEx(dc, l + sz, tp, nullptr); LineTo(dc, l, tp + sz); }
    else if (kind == 2) {
        if (!flag) { Rectangle(dc, l, tp + px(3), l + sz - px(3), tp + sz); Rectangle(dc, l + px(3), tp, l + sz, tp + sz - px(3)); }
        else { Rectangle(dc, l, tp, l + sz, tp + sz); HBRUSH fb = CreateSolidBrush(col); RECT in = {l + sz / 2, tp + sz / 2, l + sz - 1, tp + sz - 1}; FillRect(dc, &in, fb); DeleteObject(fb); }
    } else {
        int c = px(4), i = px(3);
        if (!flag) { MoveToEx(dc, l, tp + c, nullptr); LineTo(dc, l, tp); LineTo(dc, l + c + 1, tp); MoveToEx(dc, l + sz - c, tp, nullptr); LineTo(dc, l + sz, tp); LineTo(dc, l + sz, tp + c + 1);
                     MoveToEx(dc, l, tp + sz - c, nullptr); LineTo(dc, l, tp + sz); LineTo(dc, l + c + 1, tp + sz); MoveToEx(dc, l + sz - c, tp + sz, nullptr); LineTo(dc, l + sz, tp + sz); LineTo(dc, l + sz, tp + sz - c - 1); }
        else { MoveToEx(dc, l + i, tp, nullptr); LineTo(dc, l + i, tp + i); LineTo(dc, l, tp + i); MoveToEx(dc, l + sz - i, tp, nullptr); LineTo(dc, l + sz - i, tp + i); LineTo(dc, l + sz, tp + i);
               MoveToEx(dc, l + i, tp + sz, nullptr); LineTo(dc, l + i, tp + sz - i); LineTo(dc, l, tp + sz - i); MoveToEx(dc, l + sz - i, tp + sz, nullptr); LineTo(dc, l + sz - i, tp + sz - i); LineTo(dc, l + sz, tp + sz - i); }
    }
    SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pn);
}

int gSettingsSec = 0;       // selected section in the Settings window: Themes, Text, Panels, Behaviour, Advanced, About
#define MC_WIDEN2(x) L##x
#define MC_WIDEN(x) MC_WIDEN2(x)
const wchar_t* kVersion = L"1.0.0  (build " MC_WIDEN(__DATE__) L" " MC_WIDEN(__TIME__) L")";
const wchar_t* kContactEmail = L"steriledevelopment@gmail.com";
// The "Donate with PayPal" button image is embedded in the exe as resource 101 (PNG).
Gdiplus::Image* payPalImage() {
    static Gdiplus::Image* img = nullptr; static bool tried = false;
    if (tried) return img;
    tried = true;
    HRSRC rs = FindResourceW(nullptr, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));      // RT_RCDATA
    if (!rs) return nullptr;
    HGLOBAL hg = LoadResource(nullptr, rs); DWORD sz = SizeofResource(nullptr, rs); const void* data = hg ? LockResource(hg) : nullptr;
    if (!data || !sz) return nullptr;
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, sz); if (!copy) return nullptr;
    memcpy(GlobalLock(copy), data, sz); GlobalUnlock(copy);
    IStream* st = nullptr;
    if (CreateStreamOnHGlobal(copy, TRUE, &st) != S_OK) { GlobalFree(copy); return nullptr; }
    img = Gdiplus::Image::FromStream(st);                 // the stream must stay alive for the image's lifetime
    if (img && img->GetLastStatus() != Gdiplus::Ok) { delete img; img = nullptr; }
    return img;
}

const wchar_t* kDonateUrl = L"https://www.paypal.com/donate/?hosted_button_id=GYMZNRJ3TQF3N";   // PayPal donate button (owner-supplied)

void paintPanel(HWND hw, Panel& p) {
    int k = panelIndex(&p);
    PAINTSTRUCT ps; HDC wdc = BeginPaint(hw, &ps); RECT rc; GetClientRect(hw, &rc);
    HDC dc = CreateCompatibleDC(wdc); HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(dc, bmp);
    const Theme& t = theme(); SetBkMode(dc, TRANSPARENT);
    HBRUSH bg = CreateSolidBrush(t.panel); FillRect(dc, &rc, bg); DeleteObject(bg);
    p.hots.clear();
    int headH = px(52);
    int y = headH + px(4) - p.scroll, yStart = y;
    int cl = 0, cr = rc.right;                      // content column (the Settings window keeps a section menu on the left)
    auto visible = [&](int top, int h) { return top + h > headH && top < rc.bottom; };
    auto hov = [&]() { return (int)p.hots.size() == p.hover; };
    auto section = [&](const wchar_t* title) { y += px(10); if (visible(y, px(22))) dtext(dc, gBold, t.accent, title, {cl + px(18), y, cr - px(16), y + px(22)}); y += px(28); };
    auto paraRow = [&](const std::wstring& text, COLORREF col, HFONT f, int padBelow) {
        RECT r = {cl + px(18), y, cr - px(18), y + px(20)}; RECT calc = r; HGDIOBJ o = SelectObject(dc, f);
        DrawTextW(dc, text.c_str(), -1, &calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX); SelectObject(dc, o);
        int h = (int)(calc.bottom - calc.top); r.bottom = r.top + h;
        if (visible(y, h)) dtext(dc, f, col, text, r, DT_WORDBREAK | DT_NOPREFIX);
        y += h + px(padBelow);
    };
    auto imageButtonRow = [&](Gdiplus::Image* img, int widthPx, int cmd) {     // clickable picture (the PayPal donate button)
        if (!img) return;
        int iw = widthPx, ih = (int)((double)iw * img->GetHeight() / std::max<UINT>(1, img->GetWidth()));
        RECT r = {cl + px(18), y, cl + px(18) + iw, y + ih};
        if (visible(y, ih)) {
            Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(img, Gdiplus::Rect(r.left, r.top, iw, ih));
            p.hots.push_back({r, cmd, 0});
        }
        y += ih + px(10);
    };
    auto toggleRow = [&](const std::wstring& text, bool on, int cmd) {
        RECT r = {cl + px(8), y, cr - px(8), y + px(38)};
        if (visible(y, px(38))) {
            if (hov()) rrect(dc, r, px(8), t.panelSel, -1);
            dtext(dc, gBody, t.text, text, {r.left + px(12), r.top, r.right - px(70), r.bottom});
            int pw = px(40), ph = px(22); RECT sw = {r.right - px(12) - pw, r.top + (px(38) - ph) / 2, r.right - px(12), r.top + (px(38) - ph) / 2 + ph};
            rrect(dc, sw, ph / 2, on ? t.accentFill : t.inputBg, on ? t.accent : t.border);
            int kd = ph - px(8); int kx = on ? sw.right - px(4) - kd : sw.left + px(4);
            RECT kn = {kx, sw.top + px(4), kx + kd, sw.top + px(4) + kd}; rrect(dc, kn, kd / 2, on ? t.accent : t.muted, -1);
            p.hots.push_back({r, cmd, 0});
        }
        y += px(40);
    };
    auto themeRow = [&](int i) {
        const Theme& th = themes()[i]; bool cur = A.settings.theme == th.name;
        RECT r = {cl + px(8), y, cr - px(8), y + px(38)};
        if (visible(y, px(38))) {
            if (hov() || cur) rrect(dc, r, px(8), cur ? t.panelSel : mix(t.panelSel, t.panel, 50), cur ? t.accent : (COLORREF)-1);
            int cy = (r.top + r.bottom) / 2, cx = r.left + px(22);
            dot(dc, cx, cy, px(8), th.bg); dot(dc, cx + px(14), cy, px(8), th.accent); dot(dc, cx + px(28), cy, px(8), th.fg);
            dtext(dc, gBody, t.text, W(th.name), {r.left + px(72), r.top, r.right - px(40), r.bottom});
            if (cur) dtext(dc, gBold, t.accent, L"✓", {r.right - px(36), r.top, r.right - px(10), r.bottom}, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            p.hots.push_back({r, ID_THEME_BASE + i, 0});
        }
        y += px(40);
    };
    auto stepperRow = [&](const std::wstring& text, const std::wstring& val, int down, int up) {
        RECT r = {cl + px(8), y, cr - px(8), y + px(38)};
        if (visible(y, px(38))) {
            dtext(dc, gBody, t.text, text, {r.left + px(12), r.top, r.right - px(160), r.bottom});
            int bw = px(30), bh = px(28), by = r.top + (px(38) - bh) / 2;
            RECT plus = {r.right - px(12) - bw, by, r.right - px(12), by + bh}, minus = {plus.left - px(70) - bw, by, plus.left - px(70), by + bh};
            for (int pass = 0; pass < 2; pass++) {
                RECT b = pass ? plus : minus; int cmd = pass ? up : down; bool hv = hov();
                rrect(dc, b, px(8), hv ? mix(t.panelSel, t.accent, 85) : t.panelSel, hv ? t.accent : t.border);
                dtext(dc, gBody, t.text, pass ? L"+" : L"−", b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                p.hots.push_back({b, cmd, 0});
            }
            dtext(dc, gBody, t.muted, val, {minus.right, r.top, plus.left, r.bottom}, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        y += px(40);
    };
    auto buttonRow = [&](const std::wstring& text, const std::wstring& val, const std::wstring& btn, int cmd) {
        RECT r = {cl + px(8), y, cr - px(8), y + px(38)};
        if (visible(y, px(38))) {
            dtext(dc, gBody, t.text, text, {r.left + px(12), r.top, r.left + px(70), r.bottom});
            int bw = textW(gBody, btn) + px(28), bh = px(28), by = r.top + (px(38) - bh) / 2;
            RECT b = {r.right - px(12) - bw, by, r.right - px(12), by + bh}; bool hv = hov();
            rrect(dc, b, px(8), hv ? mix(t.panelSel, t.accent, 85) : t.panelSel, hv ? t.accent : t.border);
            dtext(dc, gBody, t.text, btn, b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            dtext(dc, gBody, t.muted, val, {r.left + px(70), r.top, b.left - px(8), r.bottom});
            p.hots.push_back({b, cmd, 0});
        }
        y += px(40);
    };

    if (isFormPanel(k)) {
        // the form window below the header paints the content
    } else {
        cl = std::min(px(190), (int)rc.right * 30 / 100);
        switch (gSettingsSec) {
        case 0:
            section(L"THEMES");
            for (int i = 0; i < (int)themes().size(); i++) themeRow(i);
            break;
        case 1:
            section(L"TEXT");
            stepperRow(L"Text size", std::to_wstring(A.settings.fontSize) + L" pt", ID_ZOOM_OUT, ID_ZOOM_IN);
            buttonRow(L"Font", W(A.settings.fontName), L"Change…", ID_VIEW_FONT);
            break;
        case 2:
            section(L"PANELS");
            toggleRow(L"Profile list", !A.settings.sidebarCollapsed, ID_VIEW_SIDEBAR);
            toggleRow(L"Map panel", A.settings.showMap, ID_VIEW_MAP);
            toggleRow(L"Chat panel", A.settings.showChat, ID_VIEW_CHAT);
            break;
        case 3:
            section(L"BEHAVIOUR");
            toggleRow(L"Echo sent commands", A.settings.echoInput, ID_VIEW_ECHO);
            toggleRow(L"Numpad walking", A.settings.numpad, ID_VIEW_NUMPAD);
            toggleRow(L"Log sessions to file", A.settings.logging, ID_VIEW_LOG);
            break;
        case 4:
            section(L"ADVANCED");
            stepperRow(L"Scrollback lines", std::to_wstring(A.settings.scrollback), ID_SET_SCROLL_DOWN, ID_SET_SCROLL_UP);
            stepperRow(L"Auto-walk delay", std::to_wstring(A.settings.walkDelayMs) + L" ms", ID_SET_WALK_DOWN, ID_SET_WALK_UP);
            break;
        case 5:
            section(L"ABOUT");
            paraRow(L"The Mud Crawler", t.text, gHead, 2);
            paraRow(std::wstring(L"Version ") + kVersion, t.muted, gBody, 12);
            paraRow(L"A native Windows MUD client written in C++. Telnet with GMCP, MSDP, MCCP2, NAWS and MTTS; ANSI 16 / 256 / truecolor; triggers, aliases, timers and variables; and an auto-mapper with room notes.", t.text, gBody, 12);
            paraRow(L"Your data lives in:", t.muted, gBody, 2);
            paraRow(W(dataDir()), t.text, gBody, 10);
            buttonRow(L"", L"", L"Open data folder", ID_OPEN_DATA);
            section(L"COPYRIGHT & CONTACT");
            paraRow(L"Copyright \u00A9 2026 Sterile Development. All rights reserved.", t.text, gBody, 8);
            buttonRow(L"Email", kContactEmail, L"Write to us", ID_OPEN_MAIL);
            section(L"SUPPORT THE MUD CRAWLER");
            paraRow(L"If you enjoy The Mud Crawler, you can help keep it going with a donation through PayPal. Thank you!", t.text, gBody, 8);
            if (Gdiplus::Image* pp = payPalImage()) imageButtonRow(pp, px(200), ID_OPEN_DONATE);
            else buttonRow(L"", L"", L"Donate with PayPal", ID_OPEN_DONATE);       // fallback if the image could not be loaded
            break;
        default:
            section(L"PRIVACY POLICY");
            for (auto& ps : kPrivacy) { paraRow(ps.heading, t.text, gBold, 2); paraRow(ps.body, t.muted, gBody, 14); }
            buttonRow(L"", L"", L"Copy policy text", ID_COPY_PRIVACY);
            break;
        }
    }
    p.contentH = (y + p.scroll) - (headH + px(4)) + px(14);
    int maxScroll = std::max(0, p.contentH - (int)(rc.bottom - headH));
    if (p.scroll > maxScroll) { p.scroll = maxScroll; }
    if (maxScroll > 0) {   // slim scroll indicator
        int trk = (int)rc.bottom - headH - px(8), hh = std::max(px(24), trk * ((int)rc.bottom - headH) / p.contentH);
        int yy = headH + px(4) + (trk - hh) * p.scroll / maxScroll;
        RECT sb = {rc.right - px(5), yy, rc.right - px(2), yy + hh}; HBRUSH b = CreateSolidBrush(t.muted); FillRect(dc, &sb, b); DeleteObject(b);
    }
    if (!isFormPanel(k)) {   // Settings: section menu on the left
        RECT nav = {0, headH, cl, rc.bottom}; HBRUSH nb = CreateSolidBrush(t.win); FillRect(dc, &nav, nb); DeleteObject(nb);
        { HPEN pn = CreatePen(PS_SOLID, 1, t.border); HGDIOBJ op = SelectObject(dc, pn); MoveToEx(dc, cl - 1, headH, nullptr); LineTo(dc, cl - 1, rc.bottom); SelectObject(dc, op); DeleteObject(pn); }
        static const wchar_t* names[7] = {L"Themes", L"Text", L"Panels", L"Behaviour", L"Advanced", L"About", L"Privacy"};
        int ny = headH + px(12);
        for (int i = 0; i < 7; i++, ny += px(44)) {
            RECT r = {px(8), ny, cl - px(8), ny + px(40)}; bool sel = gSettingsSec == i, hv = hov();
            if (sel || hv) rrect(dc, r, px(8), sel ? t.panelSel : mix(t.panelSel, t.win, 50), -1);
            if (sel) { RECT bar = {r.left, r.top + px(9), r.left + px(3), r.bottom - px(9)}; HBRUSH b = CreateSolidBrush(t.accent); FillRect(dc, &bar, b); DeleteObject(b); }
            dtext(dc, gBody, sel || hv ? t.text : t.muted, names[i], {r.left + px(18), r.top, r.right - px(6), r.bottom});
            p.hots.push_back({r, ID_SET_SEC_BASE + i, 0});
        }
    }
    // header drawn last so scrolled content never shows through it
    RECT hb = {0, 0, rc.right, headH}; HBRUSH hbr = CreateSolidBrush(t.panel); FillRect(dc, &hb, hbr); DeleteObject(hbr);
    { HPEN pn = CreatePen(PS_SOLID, 1, t.border); HGDIOBJ op = SelectObject(dc, pn); MoveToEx(dc, px(10), headH - 1, nullptr); LineTo(dc, rc.right - px(10), headH - 1); SelectObject(dc, op); DeleteObject(pn); }
    dtext(dc, gHead, t.text, p.title, {px(18), 0, rc.right - px(130), headH});
    int bx = rc.right - px(12) - px(30);
    struct HB { int cmd, kind; bool flag; } hbs[3] = {{PC_CLOSE, 0, false}, {PC_POP, 2, p.popped}, {PC_EXPAND, 1, p.popped ? p.full : A.dockFull}};
    for (auto& hb2 : hbs) {
        if (isWindowOnly(k)) break;                     // window-only panels have no dock / expand / close buttons: use the title bar
        RECT b = {bx, px(11), bx + px(30), px(41)};
        bool hv = (int)p.hots.size() == p.hover;
        drawPanelIcon(dc, b, hb2.kind, hv, hb2.flag);
        p.hots.push_back({b, hb2.cmd, 0});
        bx -= px(34);
    }
    (void)yStart;
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob); DeleteObject(bmp); DeleteDC(dc); EndPaint(hw, &ps);
}

// ===========================================================================
// Themed form toolkit (used by the Macros and Profiles panels)
// Retained widgets, custom drawn, with real EDIT controls hosted for text input.
// ===========================================================================
enum WK { WK_LABEL, WK_SECTION, WK_EDIT, WK_CHECK, WK_BUTTON, WK_SELECT, WK_LIST, WK_TABS, WK_NOTE };
struct Widget {
    WK kind = WK_LABEL; RECT r{}; int id = 0; std::wstring text;
    bool on = false, enabled = true, primary = false, multi = false, password = false, number = false, mono = false, danger = false;
    std::vector<std::wstring> items; int sel = -1;
};

std::wstring nl2win(const std::string& s) { std::string r; for (char c : s) { if (c == '\n') r += "\r\n"; else if (c != '\r') r.push_back(c); } return W(r); }
std::string win2nl(HWND h) { std::string s = getText(h), r; for (char c : s) if (c != '\r') r.push_back(c); return r; }

struct Form {
    HWND wnd = nullptr;
    std::vector<Widget> ws;
    std::map<int, HWND> edits;
    std::map<int, int> listScroll;
    int scroll = 0, contentH = 0, hover = -1, down = -1, clientW = 0, clientH = 0;
    bool loading = false;
    std::function<void(Form&, int)> build;                 // fill ws for the current width (rects before scrolling)
    std::function<void(Form&, int, int)> onClick;          // (widget id, arg: list row / tab / select choice / 0)
    std::function<void(Form&, int)> onEdit;                // text of an EDIT changed
    std::function<void(Form&, int, bool)> onFocus;         // EDIT gained / lost focus

    Widget& add(WK k, RECT r, int id = 0, const std::wstring& text = L"") { Widget w; w.kind = k; w.r = r; w.id = id; w.text = text; ws.push_back(w); return ws.back(); }
    std::string getEdit(int id) { auto it = edits.find(id); return it == edits.end() ? "" : win2nl(it->second); }
    void setEdit(int id, const std::string& s) { auto it = edits.find(id); if (it == edits.end()) return; loading = true; SetWindowTextW(it->second, nl2win(s).c_str()); loading = false; }
    Widget* find(int id) { for (auto& w : ws) if (w.id == id) return &w; return nullptr; }
};

const int kFormRow = 30;

LRESULT CALLBACK editWheelSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_MOUSEWHEEL) { SendMessageW(GetParent(h), WM_MOUSEWHEEL, w, l); return 0; }     // scroll the form, don't swallow the wheel
    return DefSubclassProc(h, m, w, l);
}

void formSyncEdits(Form& F) {
    RECT cr; GetClientRect(F.wnd, &cr);
    std::set<int> used;
    for (auto& w : F.ws) {
        if (w.kind != WK_EDIT) continue;
        used.insert(w.id);
        HWND e = F.edits.count(w.id) ? F.edits[w.id] : nullptr;
        if (!e) {
            DWORD st = WS_CHILD | WS_TABSTOP | (w.multi ? (ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL) : ES_AUTOHSCROLL) | (w.password ? ES_PASSWORD : 0) | (w.number ? ES_NUMBER : 0);
            e = CreateWindowExW(0, L"EDIT", L"", st, 0, 0, 10, 10, F.wnd, (HMENU)(INT_PTR)w.id, GetModuleHandle(nullptr), nullptr);
            SendMessageW(e, EM_LIMITTEXT, w.multi ? 8000 : 400, 0);
            SetWindowSubclass(e, editWheelSub, 1, 0);
            F.edits[w.id] = e;
        }
        SendMessageW(e, WM_SETFONT, (WPARAM)(w.mono ? A.mono : gBody), TRUE);
        if (w.multi) darkScroll(e);       // dark scrollbar for multi-line boxes
        RECT in = w.r; InflateRect(&in, -px(10), 0);
        if (w.multi) { in.top += px(7); in.bottom -= px(7); }
        else { int th = px(20); int mid = (in.top + in.bottom) / 2; in.top = mid - th / 2; in.bottom = in.top + th; }
        bool vis = w.r.bottom > 0 && w.r.top < cr.bottom;
        MoveWindow(e, in.left, in.top, std::max<int>(10, (int)(in.right - in.left)), std::max<int>(10, (int)(in.bottom - in.top)), TRUE);
        ShowWindow(e, vis ? SW_SHOW : SW_HIDE);
        EnableWindow(e, w.enabled);
    }
    for (auto& kv : F.edits) if (!used.count(kv.first)) ShowWindow(kv.second, SW_HIDE);
}

void formRelayout(Form& F) {
    if (!F.wnd) return;
    RECT cr; GetClientRect(F.wnd, &cr); F.clientW = cr.right; F.clientH = cr.bottom;
    F.ws.clear();
    if (F.build) F.build(F, F.clientW);
    int bottom = 0; for (auto& w : F.ws) bottom = std::max<int>(bottom, (int)w.r.bottom);
    F.contentH = bottom + px(14);
    int maxScroll = std::max(0, F.contentH - F.clientH);
    F.scroll = std::max(0, std::min(F.scroll, maxScroll));
    for (auto& w : F.ws) OffsetRect(&w.r, 0, -F.scroll);
    formSyncEdits(F);
    InvalidateRect(F.wnd, nullptr, FALSE);
}

int formHit(Form& F, POINT p) {
    for (int i = (int)F.ws.size() - 1; i >= 0; i--) {
        Widget& w = F.ws[i];
        if ((w.kind == WK_CHECK || w.kind == WK_BUTTON || w.kind == WK_SELECT || w.kind == WK_LIST || w.kind == WK_TABS) && w.enabled && PtInRect(&w.r, p)) return i;
    }
    return -1;
}

void formPaint(HWND hw, Form& F) {
    PAINTSTRUCT ps; HDC wdc = BeginPaint(hw, &ps); RECT rc; GetClientRect(hw, &rc);
    HDC dc = CreateCompatibleDC(wdc); HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(dc, bmp);
    const Theme& t = theme(); SetBkMode(dc, TRANSPARENT);
    HBRUSH bg = CreateSolidBrush(t.panel); FillRect(dc, &rc, bg); DeleteObject(bg);
    HWND focus = GetFocus();
    for (int i = 0; i < (int)F.ws.size(); i++) {
        Widget& w = F.ws[i];
        if (w.r.bottom < 0 || w.r.top > rc.bottom) continue;
        bool hv = i == F.hover && w.enabled;
        switch (w.kind) {
        case WK_LABEL: dtext(dc, gBody, w.enabled ? t.muted : mix(t.muted, t.panel, 50), w.text, w.r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS); break;
        case WK_SECTION: dtext(dc, gBold, t.accent, w.text, w.r, DT_LEFT | DT_BOTTOM | DT_SINGLELINE); break;
        case WK_NOTE: dtext(dc, gBody, w.mono ? t.text : t.muted, w.text, w.r, DT_LEFT | DT_TOP | DT_WORDBREAK); break;
        case WK_EDIT: {
            bool foc = F.edits.count(w.id) && focus == F.edits[w.id];
            rrect(dc, w.r, px(8), w.enabled ? t.inputBg : t.panel, foc ? t.accent : t.border, foc ? px(2) : 1);
            break;
        }
        case WK_CHECK: {
            int bs = px(18); RECT bx = {w.r.left, (w.r.top + w.r.bottom) / 2 - bs / 2, w.r.left + bs, (w.r.top + w.r.bottom) / 2 - bs / 2 + bs};
            rrect(dc, bx, px(5), w.on ? t.accentFill : t.inputBg, w.on || hv ? t.accent : t.border);
            if (w.on) dtext(dc, gBold, t.accent, L"✓", bx, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            dtext(dc, gBody, w.enabled ? t.text : t.muted, w.text, {bx.right + px(10), w.r.top, w.r.right, w.r.bottom});
            break;
        }
        case WK_BUTTON: {
            COLORREF fill = w.danger ? t.dangerFill : w.primary ? t.accentFill : t.panelSel, border = w.danger ? mix(RGB(0xd0, 0x50, 0x50), t.dangerFill, 70) : w.primary ? t.accent : t.border;
            rrect(dc, w.r, px(9), hv ? mix(w.primary ? t.accent : border, fill, 30) : fill, hv ? t.accent : border, w.primary ? px(2) : 1);
            dtext(dc, gBold, hv && w.primary ? RGB(0x10, 0x0c, 0x06) : w.danger ? RGB(0xf4, 0xc0, 0xc0) : t.text, w.text, w.r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case WK_SELECT: {
            rrect(dc, w.r, px(8), t.inputBg, hv ? t.accent : t.border);
            std::wstring cur = (w.sel >= 0 && w.sel < (int)w.items.size()) ? w.items[w.sel] : L"";
            dtext(dc, gBody, t.text, cur, {w.r.left + px(12), w.r.top, w.r.right - px(30), w.r.bottom});
            dtext(dc, gBody, t.muted, L"▾", {w.r.right - px(30), w.r.top, w.r.right - px(8), w.r.bottom}, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case WK_TABS: {
            int n = (int)w.items.size(); if (!n) break;
            int tw = (w.r.right - w.r.left) / n;
            for (int k = 0; k < n; k++) {
                RECT tr = {w.r.left + k * tw, w.r.top, w.r.left + (k + 1) * tw - px(4), w.r.bottom};
                bool sel = k == w.sel;
                rrect(dc, tr, px(8), sel ? t.accentFill : (hv ? mix(t.panelSel, t.panel, 60) : t.panelSel), sel ? t.accent : t.border);
                dtext(dc, gBody, sel ? t.fgBright : t.text, w.items[k], tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            break;
        }
        case WK_LIST: {
            rrect(dc, w.r, px(8), t.inputBg, t.border);
            int rowH = px(kFormRow); int sc = F.listScroll[w.id];
            int total = (int)w.items.size() * rowH, vis = (int)(w.r.bottom - w.r.top) - px(8);
            int maxSc = std::max(0, total - vis); sc = std::max(0, std::min(sc, maxSc)); F.listScroll[w.id] = sc;
            HRGN rg = CreateRectRgn(w.r.left + 2, w.r.top + 4, w.r.right - 2, w.r.bottom - 4); SelectClipRgn(dc, rg);
            for (int k = 0; k < (int)w.items.size(); k++) {
                RECT rr = {w.r.left + px(4), w.r.top + px(4) + k * rowH - sc, w.r.right - px(4), w.r.top + px(4) + (k + 1) * rowH - sc};
                if (rr.bottom < w.r.top || rr.top > w.r.bottom) continue;
                bool selr = k == w.sel;
                if (selr) { rrect(dc, rr, px(6), t.panelSel, -1); RECT bar = {rr.left, rr.top + px(6), rr.left + px(3), rr.bottom - px(6)}; HBRUSH b = CreateSolidBrush(t.accent); FillRect(dc, &bar, b); DeleteObject(b); }
                dtext(dc, gBody, selr ? t.text : t.text, w.items[k], {rr.left + px(12), rr.top, rr.right - px(8), rr.bottom});
            }
            SelectClipRgn(dc, nullptr); DeleteObject(rg);
            if (maxSc > 0) { int hh = std::max(px(20), vis * vis / total), yy = w.r.top + px(4) + (vis - hh) * sc / maxSc; RECT sb = {w.r.right - px(6), yy, w.r.right - px(3), yy + hh}; HBRUSH b = CreateSolidBrush(t.muted); FillRect(dc, &sb, b); DeleteObject(b); }
            break;
        }
        }
    }
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob); DeleteObject(bmp); DeleteDC(dc); EndPaint(hw, &ps);
}

LRESULT CALLBACK formProc(HWND hw, UINT m, WPARAM w, LPARAM l) {
    Form* F = (Form*)GetWindowLongPtrW(hw, GWLP_USERDATA);
    if (m == WM_NCCREATE) { F = (Form*)((CREATESTRUCT*)l)->lpCreateParams; SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)F); F->wnd = hw; }
    if (!F) return DefWindowProcW(hw, m, w, l);
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: formPaint(hw, *F); return 0;
    case WM_SIZE: formRelayout(*F); return 0;
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: { const Theme& t = theme(); SetTextColor((HDC)w, t.text); SetBkColor((HDC)w, t.inputBg); return (LRESULT)A.inputBrush; }
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (code == EN_CHANGE && !F->loading && F->onEdit) F->onEdit(*F, id);
        else if ((code == EN_SETFOCUS || code == EN_KILLFOCUS)) { if (F->onFocus) F->onFocus(*F, id, code == EN_SETFOCUS); InvalidateRect(hw, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p{(short)LOWORD(l), (short)HIWORD(l)}; int i = formHit(*F, p);
        if (i != F->hover) { F->hover = i; InvalidateRect(hw, nullptr, FALSE); }
        if (i >= 0) { TRACKMOUSEEVENT te{sizeof te, TME_LEAVE, hw, 0}; TrackMouseEvent(&te); }
        return 0;
    }
    case WM_MOUSELEAVE: if (F->hover != -1) { F->hover = -1; InvalidateRect(hw, nullptr, FALSE); } return 0;
    case WM_LBUTTONDOWN: { SetFocus(hw); POINT p{(short)LOWORD(l), (short)HIWORD(l)}; F->down = formHit(*F, p); InvalidateRect(hw, nullptr, FALSE); return 0; }
    case WM_LBUTTONUP: {
        POINT p{(short)LOWORD(l), (short)HIWORD(l)}; int i = formHit(*F, p), d = F->down; F->down = -1;
        if (i < 0 || i != d) return 0;
        Widget w0 = F->ws[i];                      // copy: the handler rebuilds the widget list
        if (w0.kind == WK_CHECK || w0.kind == WK_BUTTON) { if (F->onClick) F->onClick(*F, w0.id, 0); }
        else if (w0.kind == WK_TABS) { int n = (int)w0.items.size(); int k = std::min(n - 1, (int)((p.x - w0.r.left) * n / std::max<int>(1, (int)(w0.r.right - w0.r.left)))); if (F->onClick) F->onClick(*F, w0.id, k); }
        else if (w0.kind == WK_LIST) { int rowH = px(kFormRow); int row = (int)((p.y - w0.r.top - px(4) + F->listScroll[w0.id]) / rowH); if (row >= 0 && row < (int)w0.items.size() && F->onClick) F->onClick(*F, w0.id, row); }
        else if (w0.kind == WK_SELECT) {
            std::vector<MItem> mi; for (size_t k = 0; k < w0.items.size(); k++) { MItem it; it.id = (int)k + 1; it.text = w0.items[k]; it.checked = (int)k == w0.sel; mi.push_back(it); }
            POINT sp{w0.r.left, w0.r.bottom + px(4)}; ClientToScreen(hw, &sp);
            int c = popupMenu(sp, mi); if (c > 0 && F->onClick) F->onClick(*F, w0.id, c - 1);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1; POINT sp{(short)LOWORD(l), (short)HIWORD(l)}; ScreenToClient(hw, &sp);
        for (auto& wd : F->ws) if (wd.kind == WK_LIST && PtInRect(&wd.r, sp) && (int)wd.items.size() * px(kFormRow) > (int)(wd.r.bottom - wd.r.top) - px(8)) {
            F->listScroll[wd.id] = std::max(0, F->listScroll[wd.id] - d * px(kFormRow) * 2); InvalidateRect(hw, nullptr, FALSE); return 0;
        }
        F->scroll = std::max(0, std::min(std::max(0, F->contentH - F->clientH), F->scroll - d * px(60)));
        formRelayout(*F); return 0;
    }
    }
    return DefWindowProcW(hw, m, w, l);
}

HWND createFormWindow(HWND parent, Form* F) {
    static bool reg = false;
    if (!reg) { WNDCLASSW wc{}; wc.lpfnWndProc = formProc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"McForm"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.style = CS_HREDRAW | CS_VREDRAW; RegisterClassW(&wc); reg = true; }
    return CreateWindowExW(0, L"McForm", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr, GetModuleHandle(nullptr), F);
}

// layout helpers shared by the builders
struct Flow {
    Form& F; int x, y, w; int labelW = 0;
    Flow(Form& f, int x_, int y_, int w_) : F(f), x(x_), y(y_), w(w_) {}
    void gap(int g) { y += px(g); }
    void section(const std::wstring& t) { y += px(10); F.add(WK_SECTION, {x, y, x + w, y + px(24)}, 0, t); y += px(28); }
    void note(const std::wstring& t, int hpx, bool bright = false) { Widget& n = F.add(WK_NOTE, {x, y, x + w, y + px(hpx)}, 0, t); n.mono = bright; y += px(hpx) + px(6); }
    void field(const std::wstring& label, int id, bool multi = false, int hpx = 34, bool mono = false, bool password = false, bool number = false) {
        int lw = labelW; int h = multi ? px(hpx) : px(34);
        if (lw > 0) { F.add(WK_LABEL, {x, y, x + lw, y + px(34)}, 0, label); }
        Widget& e = F.add(WK_EDIT, {x + lw, y, x + w, y + h}, id); e.multi = multi; e.mono = mono; e.password = password; e.number = number;
        y += h + px(8);
    }
    void select(const std::wstring& label, int id, std::vector<std::wstring> items, int sel, int selW = 0) {
        int lw = labelW; if (lw > 0) F.add(WK_LABEL, {x, y, x + lw, y + px(34)}, 0, label);
        int sw = selW > 0 ? px(selW) : (w - lw);
        Widget& s = F.add(WK_SELECT, {x + lw, y, x + lw + sw, y + px(34)}, id); s.items = std::move(items); s.sel = sel;
        y += px(42);
    }
    void check(const std::wstring& label, int id, bool on, bool enabled = true) { Widget& c = F.add(WK_CHECK, {x + labelW, y, x + w, y + px(28)}, id, label); c.on = on; c.enabled = enabled; y += px(32); }
};

// ===========================================================================
// Macros panel: triggers, aliases, timers, variables
// ===========================================================================
enum { M_TABS = 1, M_LIST, M_ADD, M_DEL, M_TYPE, M_PANE, M_COLOR, M_EN, M_GAG, M_STOP, M_ONCE, M_REP,
       E_NAME = 20, E_PAT, E_CMDS, E_SECS, E_TEST };
struct MacrosState {
    RuleEngine* eng = nullptr; std::function<void()> save; std::string title, boundKey; bool live = false;
    RuleEngine offline;
    int cat = 0, cur = -1, typeSel = 0, paneSel = 0, colorSel = 0;
    bool en = true, gag = false, stop = false, once = false, rep = true;
    std::wstring testResult; bool dirty = false; ULONGLONG dirtyAt = 0;
} MS;
Form gMacrosForm;

size_t macrosCount() {
    if (!MS.eng) return 0;
    switch (MS.cat) { case 0: return MS.eng->triggers.size(); case 1: return MS.eng->aliases.size(); case 2: return MS.eng->timers.size(); default: return MS.eng->vars.size(); }
}
std::string macrosVarKey(size_t i) { auto it = MS.eng->vars.begin(); std::advance(it, i); return it->first; }
std::wstring macrosLabel(size_t i) {
    RuleEngine& r = *MS.eng;
    switch (MS.cat) {
    case 0: { auto& t = r.triggers[i]; return W((t.name.empty() ? t.pattern : t.name) + (t.enabled ? "" : "  (off)")); }
    case 1: { auto& a = r.aliases[i]; return W((a.name.empty() ? a.pattern : a.name) + (a.enabled ? "" : "  (off)")); }
    case 2: { auto& t = r.timers[i]; return W((t.name.empty() ? "timer" : t.name) + (t.enabled ? "" : "  (off)")); }
    default: { auto it = r.vars.begin(); std::advance(it, i); return W(it->first + " = " + it->second.substr(0, 40)); }
    }
}
void macrosFlush() { if (MS.dirty && MS.save) { MS.save(); } MS.dirty = false; }
void macrosMarkDirty() { MS.dirty = true; MS.dirtyAt = GetTickCount64(); }
void macrosDetach() { macrosFlush(); MS.eng = nullptr; MS.save = nullptr; MS.boundKey.clear(); }

void macrosStore() {
    if (!MS.eng || MS.cur < 0 || MS.cur >= (int)macrosCount()) return;
    Form& F = gMacrosForm; RuleEngine& r = *MS.eng;
    if (MS.cat == 0) {
        Trigger& t = r.triggers[MS.cur];
        t.name = F.getEdit(E_NAME); t.pattern = F.getEdit(E_PAT); t.type = (MatchType)MS.typeSel; t.commands = F.getEdit(E_CMDS);
        t.enabled = MS.en; t.gag = MS.gag; t.stop = MS.stop; t.once = MS.once; t.fg = MS.colorSel <= 0 ? -1 : MS.colorSel; t.pane = MS.paneSel == 1 ? "chat" : "main";
    } else if (MS.cat == 1) {
        Alias& a = r.aliases[MS.cur];
        a.name = F.getEdit(E_NAME); a.pattern = F.getEdit(E_PAT); a.type = (MatchType)MS.typeSel; a.commands = F.getEdit(E_CMDS); a.enabled = MS.en;
    } else if (MS.cat == 2) {
        Timer& t = r.timers[MS.cur];
        t.name = F.getEdit(E_NAME); t.seconds = std::max(0.1, atof(F.getEdit(E_SECS).c_str())); t.repeat = MS.rep; t.commands = F.getEdit(E_CMDS); t.enabled = MS.en; t.elapsed = 0;
    } else {
        std::string key = macrosVarKey(MS.cur), nk = F.getEdit(E_NAME), val = F.getEdit(E_CMDS);
        if (nk.empty()) nk = key;
        if (nk != key) { r.vars.erase(key); }
        r.vars[nk] = val;
        MS.cur = (int)std::distance(r.vars.begin(), r.vars.find(nk));      // the map is sorted: follow the renamed variable
    }
    macrosMarkDirty();
}

void macrosLoadState() {
    if (!MS.eng || MS.cur < 0 || MS.cur >= (int)macrosCount()) return;
    RuleEngine& r = *MS.eng;
    if (MS.cat == 0) { Trigger& t = r.triggers[MS.cur]; MS.typeSel = (int)t.type; MS.paneSel = t.pane == "chat" ? 1 : 0; MS.colorSel = t.fg < 0 || t.fg > 15 ? 0 : t.fg; MS.en = t.enabled; MS.gag = t.gag; MS.stop = t.stop; MS.once = t.once; }
    else if (MS.cat == 1) { Alias& a = r.aliases[MS.cur]; MS.typeSel = (int)a.type; MS.en = a.enabled; }
    else if (MS.cat == 2) { Timer& t = r.timers[MS.cur]; MS.en = t.enabled; MS.rep = t.repeat; }
}

void macrosLoadEdits() {
    Form& F = gMacrosForm;
    for (int id : {E_NAME, E_PAT, E_CMDS, E_SECS}) F.setEdit(id, "");
    if (!MS.eng || MS.cur < 0 || MS.cur >= (int)macrosCount()) return;
    RuleEngine& r = *MS.eng;
    if (MS.cat == 0) { Trigger& t = r.triggers[MS.cur]; F.setEdit(E_NAME, t.name); F.setEdit(E_PAT, t.pattern); F.setEdit(E_CMDS, t.commands); }
    else if (MS.cat == 1) { Alias& a = r.aliases[MS.cur]; F.setEdit(E_NAME, a.name); F.setEdit(E_PAT, a.pattern); F.setEdit(E_CMDS, a.commands); }
    else if (MS.cat == 2) { Timer& t = r.timers[MS.cur]; char b[32]; snprintf(b, sizeof b, "%g", t.seconds); F.setEdit(E_NAME, t.name); F.setEdit(E_SECS, b); F.setEdit(E_CMDS, t.commands); }
    else { std::string k = macrosVarKey(MS.cur); F.setEdit(E_NAME, k); F.setEdit(E_CMDS, r.vars[k]); }
}

void macrosUpdateTest() {
    Form& F = gMacrosForm;
    if (MS.cat > 1 || MS.cur < 0 || !MS.eng) { MS.testResult.clear(); return; }
    std::string line = F.getEdit(E_TEST);
    if (line.empty()) { MS.testResult = L"Type a line above to test whether this matches."; return; }
    RuleEngine t; t.vars = MS.eng->vars; std::string msg;
    if (MS.cat == 0) {
        Trigger tr; tr.pattern = F.getEdit(E_PAT); tr.type = (MatchType)MS.typeSel; tr.commands = F.getEdit(E_CMDS); tr.gag = MS.gag; t.triggers.push_back(tr);
        LineResult r = t.matchLine(line);
        if (!t.lastError.empty()) msg = t.lastError;
        else {
            Trigger probe = tr; probe.commands = "x"; RuleEngine p; p.triggers.push_back(probe);
            if (p.matchLine(line).commands.empty()) msg = "No match.";
            else { msg = "MATCH."; for (auto& c : r.commands) msg += "\nSends: " + c; if (tr.commands.empty()) msg += " (no commands set)"; if (r.gag) msg += "\n(the line would be hidden)"; }
        }
    } else {
        Alias a; a.pattern = F.getEdit(E_PAT); a.type = (MatchType)MS.typeSel; a.commands = F.getEdit(E_CMDS); t.aliases.push_back(a);
        auto acts = t.processInput(line);
        if (!t.lastError.empty()) msg = t.lastError;
        else if (acts.size() == 1 && !acts[0].client && acts[0].text == line) msg = "No match (it would be sent as typed).";
        else { msg = "MATCH. Would send:"; for (auto& x : acts) msg += "\n" + std::string(x.client ? "(client) " : "") + x.text; }
    }
    MS.testResult = nl2win(msg);
}

void macrosSelect(int i) {
    macrosStore(); MS.cur = i;
    macrosLoadState(); formRelayout(gMacrosForm); macrosLoadEdits(); macrosUpdateTest(); formRelayout(gMacrosForm);
}

void macrosBuild(Form& F, int Wd) {
    int pad = px(14), y = px(10); bool wide = Wd >= px(640);
    std::wstring tgt = MS.eng ? L"Rules for  " + W(MS.title) + (MS.live ? L"   (live session)" : L"   (saved — used on next connect)") : L"No profile yet — create one in Profiles first.";
    F.add(WK_LABEL, {pad, y, Wd - pad, y + px(24)}, 0, tgt); y += px(32);
    Widget& tb = F.add(WK_TABS, {pad, y, Wd - pad, y + px(36)}, M_TABS); tb.items = {L"Triggers", L"Aliases", L"Timers", L"Variables"}; tb.sel = MS.cat; y += px(48);
    if (!MS.eng) return;
    int listW = wide ? px(240) : Wd - 2 * pad, listH = wide ? px(380) : px(124);
    Widget& ls = F.add(WK_LIST, {pad, y, pad + listW, y + listH}, M_LIST);
    for (size_t i = 0; i < macrosCount(); i++) ls.items.push_back(macrosLabel(i));
    ls.sel = MS.cur;
    int by = y + listH + px(8), bw = (listW - px(8)) / 2;
    { Widget& a = F.add(WK_BUTTON, {pad, by, pad + bw, by + px(34)}, M_ADD, L"+  Add"); a.primary = true; }
    { Widget& d = F.add(WK_BUTTON, {pad + bw + px(8), by, pad + listW, by + px(34)}, M_DEL, L"Delete"); d.danger = true; d.enabled = MS.cur >= 0; }
    int fx, fy, fw;
    if (wide) { fx = pad + listW + px(22); fy = y; fw = Wd - fx - pad; } else { fx = pad; fy = by + px(34) + px(14); fw = Wd - 2 * pad; }
    Flow f(F, fx, fy, fw); f.labelW = px(96);
    if (MS.cur < 0) { F.add(WK_NOTE, {fx, fy + px(8), fx + fw, fy + px(70)}, 0, L"Select an item on the left, or press Add to create one."); return; }
    auto twoChecks = [&](const wchar_t* a, int ida, bool va, const wchar_t* b, int idb, bool vb) {
        int cx = f.x + f.labelW, cw = (f.w - f.labelW) / 2;
        { Widget& c = F.add(WK_CHECK, {cx, f.y, cx + cw, f.y + px(28)}, ida, a); c.on = va; }
        if (b) { Widget& c = F.add(WK_CHECK, {cx + cw, f.y, cx + 2 * cw, f.y + px(28)}, idb, b); c.on = vb; }
        f.y += px(32);
    };
    std::vector<std::wstring> types = {L"Wildcard (*)", L"Regular expression", L"Contains text"};
    if (MS.cat == 0) {
        f.field(L"Name", E_NAME); f.field(L"Pattern", E_PAT, false, 34, true);
        f.select(L"Match type", M_TYPE, types, MS.typeSel);
        f.field(L"Commands", E_CMDS, true, 100, true);
        twoChecks(L"Enabled", M_EN, MS.en, L"Hide the line", M_GAG, MS.gag);
        twoChecks(L"Stop after match", M_STOP, MS.stop, L"Fire once", M_ONCE, MS.once);
        f.gap(4);
        f.select(L"Show in", M_PANE, {L"Main text", L"Chat pane"}, MS.paneSel);
        f.select(L"Highlight", M_COLOR, {L"(none)", L"Red", L"Green", L"Yellow", L"Blue", L"Magenta", L"Cyan", L"White", L"Gray", L"Bright red", L"Bright green", L"Bright yellow", L"Bright blue", L"Bright magenta", L"Bright cyan", L"Bright white"}, MS.colorSel);
    } else if (MS.cat == 1) {
        f.field(L"Name", E_NAME); f.field(L"Pattern", E_PAT, false, 34, true);
        f.select(L"Match type", M_TYPE, types, MS.typeSel);
        f.field(L"Commands", E_CMDS, true, 100, true);
        twoChecks(L"Enabled", M_EN, MS.en, nullptr, 0, false);
    } else if (MS.cat == 2) {
        f.field(L"Name", E_NAME); f.field(L"Every (seconds)", E_SECS);
        f.field(L"Commands", E_CMDS, true, 100, true);
        twoChecks(L"Enabled", M_EN, MS.en, L"Repeat", M_REP, MS.rep);
    } else {
        f.field(L"Variable", E_NAME); f.field(L"Value", E_CMDS, true, 100, true);
    }
    if (MS.cat <= 1) {
        f.section(L"TRY IT");
        f.field(L"Test text", E_TEST);
        { Widget& n = F.add(WK_NOTE, {f.x, f.y, f.x + f.w, f.y + px(72)}, 0, MS.testResult); n.mono = true; f.y += px(76); }
    }
    f.note(L"Wildcard patterns: * matches anything and is captured as $1, $2 ...; $0 is the whole line. Separate commands with ; or new lines. ${name} inserts a variable. #commands work too.", 54);
}

void macrosClick(Form& F, int id, int arg) {
    if (!MS.eng && id != M_TABS) return;
    switch (id) {
    case M_TABS: macrosStore(); MS.cat = arg; MS.cur = -1; F.scroll = 0; F.listScroll[M_LIST] = 0; if (MS.eng && macrosCount() > 0) macrosSelect(0); else { formRelayout(F); macrosLoadEdits(); } break;
    case M_LIST: macrosSelect(arg); break;
    case M_ADD: {
        macrosStore(); RuleEngine& r = *MS.eng;
        if (MS.cat == 0) { Trigger t; t.name = "New trigger"; t.pattern = "*"; r.triggers.push_back(t); }
        else if (MS.cat == 1) { Alias a; a.name = "New alias"; a.pattern = "*"; r.aliases.push_back(a); }
        else if (MS.cat == 2) { Timer t; t.name = "New timer"; r.timers.push_back(t); }
        else { std::string k = "var"; int n = 1; while (r.vars.count(k)) k = "var" + std::to_string(++n); r.vars[k] = ""; MS.cur = (int)std::distance(r.vars.begin(), r.vars.find(k)); macrosMarkDirty(); macrosLoadState(); formRelayout(F); macrosLoadEdits(); formRelayout(F); SetFocus(F.edits[E_NAME]); SendMessageW(F.edits[E_NAME], EM_SETSEL, 0, -1); break; }
        MS.cur = (int)macrosCount() - 1; macrosMarkDirty(); macrosLoadState(); formRelayout(F); macrosLoadEdits(); macrosUpdateTest(); formRelayout(F);
        SetFocus(F.edits[E_NAME]); SendMessageW(F.edits[E_NAME], EM_SETSEL, 0, -1);
        break;
    }
    case M_DEL: {
        if (MS.cur < 0) break;
        Widget* wd = F.find(M_DEL); POINT sp{wd ? wd->r.left : 0, wd ? wd->r.bottom + px(4) : 0}; ClientToScreen(F.wnd, &sp);
        std::vector<MItem> mi(2); mi[0].id = 1; mi[0].text = L"Delete"; mi[1].id = 0; mi[1].text = L"Cancel";
        if (popupMenu(sp, mi, L"Delete this item?") != 1) break;
        RuleEngine& r = *MS.eng; int c = MS.cur; MS.cur = -1;
        if (MS.cat == 0) r.triggers.erase(r.triggers.begin() + c); else if (MS.cat == 1) r.aliases.erase(r.aliases.begin() + c);
        else if (MS.cat == 2) r.timers.erase(r.timers.begin() + c); else { auto it = r.vars.begin(); std::advance(it, c); r.vars.erase(it); }
        macrosMarkDirty();
        if (macrosCount() > 0) macrosSelect(std::min<int>(c, (int)macrosCount() - 1)); else { formRelayout(F); macrosLoadEdits(); }
        break;
    }
    case M_TYPE: MS.typeSel = arg; macrosStore(); macrosUpdateTest(); formRelayout(F); break;
    case M_PANE: MS.paneSel = arg; macrosStore(); formRelayout(F); break;
    case M_COLOR: MS.colorSel = arg; macrosStore(); formRelayout(F); break;
    case M_EN: MS.en = !MS.en; macrosStore(); formRelayout(F); break;
    case M_GAG: MS.gag = !MS.gag; macrosStore(); macrosUpdateTest(); formRelayout(F); break;
    case M_STOP: MS.stop = !MS.stop; macrosStore(); formRelayout(F); break;
    case M_ONCE: MS.once = !MS.once; macrosStore(); formRelayout(F); break;
    case M_REP: MS.rep = !MS.rep; macrosStore(); formRelayout(F); break;
    }
}

void macrosEdit(Form& F, int id) {
    if (id == E_TEST) { macrosUpdateTest(); formRelayout(F); return; }
    macrosStore(); macrosUpdateTest(); formRelayout(F);      // also refreshes the list label
}

void macrosRebind() {
    View* v = activeView(); const Profile* pf = v ? nullptr : pickProfile();
    std::string key = v ? "s" + std::to_string(v->s->id) : pf ? "p" + pf->id : "";
    if (MS.eng && key == MS.boundKey) {                                // already editing this one: just make sure something is shown
        if (gMacrosForm.wnd) { if (MS.cur < 0 && macrosCount() > 0) macrosSelect(0); else formRelayout(gMacrosForm); }
        return;
    }
    macrosFlush();
    MS.boundKey = key;
    if (v) { Session* s = v->s.get(); MS.eng = &s->rules; MS.save = [s] { s->saveAll(); }; MS.title = s->profile.name; MS.live = true; }
    else if (pf) {
        std::string path = dataDir() + "\\rules_" + safeName(pf->id) + ".rules";
        MS.offline = RuleEngine(); MS.offline.deserialize(readFile(path));
        MS.eng = &MS.offline; MS.save = [path] { writeFile(path, MS.offline.serialize()); }; MS.title = pf->name; MS.live = false;
    } else { MS.eng = nullptr; MS.save = nullptr; MS.title.clear(); MS.live = false; }
    MS.cat = 0; MS.cur = -1; MS.testResult.clear();
    if (gMacrosForm.wnd) { gMacrosForm.scroll = 0; if (MS.eng && macrosCount() > 0) macrosSelect(0); else { formRelayout(gMacrosForm); macrosLoadEdits(); } }
}

// ===========================================================================
// Profiles panel: profiles, connection details and saved login
// ===========================================================================
enum { P_LIST = 40, P_NEW, P_DEL, P_SAVE, P_CONNECT, EP_NAME = 50, EP_HOST, EP_PORT, EP_USER, EP_PASS, EP_UP, EP_PP, EP_POST, CK_TLS = 60, CK_AUTO, CK_RECON };
struct ProfilesState { int cur = -1; bool passChanged = false, dirty = false; } PS;
Form gProfilesForm;
const char* kPassPlaceholder = "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2";   // 8 bullets

void profilesFlush() { if (PS.dirty) { saveProfiles(A.profiles); PS.dirty = false; } }

void profilesStore() {
    if (PS.cur < 0 || PS.cur >= (int)A.profiles.size()) return;
    Form& F = gProfilesForm; Profile& p = A.profiles[PS.cur];
    p.name = F.getEdit(EP_NAME); p.host = F.getEdit(EP_HOST); p.port = atoi(F.getEdit(EP_PORT).c_str());
    if (p.port <= 0 || p.port > 65535) p.port = 23;
    p.user = F.getEdit(EP_USER); p.userPrompt = F.getEdit(EP_UP); p.passPrompt = F.getEdit(EP_PP); p.postLogin = F.getEdit(EP_POST);
    if (PS.passChanged) p.setPassword(F.getEdit(EP_PASS));
    PS.dirty = true;
}

void profilesLoadEdits() {
    Form& F = gProfilesForm;
    for (int id : {EP_NAME, EP_HOST, EP_PORT, EP_USER, EP_PASS, EP_UP, EP_PP, EP_POST}) F.setEdit(id, "");
    if (PS.cur < 0 || PS.cur >= (int)A.profiles.size()) return;
    Profile& p = A.profiles[PS.cur]; PS.passChanged = false;
    F.setEdit(EP_NAME, p.name); F.setEdit(EP_HOST, p.host); F.setEdit(EP_PORT, std::to_string(p.port)); F.setEdit(EP_USER, p.user);
    F.setEdit(EP_PASS, p.passEnc.empty() ? "" : kPassPlaceholder); F.setEdit(EP_UP, p.userPrompt); F.setEdit(EP_PP, p.passPrompt); F.setEdit(EP_POST, p.postLogin);
}

void profilesSelect(int i) {
    profilesStore(); PS.cur = (i >= 0 && i < (int)A.profiles.size()) ? i : -1;
    formRelayout(gProfilesForm); profilesLoadEdits(); formRelayout(gProfilesForm);
}

void profilesBuild(Form& F, int Wd) {
    int pad = px(14), y = px(10); bool wide = Wd >= px(640);
    F.add(WK_LABEL, {pad, y, Wd - pad, y + px(24)}, 0, L"Your MUDs. Passwords are encrypted for your Windows account."); y += px(34);
    int listW = wide ? px(240) : Wd - 2 * pad, listH = wide ? px(380) : px(124);
    Widget& ls = F.add(WK_LIST, {pad, y, pad + listW, y + listH}, P_LIST);
    for (auto& p : A.profiles) ls.items.push_back(W(p.name.empty() ? "(unnamed)" : p.name));
    ls.sel = PS.cur;
    int by = y + listH + px(8), bw = (listW - px(8)) / 2;
    { Widget& a = F.add(WK_BUTTON, {pad, by, pad + bw, by + px(34)}, P_NEW, L"+  New"); a.primary = true; }
    { Widget& d = F.add(WK_BUTTON, {pad + bw + px(8), by, pad + listW, by + px(34)}, P_DEL, L"Delete"); d.danger = true; d.enabled = PS.cur >= 0; }
    int fx, fy, fw;
    if (wide) { fx = pad + listW + px(22); fy = y; fw = Wd - fx - pad; } else { fx = pad; fy = by + px(34) + px(14); fw = Wd - 2 * pad; }
    Flow f(F, fx, fy, fw); f.labelW = px(110);
    if (PS.cur < 0 || PS.cur >= (int)A.profiles.size()) { F.add(WK_NOTE, {fx, fy + px(8), fx + fw, fy + px(70)}, 0, A.profiles.empty() ? L"No profiles yet. Press New to add your first MUD." : L"Select a profile on the left, or press New."); return; }
    Profile& p = A.profiles[PS.cur];
    f.section(L"CONNECTION");
    f.field(L"Name", EP_NAME); f.field(L"Host", EP_HOST); f.field(L"Port", EP_PORT, false, 34, false, false, true);
    f.check(L"Use TLS (not supported yet)", CK_TLS, p.tls);
    f.check(L"Reconnect automatically if the connection drops", CK_RECON, p.autoReconnect);
    f.section(L"LOGIN");
    f.check(L"Log in automatically when connected", CK_AUTO, p.autoLogin);
    f.field(L"Username", EP_USER); f.field(L"Password", EP_PASS, false, 34, false, true);
    f.field(L"Name prompt", EP_UP); f.field(L"Password prompt", EP_PP);
    f.field(L"Then send", EP_POST, true, 72, true);
    f.note(L"One command per line, sent after the password.", 22);
    int bw2 = px(120), bx = f.x + f.labelW;
    F.add(WK_BUTTON, {bx, f.y, bx + bw2, f.y + px(38)}, P_SAVE, L"Save");
    { Widget& c = F.add(WK_BUTTON, {bx + bw2 + px(10), f.y, bx + 2 * bw2 + px(10), f.y + px(38)}, P_CONNECT, L"Connect"); c.primary = true; }
    f.y += px(48);
}

void profilesNew() {
    profilesStore();
    Profile p; p.id = newId(); p.name = "New profile"; p.host = "localhost"; A.profiles.push_back(p);
    PS.cur = (int)A.profiles.size() - 1; PS.dirty = true;
    formRelayout(gProfilesForm); profilesLoadEdits(); formRelayout(gProfilesForm); layout();
    if (gProfilesForm.edits.count(EP_NAME)) { SetFocus(gProfilesForm.edits[EP_NAME]); SendMessageW(gProfilesForm.edits[EP_NAME], EM_SETSEL, 0, -1); }
}

void profilesClick(Form& F, int id, int arg) {
    switch (id) {
    case P_LIST: profilesSelect(arg); break;
    case P_NEW: profilesNew(); break;
    case P_DEL: {
        if (PS.cur < 0) break;
        Widget* wd = F.find(P_DEL); POINT sp{wd ? wd->r.left : 0, wd ? wd->r.bottom + px(4) : 0}; ClientToScreen(F.wnd, &sp);
        std::vector<MItem> mi(2); mi[0].id = 1; mi[0].text = L"Delete"; mi[1].id = 0; mi[1].text = L"Cancel";
        if (popupMenu(sp, mi, L"Delete this profile?\nIts rules and maps stay on disk.") != 1) break;
        A.profiles.erase(A.profiles.begin() + PS.cur); PS.cur = A.profiles.empty() ? -1 : std::min<int>(PS.cur, (int)A.profiles.size() - 1);
        PS.dirty = true; profilesFlush(); formRelayout(F); profilesLoadEdits(); formRelayout(F); layout();
        break;
    }
    case CK_TLS: case CK_AUTO: case CK_RECON: {
        if (PS.cur < 0) break; Profile& p = A.profiles[PS.cur];
        if (id == CK_TLS) p.tls = !p.tls; else if (id == CK_AUTO) p.autoLogin = !p.autoLogin; else p.autoReconnect = !p.autoReconnect;
        PS.dirty = true; formRelayout(F); break;
    }
    case P_SAVE: profilesStore(); profilesFlush(); layout(); showToast(L"Profile saved."); break;
    case P_CONNECT: {
        profilesStore();
        if (PS.cur < 0) break;
        if (A.profiles[PS.cur].host.empty()) { showToast(L"Enter a host name first."); break; }
        profilesFlush(); Profile p = A.profiles[PS.cur]; layout();
        closePanel(PN_PROFILES); openSession(p);
        break;
    }
    }
}

void profilesEdit(Form& F, int id) {
    if (id == EP_PASS) PS.passChanged = true;
    profilesStore();
    if (id == EP_NAME) { InvalidateRect(A.wnd, &A.rSidebar, FALSE); formRelayout(F); }      // keep the list label in sync
}

void profilesFocus(Form& F, int id, bool gained) {
    if (id != EP_PASS || PS.cur < 0) return;
    Profile& p = A.profiles[PS.cur];
    if (gained && !PS.passChanged && !p.passEnc.empty()) F.setEdit(EP_PASS, "");
    else if (!gained && !PS.passChanged && !p.passEnc.empty()) F.setEdit(EP_PASS, kPassPlaceholder);
}

void profilesRefresh() {
    if (PS.cur >= (int)A.profiles.size()) PS.cur = -1;
    if (PS.cur < 0 && !A.profiles.empty()) { const Profile* pf = pickProfile(); PS.cur = pf ? (int)(pf - &A.profiles[0]) : 0; }
    if (gProfilesForm.wnd) { formRelayout(gProfilesForm); profilesLoadEdits(); formRelayout(gProfilesForm); }
}

int panelHit(Panel& p, POINT pt) {
    for (int i = (int)p.hots.size() - 1; i >= 0; i--) if (PtInRect(&p.hots[i].r, pt)) return i;
    return -1;
}

LRESULT CALLBACK panelProc(HWND hw, UINT m, WPARAM w, LPARAM l) {
    Panel* p = (Panel*)GetWindowLongPtrW(hw, GWLP_USERDATA);
    if (m == WM_NCCREATE) { p = (Panel*)((CREATESTRUCT*)l)->lpCreateParams; SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)p); }
    if (!p) return DefWindowProcW(hw, m, w, l);
    int k = panelIndex(p);
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paintPanel(hw, *p); return 0;
    case WM_SIZE:
        if (p->formWnd) MoveWindow(p->formWnd, 0, px(52), LOWORD(l), std::max<int>(10, (int)HIWORD(l) - px(52)), TRUE);
        InvalidateRect(hw, nullptr, FALSE); return 0;
    case WM_MOUSEMOVE: {
        POINT pt{(short)LOWORD(l), (short)HIWORD(l)}; int i = panelHit(*p, pt);
        if (i != p->hover) { p->hover = i; InvalidateRect(hw, nullptr, FALSE); }
        if (i >= 0) { TRACKMOUSEEVENT te{sizeof te, TME_LEAVE, hw, 0}; TrackMouseEvent(&te); }
        return 0;
    }
    case WM_MOUSELEAVE: if (p->hover != -1) { p->hover = -1; InvalidateRect(hw, nullptr, FALSE); } return 0;
    case WM_LBUTTONDOWN: SetFocus(hw); { POINT pt{(short)LOWORD(l), (short)HIWORD(l)}; p->down = panelHit(*p, pt); } return 0;
    case WM_LBUTTONUP: {
        POINT pt{(short)LOWORD(l), (short)HIWORD(l)}; int i = panelHit(*p, pt), d = p->down; p->down = -1;
        if (i < 0 || i != d) return 0;
        int cmd = p->hots[i].cmd;
        if (cmd == PC_CLOSE) closePanel(k);
        else if (cmd == PC_POP) { if (p->popped) dockPanel(k); else popOutPanel(k); }
        else if (cmd == PC_EXPAND) togglePanelFull(k);
        else { doCommand(cmd); if (gPanels[k].wnd) InvalidateRect(gPanels[k].wnd, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1; RECT rc; GetClientRect(hw, &rc);
        int maxScroll = std::max(0, p->contentH - ((int)rc.bottom - px(52)));
        p->scroll = std::max(0, std::min(maxScroll, p->scroll - d * px(48)));
        InvalidateRect(hw, nullptr, FALSE); return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_F11 || (w == VK_ESCAPE && (p->popped ? p->full : A.dockFull))) { togglePanelFull(k); return 0; }
        break;
    case WM_CHAR:
        if (w >= 32 && !p->popped && A.input) { SetFocus(A.input); SendMessageW(A.input, WM_CHAR, w, l); return 0; }
        break;
    }
    return DefWindowProcW(hw, m, w, l);
}

LRESULT CALLBACK panelPopProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Panel* p = (Panel*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) { p = (Panel*)((CREATESTRUCT*)l)->lpCreateParams; SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p); }
    if (!p) return DefWindowProcW(h, m, w, l);
    int k = panelIndex(p);
    switch (m) {
    case WM_SIZE: layoutPanelPop(k); InvalidateRect(h, nullptr, TRUE); if (p->wnd) RedrawWindow(p->wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC dc = CreateCompatibleDC(wdc); HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(dc, bmp);
        const Theme& t = theme();
        HBRUSH wb = CreateSolidBrush(p->full ? t.panel : t.win); FillRect(dc, &rc, wb); DeleteObject(wb);
        if (!p->full) { RECT panel = {px(10), px(10), rc.right - px(10), rc.bottom - px(10)}; rrect(dc, panel, px(14), t.panel, t.border); }
        BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob); DeleteObject(bmp); DeleteDC(dc); EndPaint(h, &ps); return 0;
    }
    case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize = {px(300), px(260)}; return 0;
    case WM_EXITSIZEMOVE: savePanelState(); return 0;
    case WM_KEYDOWN: if (w == VK_F11 || (w == VK_ESCAPE && p->full)) { togglePanelFull(k); return 0; } break;
    case WM_CLOSE: closePanel(k); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void ensurePanelWindow(int k) {
    Panel& p = gPanels[k]; if (p.wnd) return;
    static bool reg = false;
    if (!reg) {
        HINSTANCE hi = GetModuleHandle(nullptr);
        WNDCLASSW wc{}; wc.lpfnWndProc = panelProc; wc.hInstance = hi; wc.lpszClassName = L"McPanel"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.style = CS_HREDRAW | CS_VREDRAW; RegisterClassW(&wc);
        WNDCLASSW pc{}; pc.lpfnWndProc = panelPopProc; pc.hInstance = hi; pc.lpszClassName = L"McPanelPop"; pc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        pc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1)); pc.style = CS_HREDRAW | CS_VREDRAW; RegisterClassW(&pc);
        reg = true;
    }
    p.wnd = CreateWindowExW(0, L"McPanel", p.title, WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, A.wnd, nullptr, GetModuleHandle(nullptr), &p);
    if (k == PN_MACROS) {
        gMacrosForm.build = macrosBuild; gMacrosForm.onClick = macrosClick; gMacrosForm.onEdit = macrosEdit;
        p.formWnd = createFormWindow(p.wnd, &gMacrosForm);
    } else if (k == PN_PROFILES) {
        gProfilesForm.build = profilesBuild; gProfilesForm.onClick = profilesClick; gProfilesForm.onEdit = profilesEdit; gProfilesForm.onFocus = profilesFocus;
        p.formWnd = createFormWindow(p.wnd, &gProfilesForm);
    }
}

void layoutPanelPop(int k) {
    Panel& p = gPanels[k]; if (!p.pop || !p.wnd) return;
    RECT cr; GetClientRect(p.pop, &cr);
    int inset = p.full ? 0 : px(18);
    MoveWindow(p.wnd, inset, inset, std::max<int>(10, (int)cr.right - 2 * inset), std::max<int>(10, (int)cr.bottom - 2 * inset), TRUE);
}

void savePanelState() {
    std::string s;
    for (int k = 0; k < PN_COUNT; k++) {
        Panel& p = gPanels[k];
        if (p.pop && !p.full) { RECT r; GetWindowRect(p.pop, &r); p.x = r.left; p.y = r.top; p.w = r.right - r.left; p.h = r.bottom - r.top; }
        char buf[160]; snprintf(buf, sizeof buf, "%s=%d,%d,%d,%d,%d;", p.key, (p.open && p.popped) ? 1 : 0, p.x, p.y, p.w, p.h); s += buf;
    }
    A.settings.panelState = s; saveSettings(A.settings);
}

void loadPanelState() {
    std::string s = A.settings.panelState; size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find(';', pos); if (e == std::string::npos) e = s.size();
        std::string item = s.substr(pos, e - pos); pos = e + 1;
        size_t eq = item.find('='); if (eq == std::string::npos) continue;
        std::string key = item.substr(0, eq); int pop, x, y, w, h;
        if (sscanf(item.c_str() + eq + 1, "%d,%d,%d,%d,%d", &pop, &x, &y, &w, &h) != 5) continue;
        for (auto& p : gPanels) if (key == p.key) { p.wantPop = pop == 1 && !isWindowOnly((int)(&p - gPanels)); p.x = x; p.y = y; p.w = std::max(300, w); p.h = std::max(260, h); }
    }
}

void openPanel(int k) {
    ensurePanelWindow(k); Panel& p = gPanels[k]; p.open = true;
    if (isWindowOnly(k) && !p.popped) popOutPanel(k);                       // Settings and Macros always get their own window
    if (p.popped && p.pop) { ShowWindow(p.pop, SW_SHOW); SetForegroundWindow(p.pop); }
    else { A.dockTab = k + 1; if (isFormPanel(k)) A.dockFull = true; }     // forms are roomy: dock them expanded by default
    layout();
    if (k == PN_MACROS) macrosRebind();
    if (k == PN_PROFILES) profilesRefresh();
    if (p.formWnd) { RECT cr; GetClientRect(p.wnd, &cr); SendMessageW(p.wnd, WM_SIZE, 0, MAKELPARAM(cr.right, cr.bottom)); }
    if (p.wnd) InvalidateRect(p.wnd, nullptr, FALSE);
}

void dockPanel(int k) {
    Panel& p = gPanels[k]; if (!p.popped) return;
    if (p.full && p.pop) { p.full = false; SetWindowLongPtrW(p.pop, GWL_STYLE, p.savedStyle); }
    savePanelState();
    SetParent(p.wnd, A.wnd);
    HWND pop = p.pop; p.pop = nullptr; p.popped = false; p.full = false;
    DestroyWindow(pop);
    p.open = true; A.dockTab = k + 1;
    savePanelState(); layout();
    if (A.input) SetFocus(A.input);
}

void closePanel(int k) {
    Panel& p = gPanels[k]; if (!p.open && !p.popped) return;
    if (k == PN_MACROS) macrosFlush();
    if (k == PN_PROFILES) { profilesStore(); profilesFlush(); }
    if (isFormPanel(k) && !p.popped) A.dockFull = false;
    if (p.popped) {
        if (p.full && p.pop) { p.full = false; SetWindowLongPtrW(p.pop, GWL_STYLE, p.savedStyle); }
        savePanelState();
        SetParent(p.wnd, A.wnd); HWND pop = p.pop; p.pop = nullptr; p.popped = false; p.full = false; DestroyWindow(pop);
    }
    p.open = false; if (p.wnd) ShowWindow(p.wnd, SW_HIDE);
    savePanelState(); layout();
    if (A.input) SetFocus(A.input);
}

void togglePanel(int k) { if (gPanels[k].open) closePanel(k); else openPanel(k); }

void popOutPanel(int k) {
    ensurePanelWindow(k); Panel& p = gPanels[k]; if (p.popped) return;
    if (A.dockFull && A.dockTab == k + 1) A.dockFull = false;
    RECT mw; GetWindowRect(A.wnd, &mw);
    if (k == PN_SETTINGS && (p.x == -1 || p.w < 640 || p.h > p.w)) { p.w = 920; p.h = 650; }          // landscape: section menu on the left, content on the right
    if (isFormPanel(k) && p.x == -1) { p.w = 900; p.h = 700; }                 // form panels need more room when popped out
    int w = std::max(px(320), p.w), hh = std::max(px(300), p.h), x = p.x, y = p.y;
    RECT want = {x, y, x + w, y + hh};
    if (x == -1 || !MonitorFromRect(&want, MONITOR_DEFAULTTONULL)) { x = mw.right - w - px(40) - k * px(30); y = mw.top + px(80) + k * px(30); }
    p.pop = CreateWindowExW(0, L"McPanelPop", p.title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, hh, A.wnd, nullptr, GetModuleHandle(nullptr), &p);
    p.popped = true; p.open = true;
    applyDwmTo(p.pop);
    SetParent(p.wnd, p.pop);
    layoutPanelPop(k);
    ShowWindow(p.wnd, SW_SHOW); ShowWindow(p.pop, SW_SHOWNOACTIVATE);
    savePanelState(); layout();
    InvalidateRect(p.wnd, nullptr, FALSE);
}

// Docked: the dock expands over the game text. Popped out: borderless fullscreen on its monitor (F11 / Esc toggle).
void togglePanelFull(int k) {
    Panel& p = gPanels[k];
    if (p.popped && p.pop) {
        HWND w = p.pop;
        if (!p.full) {
            p.savedStyle = GetWindowLongPtrW(w, GWL_STYLE); GetWindowRect(w, &p.savedRect);
            MONITORINFO mi{sizeof mi}; GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi);
            p.full = true;
            SetWindowLongPtrW(w, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
            SetWindowPos(w, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        } else {
            p.full = false; SetWindowLongPtrW(w, GWL_STYLE, p.savedStyle); RECT r = p.savedRect;
            SetWindowPos(w, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        layoutPanelPop(k); InvalidateRect(w, nullptr, TRUE);
    } else { A.dockTab = k + 1; A.dockFull = !A.dockFull; layout(); }
    if (p.wnd) { InvalidateRect(p.wnd, nullptr, FALSE); SetFocus(p.wnd); }
}

void applyThemeAll() {
    if (A.inputBrush) DeleteObject(A.inputBrush);
    A.inputBrush = CreateSolidBrush(theme().inputBg);
    for (auto& v : A.views) { rebuildView(*v); darkScroll(v->out); darkScroll(v->chat); }
    applyDwm();
    InvalidateRect(A.wnd, nullptr, FALSE); InvalidateRect(A.input, nullptr, TRUE); InvalidateRect(A.mapwnd, nullptr, TRUE); InvalidateRect(A.vitals, nullptr, TRUE);
}

void doCommand(int id);
void connectDialog(bool addNew = false, int sel = 0) {
    openPanel(PN_PROFILES);
    if (addNew) profilesNew(); else profilesSelect(sel);
}

void activateProfile(int idx) {
    if (idx < 0 || idx >= (int)A.profiles.size()) return;
    const Profile& p = A.profiles[idx];
    for (size_t i = 0; i < A.views.size(); i++) if (A.views[i]->s->profile.id == p.id) {
        selectTab((int)i);
        if (A.views[i]->s->state == Session::Disconnected) A.views[i]->s->connect();
        return;
    }
    openSession(p);
}

void changeZoom(int d) {
    int ns = std::max(7, std::min(28, A.settings.fontSize + d));
    if (ns == A.settings.fontSize) return;
    A.settings.fontSize = ns; saveSettings(A.settings);
    rebuildFont(); for (auto& vw : A.views) rebuildView(*vw); layout();
}

void setSidebarCollapsed(bool c) { A.settings.sidebarCollapsed = c; saveSettings(A.settings); A.hoverId = 0; layout(); }

void onHotClick(int id, int arg) {
    View* v = activeView();
    switch (id) {
    case 1 /*H_PROFILE*/: activateProfile(arg); break;
    case 4 /*H_TAB*/: if (arg != A.active) selectTab(arg); break;
    case 5 /*H_TABCLOSE*/: closeTab(arg); break;
    case 7 /*H_SETTINGS*/: openPanel(PN_SETTINGS); break;
    case 8 /*H_MACROS*/: doCommand(ID_EDIT_RULES); break;
    case 9 /*H_ZOOMOUT*/: changeZoom(-1); break;
    case 10 /*H_ZOOMIN*/: changeZoom(+1); break;
    case 11 /*H_CONN*/:
        if (!v) connectDialog();
        else if (v->s->state == Session::Disconnected) v->s->connect(); else v->s->disconnect();
        break;
    case 12 /*H_SEND*/: submitInput(); SetFocus(A.input); break;
    case 13 /*H_DOCKTAB*/: A.dockTab = arg; layout(); break;
    case 15 /*H_SIDECOLLAPSE*/: setSidebarCollapsed(true); break;
    case 16 /*H_SIDEEXPAND*/: setSidebarCollapsed(false); break;
    case 14 /*H_DOCKTABCLOSE*/: if (arg > 0) togglePanel(arg - 1); break;
    }
}

void doCommand(int id) {
    View* v = activeView();
    switch (id) {
    case ID_FILE_CONNECT: case ID_FILE_PROFILES: connectDialog(); break;
    case ID_FILE_RECONNECT: if (v) { v->s->disconnect(); v->s->connect(); } break;
    case ID_FILE_DISCONNECT: if (v) v->s->disconnect(); break;
    case ID_FILE_CLOSETAB: closeTab(A.active); break;
    case ID_FILE_NEWWIN: { wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{};
        if (CreateProcessW(exe, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); } break; }
    case ID_FILE_EXIT: PostMessageW(A.wnd, WM_CLOSE, 0, 0); break;
    case ID_EDIT_CLEAR: if (v) { v->pending.clear(); v->outLines.clear(); v->chatLines.clear(); rebuildView(*v); } break;
    case ID_EDIT_RULES: openPanel(PN_MACROS); break;
    case ID_EDIT_COPYALL: if (v) { SendMessageW(v->out, EM_SETSEL, 0, -1); SendMessageW(v->out, WM_COPY, 0, 0); SendMessageW(v->out, EM_SETSEL, -1, -1); } break;
    case ID_VIEW_MAP:
        A.mapFull = false; if (A.dockTab == 0) A.dockFull = false;
        if (A.mapPopped) { dockMap(); A.settings.showMap = false; } else A.settings.showMap = !A.settings.showMap;
        saveSettings(A.settings); layout(); break;
    case ID_VIEW_POPMAP: if (A.mapPopped) dockMap(); else popOutMap(); break;
    case ID_VIEW_CHAT: A.settings.showChat = !A.settings.showChat; saveSettings(A.settings); layout(); break;
    case ID_VIEW_LOG: A.settings.logging = !A.settings.logging; saveSettings(A.settings); break;
    case ID_VIEW_ECHO: A.settings.echoInput = !A.settings.echoInput; saveSettings(A.settings); break;
    case ID_VIEW_NUMPAD: A.settings.numpad = !A.settings.numpad; saveSettings(A.settings); break;
    case ID_VIEW_NEXTTAB: if (A.views.size() > 1) selectTab((A.active + 1) % (int)A.views.size()); break;
    case ID_VIEW_PREVTAB: if (A.views.size() > 1) selectTab((A.active + (int)A.views.size() - 1) % (int)A.views.size()); break;
    case ID_SET_SCROLL_UP: A.settings.scrollback = std::min(50000, A.settings.scrollback + 1000); saveSettings(A.settings); break;
    case ID_SET_SCROLL_DOWN: A.settings.scrollback = std::max(1000, A.settings.scrollback - 1000); saveSettings(A.settings); break;
    case ID_SET_WALK_UP: A.settings.walkDelayMs = std::min(1000, A.settings.walkDelayMs + 50); saveSettings(A.settings); break;
    case ID_SET_WALK_DOWN: A.settings.walkDelayMs = std::max(50, A.settings.walkDelayMs - 50); saveSettings(A.settings); break;
    case ID_VIEW_SIDEBAR: setSidebarCollapsed(!A.settings.sidebarCollapsed); break;
    case ID_OPEN_SETTINGS: openPanel(PN_SETTINGS); break;
    case ID_COPY_PRIVACY: {
        std::wstring all = L"Privacy Policy - The Mud Crawler\n\n";
        for (auto& ps : kPrivacy) { all += ps.heading; all += L"\n"; all += ps.body; all += L"\n\n"; }
        copyText(A.wnd, U(all)); showToast(L"Privacy policy copied to the clipboard.");
        break;
    }
    case ID_OPEN_MAIL: ShellExecuteW(nullptr, L"open", (std::wstring(L"mailto:") + kContactEmail + L"?subject=Mud%20Crawler").c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case ID_OPEN_DONATE:
        if (!*kDonateUrl) showToast(L"The donate link isn't set up yet.");
        else ShellExecuteW(nullptr, L"open", kDonateUrl, nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case ID_OPEN_DATA: ShellExecuteW(nullptr, L"open", W(dataDir()).c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case ID_MAP_FULL: toggleMapFull(); break;
    case ID_ZOOM_IN: changeZoom(+1); break;
    case ID_ZOOM_OUT: changeZoom(-1); break;
    case ID_VIEW_FONT: {
        LOGFONTW lf{}; HDC dc = GetDC(nullptr); lf.lfHeight = -MulDiv(A.settings.fontSize, GetDeviceCaps(dc, LOGPIXELSY), 72); ReleaseDC(nullptr, dc);
        wcsncpy(lf.lfFaceName, W(A.settings.fontName).c_str(), LF_FACESIZE - 1);
        CHOOSEFONTW cf{sizeof cf}; cf.hwndOwner = A.wnd; cf.lpLogFont = &lf; cf.Flags = CF_SCREENFONTS | CF_FIXEDPITCHONLY | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL;
        if (ChooseFontW(&cf)) {
            A.settings.fontName = U(lf.lfFaceName); A.settings.fontSize = std::max(7, std::min(28, cf.iPointSize / 10)); saveSettings(A.settings);
            rebuildFont(); for (auto& vw : A.views) rebuildView(*vw); layout();
        }
        break;
    }
    case ID_HELP_COMMANDS: if (v) v->s->userInput("#help"); break;
    case ID_HELP_ABOUT: showAbout(A.wnd); break;
    default:
        if (id >= ID_SET_SEC_BASE && id < ID_SET_SEC_BASE + 7) { gSettingsSec = id - ID_SET_SEC_BASE; gPanels[PN_SETTINGS].scroll = 0; break; }
        if (id >= ID_PROFILE_POPUP_BASE && id < ID_PROFILE_POPUP_BASE + 200) { activateProfile(id - ID_PROFILE_POPUP_BASE); break; }
        if (id >= ID_THEME_BASE && id < ID_THEME_BASE + (int)themes().size()) { A.settings.theme = themes()[id - ID_THEME_BASE].name; saveSettings(A.settings); applyThemeAll(); }
    }
    if (v && id != ID_FILE_CLOSETAB) SetFocus(A.input);
}

bool gDirty = false;

// Right-click a profile: connect / edit / new / delete. Delete asks for confirmation in a themed pop-over.
void profileContext(int idx) {
    std::vector<MItem> mi = {{ID_PROF_CONNECT, L"Connect"}, {ID_PROF_EDIT, L"Edit\u2026"}, {ID_PROF_NEW, L"New profile\u2026"}, {0, L"", L"", true, false, true}, {ID_PROF_DELETE, L"Delete"}};
    POINT p; GetCursorPos(&p);
    int c = popupMenu(p, mi);
    if (c == ID_PROF_CONNECT) activateProfile(idx);
    else if (c == ID_PROF_EDIT) connectDialog(false, idx);
    else if (c == ID_PROF_NEW) connectDialog(true);
    else if (c == ID_PROF_DELETE) {
        std::vector<MItem> ask(2); ask[0].id = 1; ask[0].text = L"Delete"; ask[1].id = 0; ask[1].text = L"Cancel";
        if (popupMenu(p, ask, L"Delete \"" + W(A.profiles[idx].name) + L"\"?\nIts rules and maps stay on disk.") == 1) { A.profiles.erase(A.profiles.begin() + idx); saveProfiles(A.profiles); profilesRefresh(); layout(); }
    }
}

// Right-click empty space in the profile list: add a profile.
void sidebarContext() {
    std::vector<MItem> mi = {{ID_PROF_NEW, L"New profile\u2026"}, {ID_PROF_MANAGE, L"Manage profiles\u2026"}};
    POINT p; GetCursorPos(&p);
    int c = popupMenu(p, mi);
    if (c == ID_PROF_NEW) connectDialog(true);
    else if (c == ID_PROF_MANAGE) connectDialog(false, 0);
}

int hotAt(POINT p, int* arg) {
    for (auto it = A.hots.rbegin(); it != A.hots.rend(); ++it) if (PtInRect(&it->r, p)) { if (arg) *arg = it->arg; return it->id; }
    return 0;
}
// Tab close buttons sit on top of the tab itself; resolve the more specific hot first.
int hotAtPriority(POINT p, int* arg) {
    for (auto& h : A.hots) if ((h.id == A.H_TABCLOSE || h.id == A.H_DOCKTABCLOSE) && PtInRect(&h.r, p)) { *arg = h.arg; return h.id; }
    return hotAt(p, arg);
}

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static int downArg = -1; static bool tracking = false; int& downId = gDownId;
    switch (m) {
    case WM_CREATE: {
        A.wnd = h;
        A.dpi = GetDeviceCaps(GetDC(nullptr), LOGPIXELSX);
        buildChromeFonts();
        A.vitals = CreateWindowExW(0, kVitalsClass, L"", WS_CHILD, 0, 0, 10, 10, h, nullptr, GetModuleHandle(nullptr), nullptr);
        A.input = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 100, 20, h, (HMENU)ID_INPUT, GetModuleHandle(nullptr), nullptr);
        A.mapwnd = CreateWindowExW(0, kMapClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, nullptr, GetModuleHandle(nullptr), nullptr);
        SetWindowSubclass(A.input, inputSub, 1, 0);
        SendMessageW(A.input, EM_LIMITTEXT, 4000, 0);
        rebuildFont();
        A.inputBrush = CreateSolidBrush(theme().inputBg);
        applyDwm();
        return 0;
    }
    case WM_MOUSEWHEEL: {   // deliver the wheel to the window under the mouse, not only the focused control
        POINT p{(short)LOWORD(l), (short)HIWORD(l)}; HWND under = WindowFromPoint(p);
        if (under && under != h && IsChild(h, under)) {
            wchar_t cls[32] = {0}; GetClassNameW(under, cls, 32);
            if (!wcscmp(cls, L"Edit")) { HWND par = GetParent(under); wchar_t pc[32] = {0}; GetClassNameW(par, pc, 32); if (!wcscmp(pc, L"McForm")) under = par; }   // scroll the form, not the box under the pointer
            SendMessageW(under, WM_MOUSEWHEEL, w, l); return 0;
        }
        break;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paintChrome(h); return 0;
    case WM_SIZE: layout(); return 0;
    case WM_SETFOCUS: if (A.input) SetFocus(A.input); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize = {px(900), px(560)}; return 0;
    case WM_CTLCOLOREDIT: if ((HWND)l == A.input) { SetTextColor((HDC)w, theme().text); SetBkColor((HDC)w, theme().inputBg); return (LRESULT)A.inputBrush; } break;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) { POINT p; GetCursorPos(&p); ScreenToClient(h, &p); int a; if (hotAtPriority(p, &a)) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; } }
        break;
    case WM_MOUSEMOVE: {
        POINT p{(short)LOWORD(l), (short)HIWORD(l)}; int arg = -1; int id = hotAtPriority(p, &arg);
        if (id != A.hoverId || arg != A.hoverArg) {
            int oi = A.hoverId, oa = A.hoverArg; A.hoverId = id; A.hoverArg = arg;
            if (oi) invalidateHot(oi, oa);
            if (id) invalidateHot(id, arg);
        }
        if (!tracking) { TRACKMOUSEEVENT te{sizeof te, TME_LEAVE, h, 0}; TrackMouseEvent(&te); tracking = true; }
        return 0;
    }
    case WM_MOUSELEAVE: { tracking = false; int oi = A.hoverId, oa = A.hoverArg; A.hoverId = 0; A.hoverArg = -1; if (oi) invalidateHot(oi, oa); return 0; }
    case WM_LBUTTONDOWN: { POINT p{(short)LOWORD(l), (short)HIWORD(l)}; downId = hotAtPriority(p, &downArg); if (downId) invalidateHot(downId, downArg); return 0; }
    case WM_LBUTTONUP: {
        POINT p{(short)LOWORD(l), (short)HIWORD(l)}; int arg = -1; int id = hotAtPriority(p, &arg);
        if (id && id == downId && arg == downArg) onHotClick(id, arg);
        { int di = downId, da = downArg; downId = 0; if (di) invalidateHot(di, da); } return 0;
    }
    case WM_MBUTTONUP: { POINT p{(short)LOWORD(l), (short)HIWORD(l)}; int arg = -1; if (hotAt(p, &arg) == A.H_TAB) closeTab(arg); return 0; }
    case WM_RBUTTONUP: {
        POINT p{(short)LOWORD(l), (short)HIWORD(l)}; int arg = -1; int id = hotAt(p, &arg);
        if (id == A.H_PROFILE) profileContext(arg);
        else if (id == 0 && PtInRect(&A.rSidebar, p)) sidebarContext();
        else if (id == A.H_TAB) {
            std::vector<MItem> mi = {{1, L"Close tab", L"Ctrl+W"}, {2, L"Reconnect"}};
            POINT sp; GetCursorPos(&sp); int c = popupMenu(sp, mi);
            if (c == 1) closeTab(arg); else if (c == 2) { selectTab(arg); doCommand(ID_FILE_RECONNECT); }
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) >= 1001 && HIWORD(w) <= 1) { doCommand(LOWORD(w)); return 0; }
        break;
    case WM_NET_CONNECTED: case WM_NET_DATA: case WM_NET_CLOSED: {
        View* v = viewById((int)w);
        if (!v) { delete (NetMsg*)l; return 0; }
        v->s->onNetMessage(m, l);
        if (m != WM_NET_DATA) { gDirty = true; if (m == WM_NET_CONNECTED && v == activeView()) reportSize(); }
        return 0;
    }
    case WM_TIMER: {
        if (w == 2) {   // fast timer: draw queued output and repaint only what changed
            for (auto& v : A.views) flushView(*v);
            if (gDirty) {
                gDirty = false;
                View* av = activeView();
                static bool hadVit = false; bool hv = av && av->s->vitals.any();
                static std::string lastSig;
                std::string sig = std::to_string(A.views.size()) + "|" + std::to_string(A.active);
                for (auto& v : A.views) sig += "|" + std::to_string((int)v->s->state) + v->s->profile.name + v->s->statusText;
                if (hv != hadVit) { hadVit = hv; layout(); lastSig = sig; }
                else if (sig != lastSig) {   // connection state / tab titles changed
                    lastSig = sig;
                    InvalidateRect(h, &A.rSidebar, FALSE); InvalidateRect(h, &A.rTabs, FALSE); InvalidateRect(h, &A.rBar, FALSE);
                }
                if (av) {
                    static Vitals lv; static unsigned lastRev = 0; static int lastId = -1;
                    const Vitals& cv = av->s->vitals;
                    if (cv.hp != lv.hp || cv.maxhp != lv.maxhp || cv.mp != lv.mp || cv.maxmp != lv.maxmp || cv.mv != lv.mv || cv.maxmv != lv.maxmv) { lv = cv; InvalidateRect(A.vitals, nullptr, FALSE); }
                    if (av->s->mapRev != lastRev || av->s->id != lastId) { lastRev = av->s->mapRev; lastId = av->s->id; InvalidateRect(A.mapwnd, nullptr, FALSE); updateMapTitle(); }
                }
            }
            return 0;
        }
        ULONGLONG now = GetTickCount64(); double dt = std::min(1.0, (now - A.lastTick) / 1000.0); A.lastTick = now;
        for (auto& v : A.views) v->s->tick(dt);
        if (MS.dirty && GetTickCount64() - MS.dirtyAt > 1500) macrosFlush();                 // autosave edits shortly after typing stops
        return 0;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: macrosFlush(); profilesFlush(); saveMapRect(); savePanelState(); KillTimer(h, 1); KillTimer(h, 2); A.views.clear(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    Gdiplus::GdiplusStartupInput gsi; ULONG_PTR gtok = 0; Gdiplus::GdiplusStartup(&gtok, &gsi, nullptr);
    INITCOMMONCONTROLSEX ic{sizeof ic, ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    LoadLibraryW(L"Msftedit.dll");

    A.settings = loadSettings();
    A.profiles = loadProfiles();
    loadPanelState();

    WNDCLASSW wc{}; wc.lpfnWndProc = wndProc; wc.hInstance = inst; wc.lpszClassName = L"McMain"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; wc.style = CS_HREDRAW | CS_VREDRAW; wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1)); RegisterClassW(&wc);
    WNDCLASSW mc_{}; mc_.lpfnWndProc = mapProc; mc_.hInstance = inst; mc_.lpszClassName = kMapClass; mc_.hCursor = LoadCursor(nullptr, IDC_CROSS); mc_.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW; RegisterClassW(&mc_);
    WNDCLASSW vc{}; vc.lpfnWndProc = vitalsProc; vc.hInstance = inst; vc.lpszClassName = kVitalsClass; vc.hCursor = LoadCursor(nullptr, IDC_ARROW); vc.style = CS_HREDRAW | CS_VREDRAW; RegisterClassW(&vc);

    A.hooks.line = [](Session* s, const std::vector<Run>& runs, const std::string& pane, bool partial) {
        View* v = viewFor(s); if (!v) return;
        bool chat = pane == "chat" && A.settings.showChat;
        if (pane == "chat" && !chat) { queueLine(*v, false, runs, false); return; }
        queueLine(*v, chat, runs, partial);
    };
    A.hooks.status = [](Session*) { gDirty = true; };
    A.hooks.passwordMode = [](Session* s, bool on) { if (viewFor(s) == activeView()) setPasswordMode(on); };
    A.hooks.clear = [](Session* s) { View* v = viewFor(s); if (v) { v->pending.clear(); v->outLines.clear(); v->chatLines.clear(); rebuildView(*v); } };

    int cw = 1280, ch = 820;
    HWND hw = CreateWindowExW(0, L"McMain", L"The Mud Crawler", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, cw, ch, nullptr, nullptr, inst, nullptr);
    ACCEL ac[] = {
        {FCONTROL | FVIRTKEY, 'O', ID_FILE_CONNECT}, {FCONTROL | FVIRTKEY, 'P', ID_FILE_PROFILES}, {FCONTROL | FVIRTKEY, 'W', ID_FILE_CLOSETAB},
        {FCONTROL | FVIRTKEY, 'B', ID_VIEW_SIDEBAR}, {FCONTROL | FVIRTKEY, 'N', ID_FILE_NEWWIN}, {FCONTROL | FVIRTKEY, 'L', ID_EDIT_CLEAR},
        {FCONTROL | FVIRTKEY, VK_TAB, ID_VIEW_NEXTTAB}, {FCONTROL | FSHIFT | FVIRTKEY, VK_TAB, ID_VIEW_PREVTAB}, {FVIRTKEY, VK_F1, ID_HELP_COMMANDS},
    };
    A.accel = CreateAcceleratorTableW(ac, (int)(sizeof ac / sizeof ac[0]));
    ShowWindow(hw, show); UpdateWindow(hw);
    A.lastTick = GetTickCount64(); SetTimer(hw, 1, 100, nullptr); SetTimer(hw, 2, 30, nullptr);

    // command line: --connect <profile name> | --host H --port P [--name N] | --theme T
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string cName, host, name; int port = 23; bool any = false;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i]; auto next = [&]() { return i + 1 < argc ? U(argv[++i]) : std::string(); };
        if (a == L"--connect") { cName = next(); any = true; } else if (a == L"--host") { host = next(); any = true; }
        else if (a == L"--port") port = atoi(next().c_str()); else if (a == L"--name") name = next();
        else if (a == L"--theme") { A.settings.theme = next(); applyThemeAll(); }
    }
    LocalFree(argv);
    if (!cName.empty()) {
        bool found = false;
        for (auto& p : A.profiles) if (p.name == cName) { openSession(p); found = true; break; }
        if (!found) showToast(L"No profile named \"" + W(cName) + L"\".");
    } else if (!host.empty()) {
        Profile p; p.id = "adhoc_" + safeName(host) + "_" + std::to_string(port); p.name = name.empty() ? host : name; p.host = host; p.port = port;
        openSession(p);
    }
    if (!any && A.profiles.empty()) connectDialog(true);   // first run: go straight to creating a profile
    layout();
    if (A.settings.mapPopped && A.settings.showMap) popOutMap();
    for (int k = 0; k < PN_COUNT; k++) if (gPanels[k].wantPop) { openPanel(k); popOutPanel(k); }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (TranslateAcceleratorW(hw, A.accel, &msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    Gdiplus::GdiplusShutdown(gtok);
    WSACleanup();
    return (int)msg.wParam;
}
