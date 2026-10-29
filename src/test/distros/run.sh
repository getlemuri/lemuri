#!/bin/bash
# Runs Lemuri against SSH servers of other distributions, in containers, and stops them at the end:
#   bash src/test/distros/run.sh [distro…]        (default: all of them; DOCKER="sudo docker" if needed)
# Each container listens only on 127.0.0.1. The test (DistrosTest) signs in with password and key, opens a
# shell, reads metrics, updates and reboot, uploads over SFTP, goes through a jump host and runs "lemuri -s".
set -euo pipefail
cd "$(dirname "$0")/../../.."
DOCKER=${DOCKER:-docker}
ALL=(debian ubuntu fedora rocky arch alpine opensuse)
DISTROS=("${@:-${ALL[@]}}")
WORK=$(mktemp -d)
trap 'for d in "${DISTROS[@]}"; do $DOCKER rm -f "lemuri-test-$d" >/dev/null 2>&1 || true; done; $DOCKER network rm lemuri-test >/dev/null 2>&1 || true; rm -rf "$WORK"' EXIT

ssh-keygen -q -t ed25519 -N '' -C lemuri-test -f "$WORK/key"
$DOCKER network create lemuri-test >/dev/null 2>&1 || true
list=()
port=2201
for d in "${DISTROS[@]}"; do
    echo "== $d: image"
    $DOCKER build -q -t "lemuri-test-$d" --build-arg PUBKEY="$(cat "$WORK/key.pub")" -f "src/test/distros/Dockerfile.$d" src/test/distros >/dev/null
    $DOCKER rm -f "lemuri-test-$d" >/dev/null 2>&1 || true
    $DOCKER run -d --name "lemuri-test-$d" --network lemuri-test -p "127.0.0.1:$port:22" "lemuri-test-$d" >/dev/null
    list+=("$d=$port")
    port=$((port + 1))
done
# Debian says it needs a reboot, as after a kernel update: the test expects "required" there.
if printf '%s\n' "${DISTROS[@]}" | grep -qx debian; then
    $DOCKER exec lemuri-test-debian sh -c 'echo "*** System restart required ***" > /var/run/reboot-required; echo linux-image-6.1.0-99-amd64 > /var/run/reboot-required.pkgs'
fi
sleep 2
./mvnw -q test -Dtest=DistrosTest -Dsurefire.failIfNoSpecifiedTests=false \
    -Dlemuri.distros="$(IFS=,; echo "${list[*]}")" -Dlemuri.distros.key="$WORK/key"
