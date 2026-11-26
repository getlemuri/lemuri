# Changelog

What changes for users, version by version. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Lemuri in the GNOME dock: asked once at the first start, and a choice in *Settings → Appearance*.
- `debian/`: a package for a PPA or a distribution, in `/usr` and updated by apt (`-DLEMURI_SELF_UPDATE=OFF`), with
  AppStream data for the software centres.
- Downloading a folder that is already here asks once, saying how many files would be replaced; very large downloads
  are asked about, and refused if the disk has no room.
- An eye in every password field shows what is typed.
- The drop-down lists have their arrow.
- At the top right of every terminal, a green terminal icon and the name of its server (of the shell, for this computer).
- The main window and the dialogs have a dark shaded backdrop, from near black at the top left to a dark slate green at the bottom right, crossed by soft waves; panels let it show through, the terminals stay black.

### Changed
- The commands over a terminal reach its right edge, over the scroll bar, which starts just under them.
- In ghost mode the host tree has the same scroll bar as the rest of the app.
- Over each terminal, an X closes that pane (the whole tab when it is the only one) where the bin removing the server used to be; the server is still removed from the tree.
- The reachability check is every 5 or 15 minutes (every minute is gone), skips hosts with a connection already open,
  and tries at most 16 hosts at a time.
- The background readings never use keys that want a touch (FIDO, `sk-`), nor an ssh-agent holding one.
- `SECURITY.md` says what Lemuri does on your servers in the background, and what the backup key does not protect
  against.
- The button that deletes, in a dialog, is filled red with white text; red text elsewhere is lighter and easier
  to read.

### Fixed
- In *Settings*, ticking the Tailscale/100.64 option did not enable Apply.
- The text of a dialog could get less height than it needs, cut at the top and the bottom (*Delete folder*).

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
