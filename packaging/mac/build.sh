#!/usr/bin/env bash
# The Mac package of Lemuri, to run on a Mac: dist/Lemuri-<version>-mac-<arch>.zip with Lemuri.app inside,
# for the processor of this Mac (Apple Silicon or Intel: Homebrew's libraries are built for one). Inside the app: the
# app and its backend (Contents/MacOS/lemuri and lemuri-core) and the libraries they link (Qt, libvterm, libssh,
# libargon2, OpenSSL, copied by macdeployqt), so nothing has to be installed. The version is the one in CMakeLists.txt.
#
# Needs: the Xcode command line tools and Homebrew with
#     brew install qt libvterm libssh argon2 openssl@3 cmake ninja pkg-config
#
# The app is signed ad hoc, not with an Apple developer certificate: on another Mac, the first time, open it with a
# right click → Open (or: xattr -dr com.apple.quarantine Lemuri.app).
set -euo pipefail
[ "$(uname)" = Darwin ] || { echo "This builds the Mac app: run it on a Mac." >&2; exit 1; }
here=$(cd "$(dirname "$0")/../.." && pwd)
dist=$here/dist
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$dist"
brews="qt libvterm libssh argon2 openssl@3 cmake ninja pkg-config"

for tool in brew cmake ninja pkg-config; do
    command -v "$tool" > /dev/null || { echo "Missing $tool: brew install $brews" >&2; exit 1; }
done
qt=$(brew --prefix qt)
openssl=$(brew --prefix openssl@3)
[ -x "$qt/bin/macdeployqt" ] || { echo "Missing Qt: brew install $brews" >&2; exit 1; }
export PKG_CONFIG_PATH="$openssl/lib/pkgconfig:$(brew --prefix)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
for lib in vterm libssh libargon2; do
    pkg-config --exists "$lib" || { echo "Missing $lib: brew install $brews" >&2; exit 1; }
done

case $(uname -m) in
    arm64) arch=arm64 ;;
    x86_64) arch=x86_64 ;;
    *) echo "Unknown processor $(uname -m)" >&2; exit 1 ;;
esac
version=$(sed -n 's/^project(lemuri VERSION \([0-9.]*\).*/\1/p' "$here/CMakeLists.txt")

echo "== Lemuri.app"
bundle=$work/Lemuri.app
contents=$bundle/Contents
mkdir -p "$contents/MacOS" "$contents/Resources"

echo "== App and backend"
cmake -S "$here" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEMURI_TESTS=OFF \
    -DCMAKE_PREFIX_PATH="$qt;$openssl" -DOPENSSL_ROOT_DIR="$openssl" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 > /dev/null
cmake --build "$work/build"
# The backend beside the app: it looks for lemuri-core in its own folder.
cp "$work/build/lemuri" "$work/build/lemuri-core" "$contents/MacOS/"

# The icon: an iconset from the Linux icons (up to 256 px), made into .icns.
set_dir=$work/lemuri.iconset
mkdir -p "$set_dir"
icons=$here/packaging/icons/hicolor
for s in 16 32 128 256; do
    cp "$icons/${s}x${s}/apps/lemuri.png" "$set_dir/icon_${s}x${s}.png"
done
cp "$icons/32x32/apps/lemuri.png" "$set_dir/icon_16x16@2x.png"
cp "$icons/64x64/apps/lemuri.png" "$set_dir/icon_32x32@2x.png"
cp "$icons/256x256/apps/lemuri.png" "$set_dir/icon_128x128@2x.png"
iconutil -c icns -o "$contents/Resources/lemuri.icns" "$set_dir"

cat > "$contents/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Lemuri</string>
  <key>CFBundleDisplayName</key><string>Lemuri</string>
  <key>CFBundleIdentifier</key><string>io.github.getlemuri.lemuri</string>
  <key>CFBundleVersion</key><string>$version</string>
  <key>CFBundleShortVersionString</key><string>$version</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>lemuri</string>
  <key>CFBundleIconFile</key><string>lemuri</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSPrincipalClass</key><string>NSApplication</string>
</dict>
</plist>
PLIST
cp "$here/LICENSE" "$here/THIRD-PARTY-NOTICES.md" "$contents/Resources/"
cp -R "$here/licenses" "$contents/Resources/"

echo "== The libraries into the app"
"$qt/bin/macdeployqt" "$bundle" -always-overwrite -executable="$contents/MacOS/lemuri-core"
# Nothing may still point at Homebrew: on another Mac it is not there.
for exe in "$contents/MacOS/lemuri" "$contents/MacOS/lemuri-core"; do
    if otool -L "$exe" | grep -q "$(brew --prefix)"; then
        echo "$(basename "$exe") still links Homebrew's libraries:" >&2
        otool -L "$exe" >&2
        exit 1
    fi
done

echo "== Signature (ad hoc)"
# Every executable and library first, inside out, then the app: on Apple Silicon unsigned code does not run.
find "$contents" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \) -print0 | while IFS= read -r -d '' f; do
    file -b "$f" | grep -q Mach-O && codesign --force --sign - --timestamp=none "$f"
done
codesign --force --sign - --timestamp=none "$bundle"
codesign --verify --deep --strict "$bundle"

echo "== Quick check: the backend answers"
# In an empty home: on this Mac the backend must not read (or migrate) the user's own data.
mkdir -p "$work/home"
reply=$(printf '{"op":"hello","id":1}\n' | HOME=$work/home "$contents/MacOS/lemuri-core" 2> /dev/null | head -1)
case $reply in *'"version"'*) echo "$reply" | cut -c1-120 ;; *) echo "The backend did not answer: $reply" >&2; exit 1 ;; esac

zip=$dist/Lemuri-$version-mac-$arch.zip
rm -f "$zip"
ditto -c -k --keepParent "$bundle" "$zip"
ls -la "$zip"
