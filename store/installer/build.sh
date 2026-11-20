#!/bin/bash
# Builds dist/MudCrawler-Setup-<version>.exe with NSIS. Needs: apt install nsis, and `make win` done first.
set -e
cd "$(dirname "$0")"
[ -f ../../dist/MudCrawler.exe ] || { echo "dist/MudCrawler.exe is missing - run: make win"; exit 1; }
makensis -V2 mudcrawler.nsi
ls -la ../../dist/MudCrawler-Setup-*.exe
