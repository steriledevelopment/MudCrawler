# The Mud Crawler installer (NSIS)

A traditional Windows setup program: asks for administrator approval (UAC), installs to
`C:\Program Files\The Mud Crawler`, offers a Start Menu shortcut (on by default) and a Desktop shortcut (off by default),
and adds an uninstaller plus an Add/Remove Programs entry. The uninstaller asks before deleting the user's own data
(`%APPDATA%\MudCrawler`).

Build (Linux): `sudo apt install nsis`, then `make installer` -> `dist/MudCrawler-Setup-1.0.0.exe`.

Silent install:  `MudCrawler-Setup-1.0.0.exe /S`   (Start Menu only)
                 `MudCrawler-Setup-1.0.0.exe /S /DESKTOP=1 /D=C:\Apps\MudCrawler`   (`/D=` must come last)
Silent uninstall: `"C:\Program Files\The Mud Crawler\Uninstall.exe" /S`

The installer is x64-only (it refuses to run on 32-bit Windows). It is **unsigned**, so Windows shows
"Unknown publisher" at the UAC prompt and SmartScreen may warn until it is signed with a code-signing certificate.
This is separate from the Microsoft Store MSIX package in `store/`, which the Store signs itself.
