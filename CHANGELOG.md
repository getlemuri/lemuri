# Changelog

What changes for users, version by version. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0] - 2026-11-22

The first release.

### Added
- Servers in folders, with their state, disk usage, pending updates (and how many are security fixes), whether a reboot is needed, and an SSH security check of each one.
- Shells in tabs and split panes, synced typing across the parts of a tab, a local shell, jump hosts like `ssh -J` with every host key checked against `known_hosts`.
- Files over SFTP from the terminal: drop files on it to upload them, right-click a name listed by `ls` to download it; progress and cancel in the status bar.
- An encrypted vault for passwords (AES-GCM, key from the master password with Argon2id).
- Sharing hosts with your contacts, sealed for them and signed by you, as encrypted text or directly over the local network; PCs are linked with a 6-digit code compared on both screens, and only while the receiving PC waits for a link (*Team → Wait for a link*).
- Encrypted backups of hosts, vault and the SSH keys you tick; a `~/.ssh/config` in a backup is never imported unless ticked, and whatever in it is more than a plain setting is named first.
- Import of `~/.ssh/config`.
- Ghost mode: a see-through terminal that docks to an edge of the screen; `lemuri --toggle` shows or hides the running copy (for a global shortcut).
- Settings, an activity log, a splash screen, English and Italian.
- Linux package (.deb), about 1.3 MB, that updates itself from the releases of this repository: only a package signed by a release key and newer than the installed one is installed, checked again as root.
- Reads the data of the first Lemuri (`~/.lemuri` and its settings), and exchanges hosts with it.
