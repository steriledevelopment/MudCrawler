#include "ui.h"
namespace mc {

static std::vector<Theme> makeThemes() {
    std::vector<Theme> t;
    // name, bg fg fgBright | win panel panelSel border inputBg | text muted accent accentFill dangerFill | palette
    t.push_back({"Stone & Gold", rgb(0x000000), rgb(0xd8d8d0), rgb(0xffffff),
        rgb(0x15110c), rgb(0x1d1812), rgb(0x2b241a), rgb(0x33291b), rgb(0x120e09),
        rgb(0xe8dec8), rgb(0x968c78), rgb(0xd9ad45), rgb(0x5f5025), rgb(0x4d2222),
        {rgb(0x000000), rgb(0xd84a4a), rgb(0x4fbf5a), rgb(0xc8a040), rgb(0x5a8ee0), rgb(0xb070d0), rgb(0x3fb0c8), rgb(0xc8c8c0),
         rgb(0x6e6e66), rgb(0xf07070), rgb(0x6ee06e), rgb(0xecc86a), rgb(0x7aa8ff), rgb(0xd898f0), rgb(0x4fc3f7), rgb(0xffffff)}});
    t.push_back({"Dark", rgb(0x0c0c0c), rgb(0xcccccc), rgb(0xffffff),
        rgb(0x16171a), rgb(0x202225), rgb(0x2d2f34), rgb(0x34363b), rgb(0x141517),
        rgb(0xdddddd), rgb(0x8a8d93), rgb(0x5aa0ff), rgb(0x264a7d), rgb(0x4d2222),
        {rgb(0x000000), rgb(0xc50f1f), rgb(0x13a10e), rgb(0xc19c00), rgb(0x3b78ff), rgb(0xb4009e), rgb(0x3a96dd), rgb(0xcccccc),
         rgb(0x767676), rgb(0xe74856), rgb(0x16c60c), rgb(0xf9f1a5), rgb(0x6c9bff), rgb(0xe03ccb), rgb(0x61d6d6), rgb(0xf2f2f2)}});
    t.push_back({"Light", rgb(0xfdfdf8), rgb(0x1e1e1e), rgb(0x000000),
        rgb(0xe6e4dd), rgb(0xf2f0ea), rgb(0xe0ddd2), rgb(0xcfcbbd), rgb(0xffffff),
        rgb(0x202020), rgb(0x6a675e), rgb(0xa06000), rgb(0xf0dfb0), rgb(0xf0c8c8),
        {rgb(0x000000), rgb(0xaa0000), rgb(0x007000), rgb(0x8a6500), rgb(0x0000c0), rgb(0x900090), rgb(0x007a85), rgb(0x606060),
         rgb(0x404040), rgb(0xd00000), rgb(0x009000), rgb(0xa07800), rgb(0x2020ff), rgb(0xc000c0), rgb(0x0099a8), rgb(0x000000)}});
    t.push_back({"Solarized", rgb(0x002b36), rgb(0x93a1a1), rgb(0xfdf6e3),
        rgb(0x00212b), rgb(0x073642), rgb(0x0d4553), rgb(0x124a58), rgb(0x00252f),
        rgb(0x93a1a1), rgb(0x657b83), rgb(0xb58900), rgb(0x4a4300), rgb(0x5a2a28),
        {rgb(0x073642), rgb(0xdc322f), rgb(0x859900), rgb(0xb58900), rgb(0x268bd2), rgb(0xd33682), rgb(0x2aa198), rgb(0xeee8d5),
         rgb(0x586e75), rgb(0xcb4b16), rgb(0x8aa400), rgb(0xd7a40a), rgb(0x5aa9e6), rgb(0x6c71c4), rgb(0x93e0d8), rgb(0xfdf6e3)}});
    t.push_back({"Amber", rgb(0x140a00), rgb(0xffb000), rgb(0xffd060),
        rgb(0x0e0700), rgb(0x1a0f00), rgb(0x2a1a00), rgb(0x3a2600), rgb(0x100800),
        rgb(0xffb000), rgb(0x9a6a00), rgb(0xffd060), rgb(0x5a3c00), rgb(0x4d2222),
        {rgb(0x140a00), rgb(0xff6a3d), rgb(0xb5c63a), rgb(0xffc800), rgb(0xff9a5a), rgb(0xff7aa0), rgb(0xffd890), rgb(0xffb000),
         rgb(0x9a6a00), rgb(0xff8a60), rgb(0xd0e050), rgb(0xffe060), rgb(0xffb080), rgb(0xff9ac0), rgb(0xffe8b0), rgb(0xffe8a0)}});
    t.push_back({"Matrix", rgb(0x000a00), rgb(0x00d040), rgb(0x80ff9a),
        rgb(0x000700), rgb(0x001400), rgb(0x002400), rgb(0x003400), rgb(0x000a00),
        rgb(0x00d040), rgb(0x007020), rgb(0x80ff9a), rgb(0x004a18), rgb(0x4d2222),
        {rgb(0x000a00), rgb(0xff5050), rgb(0x00ff41), rgb(0xc8ff50), rgb(0x40d0c0), rgb(0xa0ffc0), rgb(0x00ffc0), rgb(0x00d040),
         rgb(0x007020), rgb(0xff8080), rgb(0x60ff80), rgb(0xe0ff90), rgb(0x70ffe0), rgb(0xd0ffe0), rgb(0x80ffe8), rgb(0xb0ffc0)}});
    return t;
}
const std::vector<Theme>& themes() { static std::vector<Theme> t = makeThemes(); return t; }
const Theme& findTheme(const std::string& name) {
    for (auto& t : themes()) if (name == t.name) return t;
    return themes()[0];
}

COLORREF resolveColor(int32_t c, const Theme& t, bool isFg, bool bold) {
    if (c == -1) return isFg ? (bold ? t.fgBright : t.fg) : t.bg;
    if (c >= 0 && c < 16) { int i = (isFg && bold && c < 8) ? c + 8 : c; return t.pal[i]; }
    return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
}

HFONT uiFont() {
    static HFONT f = [] {
        NONCLIENTMETRICSW m{}; m.cbSize = sizeof m;
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
        return CreateFontIndirectW(&m.lfMessageFont);
    }();
    return f;
}

HWND ctl(HWND parent, const wchar_t* cls, const std::wstring& text, DWORD style, int x, int y, int w, int h, int id, DWORD ex) {
    HWND c = CreateWindowExW(ex, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandle(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)uiFont(), TRUE);
    return c;
}

static LRESULT CALLBACK dlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    DlgBase* d;
    if (m == WM_NCCREATE) { d = (DlgBase*)((CREATESTRUCT*)l)->lpCreateParams; d->hwnd = h; SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)d); }
    d = (DlgBase*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!d) return DefWindowProcW(h, m, w, l);
    if (m == WM_CREATE) { d->onCreate(); return 0; }
    if (m == WM_CLOSE) { d->onClose(); d->done = true; return 0; }
    if (m == WM_CTLCOLORSTATIC) { SetBkMode((HDC)w, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE); }
    return d->onMsg(m, w, l);
}

