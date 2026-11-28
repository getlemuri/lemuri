# Changelog

What changes for users, version by version. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Themes, in Settings › Appearance: Lemuri (the dark one so far), Night blue, Graphite and Light. The terminal takes
  the theme's colours too (on Light, a white terminal with darker ANSI colours), and so does ghost mode's see-through
  veil. The theme changes at once, without a restart: open terminals keep their sessions and their text.

### Changed
- The update window shows each step with its mark (done, in progress, failed) and what it gave (the size, the key
  that signed, the versions), and below, as in Synaptic, the output of apt as it comes. Long texts wrap instead of
  being cut, and a failure says apt's own reason.

### Fixed
- The .deb from GitHub installs on Ubuntu 25.04 and later and on Debian 13, where some Qt libraries are named without
  the t64 suffix: apt refused it there, and the update from an earlier version failed.

## [0.1.4] - 2026-11-27

### Added
- Packages for Debian 12 and 13 (amd64 and arm64), built by the openSUSE Build Service and updated by apt; the PPA for
  Ubuntu 24.04, 26.04 and 26.10.

### Fixed
- Lemuri builds with libvterm 0.1, the one in Debian 12.

## [0.1.3] - 2026-11-27

### Added
- Over every server terminal, next to the gauge, a folder: the terminal gives way to the files of this computer (left)
  and of the server (right), in the folder the shell is in. A double click or a drag copies files across; new folder,
  rename and delete on both sides (here, to the trash). The same button, now a terminal, brings the terminal back.
- Ghost mode has the files view too, see-through like the terminal, and the Team page: a *Team* button in its left
  column (or *Team* in the status bar) shows it under the tabs; the button again, the *Terminal* one now beside it, or a
  tab brings the terminal back.
- *Recent servers*, a clock under the ghost in the left column (or **Ctrl+Shift+O**, over the terminals and in ghost
  mode too): the last 5 servers started, numbered; a
  number (or a click) opens it, or shows its terminal if it is already open. The list survives a restart.

- Several servers at once, split in a tab: four to a tab as a grid (two side by side; three with one on the left and
  two on the right; four two by two), more in more tabs. *Open all, split* on a folder opens its servers; *launch
  groups*, in the menu of the clock button (or Ctrl+Shift+O), are named lists of servers in their order, from any
  folder, made and changed there.

### Changed
- The app icon and the splash screen take the app's dark slate green in place of the blue.
- Every terminal has a band across its top, fading into it, as Terminator's title bars: the host on the left, what
  the shell says it is doing (user@host: folder) in the middle, the gauge and the commands on the right; the shell
  starts right under it instead of under the commands.
- The scroll bar of the terminals is a thin faint line, shown only with lines to scroll back to; wider under the mouse.
- The menus have slightly rounded corners and the backdrop's dark green shade.
- The host tree starts closed (the *Hosts* button opens it): the normal window reopens it only if it was open last
  time; the ghost window always starts without it.
- With the vault locked, the Team page shows only a sign-in with the master password (choosing one, when there is no
  vault yet); the contacts, the hosts received and the local network appear once it is open.
- The Team page is simpler: the key in one line with *Send my key* and *Paste a host*, the incoming hosts only when
  there are some, contacts and local network side by side on a wide window, shorter texts (the details in the tooltips).

## [0.1.2] - 2026-11-27

### Fixed
- The first start after an update showed the new texts as `!key!` (the dock question), until the next start.
- A terminal scrolled back could crash Lemuri when it grew taller.
- Ghost mode stayed stuck when the window was maximized on a screen without panels.

## [0.1.1] - 2026-11-26

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
