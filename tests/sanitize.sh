#!/usr/bin/env bash
# The unit tests, the backend end to end and sharing between two backends, built with a sanitizer in build-<name>/:
#   tests/sanitize.sh address    AddressSanitizer, LeakSanitizer and UBSan
#   tests/sanitize.sh thread     ThreadSanitizer: by hand only, and noisy. The system Qt is not built with it, so
#                                Qt's reference counts look like races when a copy crosses threads; read each
#                                report, a clean run needs a Qt built with -fsanitize=thread.
# The tests run in a clean environment (no desktop session): with one, Qt talks to D-Bus and the input method
# (ibus) in a thread of its own, and since the system Qt is not built with the sanitizer, that looks like races.
# tests/tsan.supp silences what is left of it, inside Qt's D-Bus only.
set -uo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
kind=${1:?address or thread}
build=$here/build-${kind/address/asan}
build=${build/thread/tsan}
cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLEMURI_SANITIZE="$kind" > /dev/null || exit 1
cmake --build "$build" --parallel "${JOBS:-4}" || exit 1
clean=(env -i "HOME=$HOME" "PATH=$PATH" "LANG=${LANG:-C.UTF-8}" QT_QPA_PLATFORM=offscreen
    "ASAN_OPTIONS=detect_leaks=1:abort_on_error=1"
    "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1"
    "TSAN_OPTIONS=halt_on_error=1:suppressions=$here/tests/tsan.supp")
failed=()
echo "== Unit tests ($kind)"
# QtTest's own watchdog thread ends without a join: not ours, so thread leaks are not reported for ctest.
"${clean[@]}" "TSAN_OPTIONS=halt_on_error=1:report_thread_leaks=0:suppressions=$here/tests/tsan.supp" \
    ctest --test-dir "$build" --output-on-failure || failed+=(ctest)
echo "== Backend end to end ($kind)"
"${clean[@]}" python3 "$here/tests/core_test.py" --core "$build/lemuri-core" || failed+=(core_test)
echo "== Sharing between two backends ($kind)"
"${clean[@]}" python3 "$here/tests/team_test.py" --core "$build/lemuri-core" || failed+=(team_test)
[ ${#failed[@]} -eq 0 ] && echo "ALL PASSED ($kind)" || { echo "FAILED ($kind): ${failed[*]}"; exit 1; }
