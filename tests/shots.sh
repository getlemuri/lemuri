#!/usr/bin/env bash
# Pictures of every page and dialog, without a screen (QT_QPA_PLATFORM=offscreen), in a temporary home with three
# made-up hosts: the user's data is never read. Each one must come out (no crash, no hang); they are kept in
# build/shots/ to be looked at. tests/shots.sh [path/to/lemuri]
set -uo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
app=${1:-$here/build/lemuri}
out=$here/build/shots
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
rm -rf "$out" && mkdir -p "$out" "$work/home/.lemuri"
cat > "$work/home/.lemuri/hosts.json" << 'JSON'
{"folders": ["prod", "prod/web", "lab"], "hosts": [
 {"id": "a1", "name": "web-1", "address": "192.0.2.10", "port": 22, "user": "root", "authMethod": "KEY", "group": "prod/web", "os": "Debian GNU/Linux 12", "note": "", "state": "ONLINE", "received": false},
 {"id": "a2", "name": "db-1", "address": "192.0.2.11", "port": 2222, "user": "admin", "authMethod": "PASSWORD", "group": "prod", "os": "Ubuntu 24.04 LTS", "note": "primary", "state": "OFFLINE", "received": false},
 {"id": "a3", "name": "test-box", "address": "192.0.2.12", "port": 22, "user": "dev", "authMethod": "KEY", "group": "lab", "os": "", "note": "", "state": "UNKNOWN", "received": false}]}
JSON

# name|node|action|local shell
shots=(
    "datacenter|dc||"
    "folder|folder:prod||"
    "host|host:a1||"
    "team|team||"
    "log|log||1"
    "console|console:a3||"
    "edit-host|dc|edit:a2|"
    "new-host|dc|new:lab|"
    "new-folder|dc|folder:prod|"
    "delete-folder|dc|delete:lab|"
    "remove-host|dc|remove:a3|"
    "unlock|dc|unlock|"
    "settings|dc|settings|"
    "export|dc|export|"
    "split|dc|split|1"
    "split-sync|dc|split:sync|1"
    "split-heir|dc|split:heir|1"
    "exit-in-split|dc|exit|1"
    "ghost|dc|ghost|1"
)
failed=0
for s in "${shots[@]}"; do
    IFS='|' read -r name node action local <<< "$s"
    png=$out/$name.png
    env -u XDG_CACHE_HOME -u XDG_CONFIG_HOME HOME="$work/home" \
        QT_QPA_PLATFORM=offscreen LEMURI_SHOT="$png" LEMURI_SHOT_NODE="$node" LEMURI_SHOT_DO="$action" \
        ${local:+LEMURI_SHOT_LOCAL=1} LEMURI_SHOT_MS=4000 timeout 60 "$app" > "$work/$name.log" 2>&1
    rc=$?
    if [ $rc -ne 0 ] || [ ! -s "$png" ] || [ "$(stat -c %s "$png")" -lt 5000 ]; then
        echo "FAILED  $name (exit $rc)"; tail -5 "$work/$name.log"; failed=$((failed + 1))
    else
        echo "ok      $name"
    fi
done
echo "${#shots[@]} pictures in $out, $failed failed"
[ $failed -eq 0 ]
