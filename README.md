# Lemuri

An SSH console for the servers you look after, with a native Qt 6 interface: servers organised in folders, shells in
tabs and split panes, a local shell, disk usage, pending updates and an SSH security check for each server, file
transfers over SFTP, an encrypted password vault, host sharing with your team, and a see-through "ghost" terminal
that docks to an edge of the screen.

It reads the data of the first Lemuri (`~/.lemuri` and its settings) and exchanges hosts with it, so moving to this
version keeps your servers, vault and contacts.

**Status: early (0.x).** Developed and used daily on Ubuntu with GNOME. The GitHub workflows (CI and releases) are
written but have not run yet, and the macOS package has never been built.

## Architecture

- `src/`: the interface (C++, Qt Widgets). The terminal is rendered by Qt on top of
  [libvterm](https://www.leonerd.org.uk/code/libvterm/). `app/` holds the model and the link to the backend,
  `widgets/` the theme and shared controls, `terminal/` the terminal, its tabs and ghost mode, `window/` the main
  window and its pages, `dialogs/` settings, backups, updates and sharing.
- `backend/`: `lemuri-core` (C++, Qt Core and Network, libssh, OpenSSL, libargon2), a separate process with no UI
  that handles SSH, SFTP, the local shell, the vault, monitoring, sharing (with contacts and over the local network),
  backups and updates. The app starts it and talks to it through JSON lines on its standard input and output (see
  [backend/PROTOCOL.md](backend/PROTOCOL.md)). It reads and writes exactly the same files and formats as the first
  Lemuri.
  Its folders: `base/` (requests, threads, files, settings, texts, logging), `vault/`, `hosts/`, `ssh/`,
  `sessions/` (terminals and transfers), `monitoring/`, `sharing/`, `backup/`, `updates/`.
  `Core` reads and writes the lines and wires the modules together. Each module (`Hosts`, `Sessions`, `Transfers`,
  `Monitor`, `Team`, `Backups`, `Updates`) receives in its constructor only what it needs from `Runtime.h` (events,
  questions to the user, the activity log, the main thread and the worker pool) and from the other modules, and
  registers its requests in `ops()`. Every thread is owned by an object that joins it on shutdown, so the backend
  exits cleanly when the app closes its input.
- `backend/i18n/`: the interface texts, in English and Italian.
- `tests/`, `packaging/`: see below.

## Building and running

Requires Qt 6.4 or newer (Widgets, Network, Test), libvterm, libssh 0.10 or newer, libargon2, OpenSSL 3, CMake and
Ninja. On Debian/Ubuntu: `apt install qt6-base-dev libvterm-dev libxcb1-dev libssh-dev libargon2-dev libssl-dev
cmake ninja-build`.

    cmake -S . -B build -G Ninja && cmake --build build      # build/lemuri and build/lemuri-core
    build/lemuri                                          # --toggle shows or hides the running copy (for a shortcut)

Only one copy runs per user: starting a second one brings the first to the front.

## Tests

    tests/all.sh                  # everything below
    tests/texts_test.py           # both languages have the same keys, and every key used in the code exists
    ctest --test-dir build        # the app (terminal, tabs and splits, against a fake backend) and the backend's
                                  # modules (envelopes, local network, backups, monitoring, updates)
    tests/core_test.py            # the backend end to end, against an SSH server on loopback (tests/sshd, paramiko)
    tests/update_test.py          # package updates against a fake GitHub (signed, tampered, wrong key, downgrade...);
                                  # only the test build, build/lemuri-core-testing, reads the fake address
    tests/team_test.py            # two backends sharing hosts: key cards, envelopes, the local network, pairing
    tests/shots.sh                # a screenshot of every page and dialog, in build/shots/
    tests/sanitize.sh address     # the tests built with AddressSanitizer and UBSan ("thread": ThreadSanitizer, noisy)
    tests/fuzz.sh 60              # the parsers anyone can feed, with libFuzzer (clang): tests/fuzz/README.md
    tests/coverage.sh             # which lines the tests run (gcovr), in build-coverage/coverage/

The SSH server needs python3-paramiko. No test needs a display, a network or a real server, and none touches your data: the
backends run in temporary home folders.

`.github/workflows/build.yml` runs all of this on Ubuntu 24.04 with warnings treated as errors, plus the sanitizer
build; it has not run on GitHub yet. How to contribute: [CONTRIBUTING.md](CONTRIBUTING.md).

Screenshots by hand: `LEMURI_SHOT=file.png QT_QPA_PLATFORM=offscreen build/lemuri`, with `LEMURI_SHOT_NODE`
(`dc`, `team`, `folder:x`, `host:id`, `console:id`, `log`) and `LEMURI_SHOT_DO` (see `src/app/Shots.cpp`).

## Packages

    packaging/linux/build.sh      # dist/lemuri_<version>_<arch>.deb and .tar.gz
    packaging/mac/build.sh        # on a Mac: dist/Lemuri-<version>-mac-<arch>.zip (never built yet)

Both contain the app and its backend, nothing else. The Linux package uses the distribution's Qt,
libvterm, libssh, libargon2 and OpenSSL, so it installs wherever Qt is at least the version it was built against;
that is why releases build it on Ubuntu 24.04. The Mac app bundles those libraries (macdeployqt) and is signed ad
hoc, so on another Mac it has to be opened with right click → Open the first time.

## Releases and updates

The version lives in `CMakeLists.txt` (`project(... VERSION ...)`). To release: bump it there, add a section to
`CHANGELOG.md`, commit, `git tag -s vX.Y.Z -m vX.Y.Z` and push. `.github/workflows/release.yml` checks that the tag
is signed by a key in `.github/release-signers`, runs the tests and builds the .deb with no secrets available; a
separate job, in the protected `release` environment, signs it; a third publishes the release with a build
provenance attestation.

The installed .deb updates itself from these releases (`backend/updates/Updates.cpp`). A release counts only if it
has `lemuri_<version>_<arch>.deb` and its `.deb.sig`, signed (`ssh-keygen -Y sign -n file`) by a key in
`backend/package-signers`, and only if it is newer than the installed version. After the system password (pkexec),
`lemuri-install` checks all of this again as root before apt installs it. The signing keys, the offline backup key
and what to do if one is lost or stolen are described in [SECURITY.md](SECURITY.md#package-signing-keys). A copy
built from source does not update itself.

## Security

How to report a vulnerability, what Lemuri protects against and its known limitations: [SECURITY.md](SECURITY.md).

## License

MIT, see [LICENSE](LICENSE). Third-party components: [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)
(regenerated by `licenses.sh`).
