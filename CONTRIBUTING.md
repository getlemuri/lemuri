# Contributing

Thanks for helping. A few things keep Lemuri working for the people who use it every day on real servers.

## Before you start

- For anything bigger than a small fix, open an issue first: the app shares its data and formats with the first
  Lemuri and with colleagues' copies, and a change there must stay readable by both.
- Security problems go through [SECURITY.md](SECURITY.md), not public issues.

## Build and test

See [README.md](README.md#build-and-run) for the packages. Then:

    cmake -S . -B build -G Ninja && cmake --build build
    tests/all.sh                  # everything CI runs

Also useful:

    tests/sanitize.sh address     # the tests with AddressSanitizer and UBSan, as CI runs them
    tests/fuzz.sh 60              # the fuzz targets, with clang (tests/fuzz/README.md)
    tests/coverage.sh             # which lines the tests run (needs gcovr)

A change is ready when `tests/all.sh` says `ALL PASSED` and the build has no warnings (CI builds with
`-DLEMURI_WERROR=ON`).

## The code

- C++20, Qt 6.4 or newer: nothing newer than Ubuntu 24.04 has. The Mac build must keep working too, so no Linux-only
  calls outside `backend/base/system/Posix.h`.
- Code, comments and commit messages in English. The texts the user reads are in `backend/i18n/*.properties`, every
  language with the same keys (`tests/texts_test.py` checks it).
- Style: `.clang-format` (run `git clang-format` on what you change, not on whole files). `.clang-tidy` has the checks
  worth looking at: `clang-tidy -p build <file>`.
- The backend and the app speak only through the protocol in [backend/PROTOCOL.md](backend/PROTOCOL.md): a new
  request or event goes there too (a test compares the two).
- Passwords never leave the backend, and never go into logs or messages.
- Colors only in `src/widgets/look/Theme.h`.

## Tests

- A bug fixed comes with the test that would have found it.
- The backend's pieces: `tests/*Test.cpp` (QtTest, run by ctest). The backend as a whole: `tests/core_test.py`, against
  a local SSH server on loopback (`tests/sshd`). Nothing in the tests may touch real servers or the real local network.
- A change to a page: `tests/shots.sh` takes a picture of every page; look at the ones you changed.

## Commits and pull requests

- One subject per pull request, with what changed and why. Small commits that build on their own.
- Add a line to `CHANGELOG.md` under `[Unreleased]` for anything a user would notice.
