# Changelog

What changes for users, version by version. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
- A terminal closed while it was still signing in left its SSH connection open in the background.
- The upload confirmation took about ten seconds to appear after the files were picked.
- After a double click on a folder, the names it listed could not be clicked.
- With a second monitor, a maximized window could go under the Ubuntu dock.
- Pasting with the middle button came with a noticeable delay.
- Ghost mode: resizing a window docked on the right or at the bottom moved the opposite edge too before settling.
- Moving the window, a dialog or the ghost window by dragging was jerky: it now follows the mouse smoothly.
- Resizing the window or a dialog from its edges no longer shakes the opposite edge, and the terminals work out their columns and rows once, when the mouse is released.
- Tooltips were white on a dark theme: they are now always dark with light grey text.

### Added
- Updates from the app: a daily check on GitHub (it can be turned off) and *Settings → Updates → Check now*. A new version shows in the status bar with its notes; *Update and restart* installs it only if its tag is signed with the release key, builds it and restarts, and goes back to the previous version if anything fails.
- When a connection fails, the terminal shows why in the middle, like the connecting screen, with *Retry* (a new connection in the same tab or split part), *Show the terminal* and *Close*.
- The app starts with the bottom panel open on a shell on this computer.
- The host tree shows how full each host's disk is, with a small bar coloured by usage; the tooltip lists all its disks. To keep it current, the metrics of your hosts are read every 5 minutes while the app is open, unless the reachability check is set to never.
- `bin/lemuri --install-launcher [--pin]`: Lemuri in the applications menu of Linux desktops and, on request, in the GNOME dock.
- JDK 21 no longer has to be the default Java: with a default of 17 or newer, Maven and `bin/lemuri` find it among those installed.

### Changed
- Updated pty4j (local shell) to 0.14.0 and Maven to 3.10.0.
- Interactive mode: the names of an earlier `ls` stay clickable as long as the prompt is the same.
- Ghost mode: the controls on the right of the bar are centred and grouped by separators; no light border around its menus.
- Ghost mode: a docked window follows the mouse while it is resized, instead of a line showing the new edge.
- A new look for the number badges of the host tree and of the Team icon.
- The scroll bar of the terminals has no track: only a thin thumb, right on the terminal.
- F11 fades the window in and out, and it appears already in its place.
- Downloads show no arrow in the status bar.
- The splash screen fades out at its edge instead of ending in a dark disc.

## [0.1.0] - 2026-10-29

First public version.

- Host tree with nested folders, live reachability and import from `~/.ssh/config`.
- SSH terminals in tabs, split side by side or one above the other, with synced typing; a local shell too.
- Jump hosts (ProxyJump), with every host key checked against `~/.ssh/known_hosts`.
- Interactive mode: double click on a folder listed by `ls` to enter it, on a text file to open it in `nano`.
- Uploads and downloads over SFTP from the terminal.
- Live metrics (CPU, memory, disks, uptime), pending updates and reboot status, with no agent on the server.
- Encrypted vault for passwords and passphrases.
- Hosts shared with colleagues, sealed and signed, over the local network or as encrypted text.
- Ghost mode: a see-through terminal that can dock to a side of the screen.
- Backup and restore; the `lemuri` command opens a shell to a saved host from any terminal.
- English and Italian, light and dark themes.

[Unreleased]: https://github.com/getlemuri/lemuri/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/getlemuri/lemuri/releases/tag/v0.1.0
