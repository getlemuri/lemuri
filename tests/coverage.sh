#!/usr/bin/env bash
# How much of the code the tests run: a build with gcov in build-coverage/, then ctest, the backend end to end and
# the package updates on it; the report (gcovr) in build-coverage/coverage/index.html, the totals on screen.
#   tests/coverage.sh
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
build=$here/build-coverage
command -v gcovr > /dev/null || { echo "Needs gcovr (apt install gcovr)" >&2; exit 1; }
cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLEMURI_COVERAGE=ON > /dev/null
cmake --build "$build" --parallel "${JOBS:-4}"
find "$build" -name '*.gcda' -delete
ctest --test-dir "$build" --output-on-failure > /dev/null
python3 "$here/tests/core_test.py" --core "$build/lemuri-core" > /dev/null
python3 "$here/tests/update_test.py" --core "$build/lemuri-core-testing" --package-core "$build/lemuri-core" > /dev/null
mkdir -p "$build/coverage"
cd "$here"
gcovr --root "$here" --filter 'backend/' --filter 'src/' --exclude '.*_autogen/.*' --object-directory "$build" \
    --gcov-ignore-parse-errors=negative_hits.warn --html-details "$build/coverage/index.html" --print-summary 2>/dev/null | grep -E "^(lines|functions|branches):"
echo "report: $build/coverage/index.html"
