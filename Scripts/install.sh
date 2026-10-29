#!/bin/bash
#
#  install.sh - put the built plugins where the hosts look, and make sure the
#  hosts actually pick up the new build.
#
#  The cache flush is the part that matters. macOS keeps a registry of audio
#  units and Logic keeps a scan cache on top of it; replacing the bundle on
#  disk does not by itself make either let go of the old one - and testing a
#  binary from before the fix looks exactly like the fix not working.
#
#  Since 0.9.1 the plugins go to /Library, for every user of this Mac, not to
#  ~/Library: that folder belongs to root, so the copy runs through sudo and
#  asks for an admin password. A copy left in ~/Library from before is
#  removed - two units of the same ID and the host picks either.
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARTEFACTS="$PROJECT_DIR/build/Kitbox_artefacts/Release"

AU_SOURCE="$ARTEFACTS/AU/Kitbox.component"
VST3_SOURCE="$ARTEFACTS/VST3/Kitbox.vst3"
APP_SOURCE="$ARTEFACTS/Standalone/Kitbox.app"

AU_DEST="/Library/Audio/Plug-Ins/Components"
VST3_DEST="/Library/Audio/Plug-Ins/VST3"
OLD_AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"
OLD_VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
APP_DEST="/Applications"

if [[ ! -d "$VST3_SOURCE" || ! -d "$AU_SOURCE" ]]; then
    echo "No build found at $ARTEFACTS - run Scripts/build.sh first." >&2
    exit 1
fi

# Without the AudioComponents entry macOS does not register the unit, does not
# complain, and the plugin simply never appears in the host.
if ! /usr/libexec/PlistBuddy -c "Print :AudioComponents:0:subtype" \
        "$AU_SOURCE/Contents/Info.plist" >/dev/null 2>&1; then
    echo "The AU's Info.plist has no AudioComponents entry - rebuild from scratch:" >&2
    echo "    rm -rf build/Kitbox_artefacts && Scripts/build.sh" >&2
    exit 1
fi

# Refuse to install a build older than its sources. Verification/ is left out
# on purpose: the harness is not in the plugin.
stale="$(find "$PROJECT_DIR/Source" "$PROJECT_DIR/CMakeLists.txt" -type f \
             -newer "$AU_SOURCE/Contents/MacOS/Kitbox" -print -quit 2>/dev/null || true)"
if [[ -n "$stale" ]]; then
    echo "The build is older than ${stale#"$PROJECT_DIR"/} - run Scripts/build.sh first." >&2
    exit 1
fi

if pgrep -xq "Logic Pro"; then
    echo "Logic Pro is running. Quit it first: it holds the old plugin open and" >&2
    echo "will not rescan while it is." >&2
    exit 1
fi

echo "Installing to /Library needs an admin password:"
sudo mkdir -p "$AU_DEST" "$VST3_DEST"
sudo rm -rf "$AU_DEST/Kitbox.component" "$VST3_DEST/Kitbox.vst3"
sudo cp -R "$AU_SOURCE"   "$AU_DEST/"
sudo cp -R "$VST3_SOURCE" "$VST3_DEST/"
echo "Installed AU and VST3 to $AU_DEST and $VST3_DEST"

if [[ -e "$OLD_AU_DEST/Kitbox.component" || -e "$OLD_VST3_DEST/Kitbox.vst3" ]]; then
    rm -rf "$OLD_AU_DEST/Kitbox.component" "$OLD_VST3_DEST/Kitbox.vst3"
    echo "Removed the old copies from ~/Library/Audio/Plug-Ins"
fi

if [[ -d "$APP_SOURCE" ]]; then
    rm -rf "$APP_DEST/Kitbox.app"
    cp -R "$APP_SOURCE" "$APP_DEST/"
    echo "Installed the standalone app to $APP_DEST"
fi

rm -f "$HOME/Library/Caches/AudioUnitCache/com.apple.audiounits.cache" \
      "$HOME/Library/Preferences/com.apple.audio.InfoHelper.plist"
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 1

echo
echo "Validating the Audio Unit:"
auval -v aumu Ktbx Tzor

echo
echo "What a host will find:"
auval -a 2>/dev/null | grep -i "Ktbx" || echo "  (nothing - the AU did not register)"