void runModal(HWND owner, DlgBase& d, const std::wstring& title, int w, int h) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{}; wc.lpfnWndProc = dlgProc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"McDialog";
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(1));
        RegisterClassW(&wc); reg = true;
    }
    RECT r = {0, 0, w, h}; AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);
    RECT o{}; GetWindowRect(owner ? owner : GetDesktopWindow(), &o);
    int ww = r.right - r.left, hh = r.bottom - r.top;
    int x = o.left + ((o.right - o.left) - ww) / 2, y = o.top + ((o.bottom - o.top) - hh) / 2;
    if (owner) EnableWindow(owner, FALSE);
    HWND hw = CreateWindowExW(WS_EX_DLGMODALFRAME, L"McDialog", title.c_str(), WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_CLIPCHILDREN,
                              x, y, ww, hh, owner, nullptr, GetModuleHandle(nullptr), &d);
    MSG msg;
    while (!d.done && GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000) && IsChild(hw, msg.hwnd)) {
            PostMessageW(hw, WM_COMMAND, 1, 0);      // Ctrl+Enter = OK (useful inside multi-line boxes)
            continue;
        }
        if (!IsDialogMessage(hw, &msg)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    }
    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(hw);
    if (owner) SetForegroundWindow(owner);
}

} // namespace mc
