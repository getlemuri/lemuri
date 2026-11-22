#!/usr/bin/env bash
# The fuzz targets (tests/fuzz/), built with clang in build-fuzz/, each for SECONDS (default 60), from the seeds in
# tests/fuzz/corpus/ plus what earlier runs found (kept in build-fuzz/corpus/). A crash or a too slow input stops it
# and is saved in build-fuzz/ as crash-* or timeout-*: tests/fuzz/README.md says how to look into it.
#   tests/fuzz.sh [SECONDS]
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
seconds=${1:-60}
command -v clang++ > /dev/null || { echo "Needs clang (apt install clang)" >&2; exit 1; }
CC=clang CXX=clang++ cmake -S "$here" -B "$here/build-fuzz" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLEMURI_TESTS=OFF -DLEMURI_FUZZ=ON > /dev/null
targets=(FuzzLanLine FuzzEnvelope FuzzKnownHosts FuzzSshConfig)
cmake --build "$here/build-fuzz" --target "${targets[@]}"
failed=()
for t in "${targets[@]}"; do
    corpus=$here/build-fuzz/corpus/$t
    mkdir -p "$corpus"
    echo "== $t, $seconds s"
    # Leaks are LeakSanitizer's job in the ASan build of the tests; here a run must only not crash or hang.
    if ! (cd "$here/build-fuzz" && ASAN_OPTIONS=detect_leaks=0 "./$t" -max_total_time="$seconds" -timeout=5 -max_len=4096 \
            -rss_limit_mb=3000 -print_final_stats=1 "$corpus" "$here/tests/fuzz/corpus/$t" 2>&1 | grep -E "^Done|ERROR|SUMMARY|stat::number_of_executed_units"); then
        failed+=("$t")
    fi
done
[ ${#failed[@]} -eq 0 ] && echo "fuzzing found nothing" || { echo "FOUND: ${failed[*]}"; exit 1; }
