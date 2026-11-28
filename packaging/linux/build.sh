#!/usr/bin/env bash
# The Linux package of Lemuri: dist/lemuri_<version>_<arch>.deb and the same as a .tar.gz, runnable where it
# is unpacked. Inside: the app (bin/lemuri) and its backend (bin/lemuri-core), both native; Qt 6, libvterm, libssh,
# libargon2 and OpenSSL come from the distribution. The version is the one in CMakeLists.txt.
#
#   packaging/linux/build.sh          needs: cmake, ninja, the headers of Qt 6, libvterm, libssh, libargon2 and
#                                     OpenSSL, dpkg-deb, dpkg-shlibdeps and fakeroot
set -euo pipefail
here=$(cd "$(dirname "$0")/../.." && pwd)
dist=$here/dist
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
version=$(sed -n 's/^project(lemuri VERSION \([0-9.]*\).*/\1/p' "$here/CMakeLists.txt")
[ -n "$version" ] || { echo "No version in CMakeLists.txt" >&2; exit 1; }
arch=$(dpkg --print-architecture)

echo "== App and backend"
stage=$work/lemuri
mkdir -p "$stage/bin"
cmake -S "$here" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEMURI_TESTS=OFF > /dev/null
cmake --build "$work/build" --parallel "${JOBS:-4}"
install -m755 -s "$work/build/lemuri" "$stage/bin/lemuri"
install -m755 -s "$work/build/lemuri-core" "$stage/bin/lemuri-core"
# The updates' installer, run as root by pkexec, and the release keys it checks them against (see lemuri-install).
install -m755 "$here/packaging/linux/lemuri-install" "$stage/bin/lemuri-install"
install -Dm644 "$here/backend/package-signers" "$stage/share/package-signers"
# In an empty home: the backend must answer, without reading the data of whoever builds the package.
mkdir -p "$work/home"
reply=$(printf '{"op":"hello","id":1}\n' | HOME=$work/home "$stage/bin/lemuri-core" 2> /dev/null | head -1)
case $reply in *'"version":"v'$version'"'*) ;; *) echo "The backend did not answer: $reply" >&2; exit 1 ;; esac

echo "== Menu entry, icons, licences"
install -Dm644 "$here/packaging/linux/lemuri.desktop" "$stage/share/applications/lemuri.desktop"
for icon in "$here"/packaging/icons/hicolor/*/apps/lemuri.png; do
    size=$(basename "$(dirname "$(dirname "$icon")")")
    install -Dm644 "$icon" "$stage/share/icons/hicolor/$size/apps/lemuri.png"
done
mkdir -p "$stage/share/doc"
cp "$here/LICENSE" "$here/THIRD-PARTY-NOTICES.md" "$stage/share/doc/"
cp -r "$here/licenses" "$stage/share/doc/"

mkdir -p "$dist"
name=lemuri_${version}_$arch
echo "== $name.tar.gz"
tar -C "$work" -czf "$dist/$name.tar.gz" lemuri

echo "== $name.deb"
deb=$work/deb
mkdir -p "$deb/DEBIAN" "$deb/opt" "$deb/usr/bin" "$deb/usr/share"
cp -r "$stage" "$deb/opt/lemuri"
mv "$deb/opt/lemuri/share/applications" "$deb/opt/lemuri/share/icons" "$deb/usr/share/"
ln -s /opt/lemuri/bin/lemuri "$deb/usr/bin/lemuri"
# The menu entry starts the installed app by its full path: another "lemuri" earlier in the PATH (a copy built by
# hand, an old launcher in ~/.local/bin) must not be what the icon opens.
sed -i 's#^Exec=lemuri#Exec=/usr/bin/lemuri#' "$deb/usr/share/applications/lemuri.desktop"
# The password prompt of an update, with its own message (pkexec finds it by the installer's path).
install -Dm644 "$here/packaging/linux/io.github.getlemuri.lemuri.policy" "$deb/usr/share/polkit-1/actions/io.github.getlemuri.lemuri.policy"
# The libraries the app and the backend link, as the distribution names them (Qt 6, libvterm, libssh...).
mkdir -p "$work/shlib/debian"
echo "Source: lemuri" > "$work/shlib/debian/control"
depends=$(cd "$work/shlib" && dpkg-shlibdeps -O "$stage/bin/lemuri" "$stage/bin/lemuri-core" 2> /dev/null | sed -n 's/^shlibs:Depends=//p')
# Built on Ubuntu 24.04, where Qt's libraries carry the t64 suffix (libqt6gui6t64); later releases (Ubuntu 25.04 on,
# Debian 13) dropped it for some of them (libqt6gui6). Each takes the other name as an alternative, or apt refuses
# the package there.
depends=$(echo "$depends" | sed -E 's/(libqt6[a-z0-9]+)t64( \([^)]*\))?/\1t64\2 | \1\2/g')
size=$(du -sk "$deb" | cut -f1)
cat > "$deb/DEBIAN/control" << CONTROL
Package: lemuri
Version: $version
Architecture: $arch
Maintainer: ${LEMURI_MAINTAINER:-Lemuri <noreply@getlemuri.github.io>}
Installed-Size: $size
Depends: $depends
Section: net
Priority: optional
Homepage: https://github.com/getlemuri/lemuri
Replaces: lemuri-qt
Conflicts: lemuri-qt
Provides: lemuri-qt
Description: SSH console for your servers
 Lemuri keeps your servers in folders and opens their shells in tabs and
 splits, watches disks, updates and SSH security, moves files over SFTP and
 shares hosts with your team. Native: Qt 6 and C++.
CONTROL
cat > "$deb/DEBIAN/postinst" << 'POST'
#!/bin/sh
set -e
command -v update-desktop-database > /dev/null && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache > /dev/null && gtk-update-icon-cache -q -t /usr/share/icons/hicolor || true
POST
cp "$deb/DEBIAN/postinst" "$deb/DEBIAN/postrm"
chmod 755 "$deb/DEBIAN/postinst" "$deb/DEBIAN/postrm"
# Writable by the owner only (the umask of the build may have left group write).
chmod -R u+rwX,go+rX,go-w "$deb"
fakeroot dpkg-deb --build -Zxz "$deb" "$dist/$name.deb" > /dev/null
ls -la "$dist/$name.deb" "$dist/$name.tar.gz"
