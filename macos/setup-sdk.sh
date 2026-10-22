#!/bin/bash
# Downloads the Qt and KDE Frameworks development files that match the Kate.app
# installed on this Mac, from KDE's Craft binary cache.
#
# A plugin has to be built against the same Qt (or an older minor version) as the
# Kate that loads it, so this script reads the versions out of Kate.app and picks a
# Craft cache release that ships exactly those.
#
# Usage: macos/setup-sdk.sh [sdk-dir]
# Environment: KATE_APP (default /Applications/kate.app)

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${1:-$REPO/build-sdk}"
KATE_APP="${KATE_APP:-/Applications/kate.app}"
CACHE="https://files.kde.org/craft/Qt6"

die() { echo "error: $*" >&2; exit 1; }

[ -d "$KATE_APP" ] || die "Kate not found at $KATE_APP (set KATE_APP)"
command -v 7zz >/dev/null || die "7zz not found; run: brew install sevenzip"
command -v python3 >/dev/null || die "python3 not found"

FW="$KATE_APP/Contents/Frameworks"
ARCH="$(lipo -archs "$KATE_APP/Contents/MacOS/kate" | awk '{print $1}')"
QT_VERSION="$(otool -L "$FW/QtCore.framework/Versions/A/QtCore" | tail -n +2 | awk '/QtCore.framework/ {sub(/\)/, "", $NF); print $NF; exit}')"
KF_VERSION="$(otool -L "$FW/libKF6TextEditor.6.dylib" | tail -n +2 | awk '/libKF6TextEditor/ {sub(/\)/, "", $NF); print $NF; exit}')"
echo "Kate.app: $ARCH, Qt $QT_VERSION, KDE Frameworks $KF_VERSION"

mkdir -p "$SDK/downloads" "$SDK/prefix"
cd "$SDK"

# Find a Craft release whose qtbase and ktexteditor match Kate.app, newest first
CHANNELS="$(curl -fsSL "$CACHE/" | grep -o 'href="[0-9][^"/]*/"' | sed 's/href="//;s|/"||' | sort -ruV | uniq)"
FOUND=""
for channel in $CHANNELS; do
    base="$CACHE/$channel/macos/clang/$ARCH/RelWithDebInfo"
    curl -fsSL -o manifest.json "$base/manifest.json" 2>/dev/null || continue
    if python3 -I - "$QT_VERSION" "$KF_VERSION" manifest.json > files.txt <<'EOF'
import json, sys
qt, kf, path = sys.argv[1:]
packages = next(iter(json.load(open(path))["packages"].values()))

def pick(entry, version):
    for build in entry["build"]:
        binary = build["files"].get("Binary")
        if binary and f"-{version}-" in binary["fileName"]:
            return binary["fileName"]
    return None

# Qt modules that the KTextEditor dependency chain asks CMake for
qt_modules = ("/qtbase/", "/qtdeclarative/", "/qtsvg/")

files, have_qt, have_kf = [], False, False
for entry in packages:
    names = [b["files"].get("Binary", {}).get("fileName", "") for b in entry["build"]]
    if any(m in n for n in names for m in qt_modules):
        name = pick(entry, qt)
        have_qt |= bool(name and "/qtbase/" in name)
    elif any("kde/frameworks/" in n for n in names):
        name = pick(entry, kf)
        have_kf |= bool(name and "/ktexteditor/" in name)
    else:
        continue
    if name:
        files.append(name)

if not (have_qt and have_kf):
    sys.exit(1)
print("\n".join(files))
EOF
    then
        FOUND="$base"
        break
    fi
done
[ -n "$FOUND" ] || die "no Craft cache release ships Qt $QT_VERSION with KDE Frameworks $KF_VERSION for $ARCH"
echo "Using $FOUND ($(wc -l < files.txt | tr -d ' ') packages)"

sed "s|^|$FOUND/|" files.txt | (cd downloads && xargs -P 8 -n 1 curl -fsSLO)

for archive in downloads/*.tar.7z; do
    7zz x -so "$archive" 2>/dev/null | tar -xf - -C prefix
done
# Plugins link to the shared libraries in Kate.app; the static QML tooling libraries
# alone would take half a gigabyte
rm -rf downloads prefix/lib/*.a

# The Qt build tools link against ICU, glib and friends, which the cache keeps in
# separate packages. Kate.app bundles all of them, so let the tools borrow those.
for tool in prefix/bin/qtpaths prefix/libexec/moc prefix/libexec/rcc prefix/libexec/uic; do
    [ -f "$tool" ] || continue
    [ -f "$tool.real" ] || mv "$tool" "$tool.real"
    cat > "$tool" <<EOF
#!/bin/sh
export DYLD_LIBRARY_PATH="$FW" DYLD_FRAMEWORK_PATH="$FW"
exec "\$0.real" "\$@"
EOF
    chmod +x "$tool"
done

echo "SDK ready in $SDK/prefix"
