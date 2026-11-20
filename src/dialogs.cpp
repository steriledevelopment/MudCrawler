#include "ui.h"
#include <algorithm>

namespace mc {

static HWND edit(HWND p, int x, int y, int w, int id, DWORD extra = 0) {
    return ctl(p, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | extra, x, y, w, 23, id, WS_EX_CLIENTEDGE);
}
static HWND button(HWND p, const wchar_t* t, int x, int y, int w, int id, bool def = false) {
    return ctl(p, L"BUTTON", t, WS_TABSTOP | (def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), x, y, w, 26, id);
}
static std::string multilineToStored(const std::string& s) {   // edit controls use \r\n
    std::string r; for (char c : s) if (c != '\r') r.push_back(c); return r;
}
static std::string storedToMultiline(const std::string& s) {
    std::string r; for (char c : s) { if (c == '\n') r += "\r\n"; else r.push_back(c); } return r;
}

struct PromptDlg : DlgBase {
    std::string label; std::string& text; bool multi; bool ok = false; HWND e = nullptr;
    PromptDlg(const std::string& l, std::string& t, bool m) : label(l), text(t), multi(m) {}
    void onCreate() override {
        ctl(hwnd, L"STATIC", W(label), 0, 14, 12, 392, 20);
        e = multi ? ctl(hwnd, L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, 14, 36, 392, 110, 100, WS_EX_CLIENTEDGE)
                  : edit(hwnd, 14, 36, 392, 100);
        setText(e, storedToMultiline(text));
        button(hwnd, L"OK", 228, multi ? 158 : 72, 85, 1, true); button(hwnd, L"Cancel", 321, multi ? 158 : 72, 85, 2);
        SetFocus(e); SendMessageW(e, EM_SETSEL, 0, -1);
    }
    LRESULT onMsg(UINT m, WPARAM w, LPARAM l) override {
        if (m == WM_COMMAND) {
            int id = LOWORD(w);
            if (id == 1) { text = multilineToStored(getText(e)); ok = true; done = true; }
            if (id == 2) done = true;
            return 0;
        }
        return DefWindowProcW(hwnd, m, w, l);
    }
};
bool showTextPrompt(HWND owner, const std::string& title, const std::string& label, std::string& text, bool multiline) {
    PromptDlg d(label, text, multiline);
    runModal(owner, d, W(title), 420, multiline ? 198 : 112);
    return d.ok;
}

void showAbout(HWND owner) {
    MessageBoxW(owner,
        L"Mud Crawler\nVersion 2.0.0-dev.1\n\nA native Windows MUD client written in C++.\n"
        L"Telnet with GMCP, MSDP, MCCP2, NAWS and MTTS, ANSI 16/256/truecolor,\n"
        L"triggers, aliases, timers, variables and an auto-mapper.\n\nType #help in the input line for commands.",
        L"About", MB_OK | MB_ICONINFORMATION);
}

} // namespace mc
