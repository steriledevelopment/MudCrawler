# Building The Mud Crawler

Native Windows (x64) MUD client written in C++17 / Win32. Built on Debian by cross-compiling with mingw-w64.

## Tools (Debian)
    apt install -y g++ make g++-mingw-w64-x86-64-posix libz-mingw-w64-dev nsis zip python3 git gh imagemagick
    # optional, for running the app / installer under Wine for UI checks:
    dpkg --add-architecture i386 && apt update && apt install -y wine wine32:i386 xvfb openbox xdotool wmctrl

## Targets
    make test        # unit tests (core, rules, mapper, json, store) with native g++
    make win         # dist/MudCrawler.exe  (static, x64)
    make installer   # dist/MudCrawler-Setup-1.0.0.exe  (NSIS; see store/installer/README.md)
    bash store/stage.sh   # dist/MudCrawler-store-stage.zip: Microsoft Store packing bundle

## Microsoft Store package
The Store needs an MSIX, which is packed on Windows (MakeAppx from the Windows SDK):
1. Copy `store/identity.env.example` to `store/identity.env` and fill it in from Partner Center
   (Product management > Product identity). This file is gitignored: it is never committed.
2. `make win && bash store/stage.sh`, copy the zip to a Windows PC, unzip, run `pack.cmd`.
3. Upload the resulting `.msix` to the submission's Packages page. The Store signs it.
See `store/STORE.md` for the full walk-through and listing text.

## Privacy policy
Single source: `store/gen-privacy.py`. Run `python3 store/gen-privacy.py` to regenerate `src/privacy.h` (shown in the app),
`store/PRIVACY.md` and `docs/privacy.html` (GitHub Pages: https://steriledevelopment.github.io/MudCrawler/privacy.html).

## Testing the UI under Wine (optional)
    Xvfb :99 &  DISPLAY=:99 openbox &  DISPLAY=:99 WINEPREFIX=/tmp/wp wine dist/MudCrawler.exe
`tests/fake_mud.py <port>` is a small fake MUD server for connecting to.

## Layout
- `src/main.cpp` window, panels, settings, map, themed popups; `session.*` connection + telnet/GMCP/MSDP/MCCP; `ansi.*`, `rules.*`
  (triggers/aliases/timers), `mapper.*`, `store.*` (profiles, settings, DPAPI logins), `json.h`, `ui.*`.
- `build/` icon, version resource, manifest; `store/` Store packaging and `store/installer/` NSIS installer; `docs/` GitHub Pages.
