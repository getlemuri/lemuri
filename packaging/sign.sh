#!/usr/bin/env bash
# Signs the Linux packages in dist/ for the app's updates: lemuri_*.deb.sig beside each, made with the release
# key (ssh-keygen -Y sign, namespace "file"), whose public part is in backend/package-signers.
#   packaging/sign.sh [key]       key: ~/.ssh/lemuri-packages, or the file in LEMURI_PACKAGE_KEY_FILE
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
key=${1:-${LEMURI_PACKAGE_KEY_FILE:-$HOME/.ssh/lemuri-packages}}
signers=$(mktemp)
trap 'rm -f "$signers"' EXIT
grep -v '^#' "$here/backend/package-signers" > "$signers"
shopt -s nullglob
debs=("$here"/dist/lemuri_*.deb)
[ ${#debs[@]} -gt 0 ] || { echo "No packages in dist/: run packaging/linux/build.sh first." >&2; exit 1; }
for deb in "${debs[@]}"; do
    rm -f "$deb.sig"
    ssh-keygen -q -Y sign -f "$key" -n file "$deb"
    # Checked as the app will: a key the installed copies do not know would make an update nobody can install.
    principal=$(ssh-keygen -Y find-principals -s "$deb.sig" -f "$signers")
    ssh-keygen -q -Y verify -f "$signers" -I "$principal" -n file -s "$deb.sig" < "$deb"
    echo "signed $(basename "$deb")"
done
