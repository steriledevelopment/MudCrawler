# Project notes for Claude (read this first in a new session)

**The Mud Crawler** - native Windows MUD client, C++17/Win32, v1.0.0, by Sterile Development (steriledevelopment@gmail.com).
Repo: https://github.com/steriledevelopment/MudCrawler (public, source only). Build steps: see BUILDING.md.

## Names (keep exact)
Product "The Mud Crawler"; exe `MudCrawler.exe`; user data `%APPDATA%\TheMudCrawler`; installs to `C:\Program Files\Sterile\TheMudCrawler`.
Never write "MudCrawler2" / "Mud Crawler 2". Publisher name on the Store: SterileDevelopment (no space).

## State (2026-11-21)
- Done: app, themed UI (Stone & Gold), auto-mapper, rules, profiles with DPAPI logins, NSIS installer, Store packaging, privacy page.
- **TLS is NOT in this repo yet.** The profile checkbox still says "not supported yet". A working version was written and tested
  (under Wine) but only lives in the owner's git bundle. Design if it must be redone: Windows Schannel (new `src/tls.*`, link
  `-lsecur32`); handshake with manual validation, then verify the chain/host name ourselves; for an untrusted certificate the network
  thread posts `WM_NET_CERT` and waits while the UI shows a themed popup: "Trust this time only" / "Always trust this certificate" /
  "Cancel"; "always" stores the certificate's SHA-256 fingerprint in the profile (`trustedCerts`, `trustedCert=` lines in profiles.ini).
  Hold typed input until the handshake finishes. Add "supports TLS" to the Store listing afterwards.
- Pending on the owner's side: Microsoft Store submission and certification; new PayPal link (`kDonateUrl` in src/main.cpp).

## Owner's standing preferences
- Run Debian commands without asking. Do NOT push, delete repos, or publish without being asked; never touch the public repo's
  privacy page (the Store listing links to it).
- Never use `pkill -f` (it kills your own shell); use `pkill -x` or `setsid nohup`.
- Deliver files with SendUserFile as `.bin` (the owner renames them); direct .exe/.zip downloads failed.
- The online map is integral to the product: any world/map change must keep it working.
- Use the owner's identity for commits: `git -c user.name="Sterile Development" -c user.email="steriledevelopment@gmail.com" commit`.
- Keep the owner's personal email and Store identity values out of the repo (`store/identity.env` is gitignored; values are in Partner Center).
- Unsigned builds trigger SmartScreen/"Unknown publisher"; the Store signs the MSIX.
