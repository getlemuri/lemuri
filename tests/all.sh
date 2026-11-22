#!/usr/bin/env bash
# Every test: the texts, the app and the backend's pieces (ctest), the backend end to end (core_test.py), sharing
# between two backends (team_test.py), package updates and the pictures of every page. With a package unpacked
# somewhere, its own app and backend are tested instead:
#   tests/all.sh                      the build in build/
#   tests/all.sh path/to/lemuri    the unpacked .tar.gz (lemuri/), or /opt/lemuri once installed
set -uo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
pkg=${1:-}
core=${pkg:+$pkg/bin/lemuri-core}
core=${core:-$here/build/lemuri-core}
failed=()
echo "== Texts"
python3 "$here/tests/texts_test.py" || failed+=(texts)
echo "== App and backend pieces"
ctest --test-dir "$here/build" --output-on-failure || failed+=(ctest)
echo "== Backend, end to end"
python3 "$here/tests/core_test.py" --core "$core" || failed+=(core_test)
echo "== Package updates"
# The fake GitHub needs the tests' backend; the package's one (the build's, or the unpacked one) must ignore it.
python3 "$here/tests/update_test.py" --package-core "$core" || failed+=(update_test)
echo "== Sharing between two backends"
python3 "$here/tests/team_test.py" --core "$core" || failed+=(team_test)
echo "== Pictures"
"$here/tests/shots.sh" ${pkg:+"$pkg/bin/lemuri"} || failed+=(shots)
[ ${#failed[@]} -eq 0 ] && echo "ALL PASSED" || { echo "FAILED: ${failed[*]}"; exit 1; }
