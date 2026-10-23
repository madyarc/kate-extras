#!/bin/bash
# Builds the kate-extras plugins and installs them into Kate.app.
#
# Kate.app is signed with the hardened runtime, which only loads plugins signed by KDE,
# so after copying the plugins in, this script re-signs Kate.app locally (ad hoc). Run it
# again after every Kate update, because an update replaces the app and its signature.
#
# Usage: macos/install.sh            build and install
#        macos/install.sh --uninstall remove the plugins again
# Environment: KATE_APP (default /Applications/kate.app)

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
KATE_APP="${KATE_APP:-/Applications/kate.app}"
SDK="$REPO/build-sdk"
BUILD="$REPO/build-macos"
PLUGINS=(katedeadlinesplugin katespotifyplugin kateresmonplugin katenotebookplugin)
PLUGIN_DIR="$KATE_APP/Contents/PlugIns/kf6/ktexteditor"

die() { echo "error: $*" >&2; exit 1; }

[ -d "$KATE_APP" ] || die "Kate not found at $KATE_APP (set KATE_APP)"
if pgrep -f "$KATE_APP/Contents/MacOS/kate" >/dev/null; then
    echo "note: Kate is running; quit and reopen it afterwards to load the changes"
fi

resign() {
    # Sealing the bundle without --options runtime drops the hardened runtime, so
    # Kate stops insisting that every plugin carries KDE's signature
    codesign --force --sign - "$KATE_APP" ||
        die "could not re-sign $KATE_APP; allow your terminal under System Settings → Privacy & Security → App Management"
    # Gatekeeper would refuse a quarantined app whose signature is no longer KDE's
    xattr -d com.apple.quarantine "$KATE_APP" 2>/dev/null || true
}

if [ "${1:-}" = "--uninstall" ]; then
    for plugin in "${PLUGINS[@]}"; do
        rm -f "$PLUGIN_DIR/$plugin.so"
    done
    resign
    echo "Removed. Reinstall Kate to get KDE's original signature back."
    exit 0
fi

command -v cmake >/dev/null || die "cmake not found; run: brew install cmake ninja sevenzip"
command -v ninja >/dev/null || die "ninja not found; run: brew install cmake ninja sevenzip"

[ -d "$SDK/prefix/lib/cmake/KF6TextEditor" ] || "$REPO/macos/setup-sdk.sh" "$SDK"

cmake -S "$REPO" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$SDK/prefix" \
    -DBUILD_TEXMDPREVIEW=OFF \
    -DAPPLE_SUPPRESS_X11_WARNING=ON >/dev/null
cmake --build "$BUILD"

for plugin in "${PLUGINS[@]}"; do
    cp "$BUILD/bin/$plugin.so" "$PLUGIN_DIR/" ||
        die "could not copy into $KATE_APP; allow your terminal under System Settings → Privacy & Security → App Management"
done

# macOS only shows the "control Spotify" permission prompt to apps that explain why
PLIST="$KATE_APP/Contents/Info.plist"
if ! /usr/libexec/PlistBuddy -c "Print :NSAppleEventsUsageDescription" "$PLIST" >/dev/null 2>&1; then
    /usr/libexec/PlistBuddy -c "Add :NSAppleEventsUsageDescription string 'The Spotify plugin controls the Spotify app.'" "$PLIST"
fi

resign

echo
echo "Installed into $KATE_APP. Restart Kate, then enable the plugins under"
echo "Kate → Preferences… (⌘,) → Plugins."
