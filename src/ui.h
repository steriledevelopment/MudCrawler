// Shared Win32 UI helpers, themes and dialog entry points.
#pragma once
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <string>
#include <vector>
#include <functional>
#include "ansi.h"
#include "session.h"
#include "store.h"

namespace mc {

inline std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w;
}
inline std::string U(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
inline std::string getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring w(n + 1, 0); GetWindowTextW(h, &w[0], n + 1); w.resize(n); return U(w);
}
inline void setText(HWND h, const std::string& s) { SetWindowTextW(h, W(s).c_str()); }

struct Theme {
    const char* name;
    COLORREF bg, fg, fgBright;                       // output area
    COLORREF win, panel, panelSel, border, inputBg;  // chrome
    COLORREF text, muted, accent, accentFill, dangerFill;
    COLORREF pal[16];
    bool light() const { return (GetRValue(bg) * 299 + GetGValue(bg) * 587 + GetBValue(bg) * 114) / 1000 > 128; }
};
const std::vector<Theme>& themes();
const Theme& findTheme(const std::string& name);

inline COLORREF rgb(uint32_t v) { return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); }
// Resolve a style colour (palette index, packed RGB or default -1) for a theme.
COLORREF resolveColor(int32_t c, const Theme& t, bool isFg, bool bold);

HFONT uiFont();    // shared Segoe UI message font

HWND ctl(HWND parent, const wchar_t* cls, const std::wstring& text, DWORD style, int x, int y, int w, int h, int id = 0, DWORD ex = 0);

// Minimal modal dialog framework
struct DlgBase {
    HWND hwnd = nullptr;
    bool done = false;
    virtual ~DlgBase() {}
    virtual LRESULT onMsg(UINT msg, WPARAM w, LPARAM l) { return DefWindowProcW(hwnd, msg, w, l); }
    virtual void onCreate() = 0;
    virtual void onClose() {}   // called when the user closes the window
};
void runModal(HWND owner, DlgBase& d, const std::wstring& title, int w, int h);

// Dialogs (dialogs.cpp)
void showAbout(HWND owner);
// Small text-entry dialog (room notes, renaming). Returns true if the user pressed OK.
bool showTextPrompt(HWND owner, const std::string& title, const std::string& label, std::string& text, bool multiline);

} // namespace mc
