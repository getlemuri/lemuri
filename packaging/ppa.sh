#!/usr/bin/env bash
# Builds signed source packages of a release for a Launchpad PPA, one per Ubuntu series, and uploads them.
#
#   packaging/ppa.sh [options] [series...]
#     -v VERSION   the release (tag vVERSION); default: the version in CMakeLists.txt
#     -k KEY       the OpenPGP key that signs (fingerprint); default: LEMURI_PPA_KEY; none: unsigned, a local check
#     -p PPA       ppa:<user>/<name>; default: LEMURI_PPA
#     -n N         PPA revision, raised to upload the same release again (default 1)
#     -b           also build the binary package of the first series here, as a check (unsigned)
#     -u           upload with dput (otherwise only build, in dist/ppa)
#     series       default: noble resolute stonking
#
# Versions are 0.1.3-1~ppa1~noble1: newer than the GitHub .deb (0.1.3), older than a future archive 0.1.3-1.
set -euo pipefail
cd "$(dirname "$0")/.."

version=$(sed -n 's/^project(lemuri VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
key=${LEMURI_PPA_KEY:-}
ppa=${LEMURI_PPA:-}
rev=1
binary=0
upload=0
while getopts "v:k:p:n:bu" opt; do
    case $opt in
        v) version=$OPTARG ;;
        k) key=$OPTARG ;;
        p) ppa=$OPTARG ;;
        n) rev=$OPTARG ;;
        b) binary=1 ;;
        u) upload=1 ;;
        *) sed -n '2,13p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
series=("$@")
[ ${#series[@]} -gt 0 ] || series=(noble resolute stonking)

[ $upload = 0 ] || [ -n "$key" ] || { echo "No key: -k FINGERPRINT or LEMURI_PPA_KEY" >&2; exit 2; }
[ $upload = 0 ] || [ -n "$ppa" ] || { echo "No PPA: -p ppa:user/name or LEMURI_PPA" >&2; exit 2; }
git rev-parse -q --verify "refs/tags/v$version" >/dev/null || { echo "No tag v$version" >&2; exit 2; }

# The uploader is the key's identity: Launchpad mails the build results there.
if [ -n "$key" ]; then
    uid=$(gpg --list-keys --with-colons "$key" | awk -F: '$1 == "uid" { print $10; exit }')
    [ -n "$uid" ] || { echo "Key $key not found" >&2; exit 2; }
    sign=(-k"$key")
else
    uid=$(sed -n 's/^ -- \(.*>\)  .*/\1/p' debian/changelog | head -1)
    sign=(-us -uc)
fi

out=dist/ppa
rm -rf "$out"; mkdir -p "$out"
orig="$out/lemuri_$version.orig.tar.gz"
# The upstream tarball is the tagged tree without debian/, which travels in its own tarball.
git archive --format=tar --prefix="lemuri-$version/" "v$version" -- . ':!debian' | gzip -n9 > "$orig"

first=1
for s in "${series[@]}"; do
    dir="$out/$s/lemuri-$version"
    mkdir -p "$out/$s"
    tar -xzf "$orig" -C "$out/$s"
    git archive --format=tar "v$version" debian | tar -x -C "$dir"
    cp "$orig" "$out/$s/"
    full="$version-1~ppa${rev}~${s}1"
    # The release's entry, renamed for the series and signed off by the uploader.
    sed -i -e "1s/^lemuri ([^)]*) [a-z-]*;/lemuri ($full) $s;/" \
        -e "0,/^ -- /s|^ -- .*| -- $uid  $(date -R)|" "$dir/debian/changelog"
    # The first upload carries the upstream tarball; the others reuse it from the PPA.
    src=-sd; [ $first = 1 ] && src=-sa
    (cd "$dir" && debuild --no-lintian -S $src -d "${sign[@]}")
    if [ $binary = 1 ] && [ $first = 1 ]; then
        (cd "$dir" && dpkg-buildpackage -b -uc -us -d)
    fi
    first=0
    echo "built $out/$s/lemuri_${full}_source.changes"
done

if [ $upload = 1 ]; then
    for s in "${series[@]}"; do
        dput "$ppa" "$out/$s"/lemuri_*_source.changes
    done
    echo "Uploaded to $ppa: the builds show on its Launchpad page in a few minutes."
fi
