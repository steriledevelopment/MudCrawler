# Microsoft Store listing text - The Mud Crawler

Copy these into Partner Center. Product name to reserve: **The Mud Crawler**. Publisher display name: **SterileDevelopment**.
Privacy policy URL: https://steriledevelopment.github.io/MudCrawler/privacy.html  -  Support email: steriledevelopment@gmail.com
(Written before TLS was added: after TLS ships, add a line such as "Supports encrypted TLS connections".)

## Description (up to 10,000 characters)
The Mud Crawler is a native Windows client for MUDs, the text-based multiplayer worlds. It's fast and themeable, with a built-in auto-mapper, triggers, aliases and timers, and it respects your privacy: no accounts, no ads and no tracking.

**Play MUDs the way they deserve.**
The Mud Crawler is a modern, native Windows client for text-based multiplayer games (MUDs). Connect to any telnet MUD server, and spend your time playing instead of fighting your client.

**Built for speed and comfort**
- Smooth, flicker-free text with full ANSI colour: 16, 256 and truecolor.
- A stone-and-gold look with several themes, adjustable text size and a clean, dockable layout.
- Panels you can pop out into their own windows, including the map.

**Auto-mapper**
- Draws the world as you explore it.
- Click any room to see its details and add your own notes.
- Expand the map to fill the window, or pop it out beside the game.

**Automation, your way**
- Triggers, aliases, timers and variables to script your play.
- Rules are kept per profile, so each character can have its own setup.

**Made for modern MUD servers**
- Supports GMCP, MSDP, MCCP2 (compressed connections), NAWS and MTTS.

**Profiles and logins**
- Save a profile for each game or character.
- Optional auto-login, with saved passwords encrypted by Windows so they stay on your PC.

**Private by design**
- No accounts, no ads, no analytics. The app sends nothing to the developer and connects only to the game servers you choose.

**Free to use.** If you enjoy it, there's a donation link in Settings under About.

## Product features (up to 20 bullets, 200 characters each)
- Native Windows MUD client with fast, flicker-free ANSI colour
- Auto-mapper with clickable room notes and a pop-out map
- Triggers, aliases, timers and variables
- Supports GMCP, MSDP, MCCP2, NAWS and MTTS
- Themeable stone-and-gold interface with dockable panels
- Saved profiles with encrypted logins
- No accounts, ads or tracking

## Search terms / keywords (up to 7, 30 characters each)
MUD, MUD client, telnet, text game, MUSH, roguelike, auto-mapper

## Copyright and trademark info (200 characters)
(c) 2026 Sterile Development. All rights reserved. The Mud Crawler is a trademark of Sterile Development.
Shorter, copyright only: (c) 2026 Sterile Development. All rights reserved.

## Additional license terms (optional; leaving it empty uses Microsoft's standard terms)
The Mud Crawler is provided free of charge for personal use. You may not copy, modify, redistribute, sell or reverse engineer the software except as allowed by law. The software is provided "as is", without warranty of any kind, and Sterile Development is not liable for any damages arising from its use.

The Mud Crawler connects only to the game servers you choose. You are responsible for your use of those servers and for following their rules.

The Mud Crawler includes zlib (https://zlib.net), (c) 1995-2024 Jean-loup Gailly and Mark Adler, used under the zlib license.

Contact: steriledevelopment@gmail.com

## Restricted capability: why runFullTrust? (short answer)
The Mud Crawler is a native Win32 desktop app (C++), not a UWP app, so it can only be packaged for the Store as a full-trust desktop app. It uses runFullTrust to run as a normal Windows process: it opens telnet connections to the MUD servers the user chooses and saves the user's settings locally. It collects no personal data and needs no administrator rights.

## Other submission fields
- Price: free. Category: Games or Utilities.
- Age rating: answer the questionnaire; the app shows text from MUD servers that it cannot filter.
- Screenshots: at least 1366x768.
- Certification notes: the app needs a game server address to connect; no account is required to use it.
- Package: upload the `.msix` made by `store/pack.cmd` (not the zip). The Store signs it.
