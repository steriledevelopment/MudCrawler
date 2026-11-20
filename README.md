# The Mud Crawler

A native Windows MUD client written in C++17 (Win32 API). One small static `.exe`: no runtime needed. An optional installer is included.

## Features
- Telnet with GMCP, MSDP, MCCP2 (zlib), NAWS, MTTS/TTYPE, EOR/GA prompts and password masking
- ANSI 16 / 256 / truecolor, UTF-8, scrollback, optional session logs
- Stone & Gold theme (plus Dark, Light, Solarized, Amber, Matrix), text zoom, a Settings window with sections
- Profiles with saved logins (passwords encrypted with Windows DPAPI), tabs for several connections, auto-reconnect
- Triggers (wildcard / regex / contains, hide, highlight, route to a chat pane), aliases, timers, variables, `#N cmd` repeats
- Auto-mapper from GMCP `Room.Info` or dead reckoning; room info cards with notes and captured descriptions; click to walk
- The map can pop out, expand or go fullscreen; HP/MP/MV bars; numpad walking

## Build
Needs a Linux box with `g++-mingw-w64-x86-64-posix` and `libz-mingw-w64-dev` (and ImageMagick only if you change the artwork).

    make test     # native unit tests (telnet, ansi, rules, mapper, json, store)
    make win      # dist/MudCrawler.exe
    make installer # dist/MudCrawler-Setup-1.0.0.exe (needs the `nsis` package; see store/installer/README.md)
    python3 tests/fake_mud.py 4999 [--noeor]   # local test MUD (GMCP + MCCP + password + 5 rooms)

User data lives in `%APPDATA%\TheMudCrawler`.

## Microsoft Store
`store/` holds the manifest template, logos, scripts and the step-by-step guide (`store/STORE.md`).

## Privacy
The app collects nothing and sends nothing to the developer. Full text: [privacy policy](https://steriledevelopment.github.io/MudCrawler/privacy.html)
(also in the app under Settings > Privacy). The wording is generated from `store/gen-privacy.py`.

## Status
Early development. TLS (encrypted connections) is not implemented yet.

## Licence
Source is published to be read; all rights are reserved. See [LICENSE.txt](LICENSE.txt).
Support development: Settings > About > Donate with PayPal.
