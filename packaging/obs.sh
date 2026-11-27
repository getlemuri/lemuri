#!/usr/bin/env bash
# Builds the source package of a release for the openSUSE Build Service, which builds and publishes it for the
# Debian releases of the project (Debian 12 and 13, amd64 and arm64), and commits it there.
#
#   packaging/obs.sh [options]
#     -v VERSION   the release (tag vVERSION); default: the version in CMakeLists.txt
#     -P PROJECT   the OBS project; default: LEMURI_OBS, or home:alredai:lemuri
#     -n N         OBS revision, raised to upload the same release again (default 1)
#     -u           commit to OBS with osc (otherwise only build, in dist/obs)
#
# One source for every Debian release, versioned 0.1.3-1~obs1: newer than the GitHub .deb (0.1.3), older than a
# future Debian 0.1.3-1. OBS signs the repository with the project's key, so the source is not signed here.
set -euo pipefail
cd "$(dirname "$0")/.."

version=$(sed -n 's/^project(lemuri VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
project=${LEMURI_OBS:-home:alredai:lemuri}
rev=1
upload=0
while getopts "v:P:n:u" opt; do
    case $opt in
        v) version=$OPTARG ;;
        P) project=$OPTARG ;;
        n) rev=$OPTARG ;;
        u) upload=1 ;;
        *) sed -n '2,10p' "$0"; exit 2 ;;
    esac
done
git rev-parse -q --verify "refs/tags/v$version" >/dev/null || { echo "No tag v$version" >&2; exit 2; }

out=dist/obs
rm -rf "$out"; mkdir -p "$out"
orig="$out/lemuri_$version.orig.tar.gz"
# The same upstream tarball as the PPA: the tagged tree without debian/.
git archive --format=tar --prefix="lemuri-$version/" "v$version" -- . ':!debian' | gzip -n9 > "$orig"
dir="$out/lemuri-$version"
tar -xzf "$orig" -C "$out"
git archive --format=tar "v$version" debian | tar -x -C "$dir"

full="$version-1~obs$rev"
uid=$(sed -n 's/^ -- \(.*>\)  .*/\1/p' debian/changelog | head -1)
sed -i -e "1s/^lemuri ([^)]*) [a-z-]*;/lemuri ($full) unstable;/" \
    -e "0,/^ -- /s|^ -- .*| -- $uid  $(date -R)|" "$dir/debian/changelog"
(cd "$dir" && dpkg-buildpackage -S -us -uc -d >/dev/null)
echo "built $out/lemuri_$full.dsc"

if [ $upload = 1 ]; then
    co="$out/checkout"
    (cd "$out" && osc checkout -o checkout "$project" lemuri >/dev/null)
    # The package holds only this release: the previous files go.
    find "$co" -maxdepth 1 -type f -exec rm {} +
    cp "$orig" "$out/lemuri_$full.dsc" "$out/lemuri_$full.debian.tar.xz" "$co/"
    (cd "$co" && osc addremove >/dev/null && osc commit -m "Lemuri $version ($full)")
    echo "Committed to $project: https://build.opensuse.org/package/show/$project/lemuri"
fi
