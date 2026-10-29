#!/bin/bash
# Builds Lemuri and runs all its tests (the interface ones on the headless screen) on other distributions,
# each with its own Java 21, in containers:
#   bash src/test/distros/app.sh [distro…]        (default: all of them; DOCKER="sudo docker" if needed)
# The sources are copied into the container (read-only mount, nothing written back); the Maven downloads go
# into a docker volume shared by the runs. A summary line per distribution at the end.
set -uo pipefail
cd "$(dirname "$0")/../../.."
DOCKER=${DOCKER:-docker}
ALL=(debian ubuntu fedora arch opensuse)
DISTROS=("${@:-${ALL[@]}}")
LOGS=${LOGS:-target/distros-app}
mkdir -p "$LOGS"
result=()
for d in "${DISTROS[@]}"; do
    echo "== $d"
    if ! $DOCKER build -q -t "lemuri-app-$d" -f "src/test/distros/app/Dockerfile.$d" src/test/distros/app > "$LOGS/$d-image.log" 2>&1; then
        result+=("$d: image FAILED (see $LOGS/$d-image.log)"); continue
    fi
    $DOCKER run --rm -v "$PWD:/src:ro" -v lemuri-m2:/root/.m2 "lemuri-app-$d" bash -c '
        set -e
        mkdir /work && cd /src && tar --exclude=./target --exclude=./.git -cf - . | tar -xf - -C /work && cd /work
        java -version 2>&1 | head -1
        ./mvnw -B -q verify' > "$LOGS/$d.log" 2>&1
    code=$?
    tests=$(grep -ho "Tests run: [0-9]*, Failures: [0-9]*, Errors: [0-9]*, Skipped: [0-9]*$" "$LOGS/$d.log" | tail -1)
    java=$(grep -m1 -i "openjdk version\|openjdk [0-9]" "$LOGS/$d.log")
    if [ $code -eq 0 ]; then result+=("$d: OK  ($java)"); else result+=("$d: FAILED, exit $code  ($java) $tests — see $LOGS/$d.log"); fi
done
echo
printf '%s\n' "${result[@]}"
