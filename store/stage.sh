#!/bin/bash
# Builds dist/store-stage/ (exe + manifest + logos) and dist/MudCrawler-store-stage.zip, ready to pack on Windows.
# Needs store/identity.env (copy identity.env.example and fill it in from Partner Center).
set -e
cd "$(dirname "$0")/.."
[ -f store/identity.env ] || { echo "Missing store/identity.env - copy store/identity.env.example and fill in your Partner Center identity."; exit 1; }
. store/identity.env
case "$PUBLISHER" in CN=PUT*|"") echo "identity.env still has the placeholder publisher."; exit 1;; esac
[ -f dist/MudCrawler.exe ] || { echo "dist/MudCrawler.exe not found - run: make win"; exit 1; }
OUT=${STAGE_OUT:-dist/store-stage}
rm -rf "$OUT"; mkdir -p "$OUT/Assets"
cp dist/MudCrawler.exe "$OUT/"
cp store/Assets/Square*.png store/Assets/Wide*.png store/Assets/StoreLogo*.png "$OUT/Assets/"
sed -e "s|{{IDENTITY_NAME}}|$IDENTITY_NAME|g" -e "s|{{PUBLISHER}}|$PUBLISHER|g" \
    -e "s|{{PUBLISHER_DISPLAY_NAME}}|$PUBLISHER_DISPLAY_NAME|g" -e "s|{{DISPLAY_NAME}}|$DISPLAY_NAME|g" \
    store/AppxManifest.xml.template > "$OUT/AppxManifest.xml"
python3 -c "import xml.dom.minidom,sys; xml.dom.minidom.parse('$OUT/AppxManifest.xml'); print('manifest is well-formed XML')"
# transfer bundle: the staged folder + the packing script
TMP=$(mktemp -d); mkdir -p "$TMP/MudCrawler-store"; cp -r "$OUT" "$TMP/MudCrawler-store/stage"; cp store/pack.ps1 store/pack.cmd "$TMP/MudCrawler-store/"
(cd "$TMP" && zip -qr "$OLDPWD/dist/MudCrawler-store-stage.zip" MudCrawler-store); rm -rf "$TMP"
echo "staged: $OUT   bundle: dist/MudCrawler-store-stage.zip"
