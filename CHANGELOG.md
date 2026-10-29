# Changelog

What changes for users, version by version. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
- A terminal closed while it was still signing in left its SSH connection open in the background.
- The upload confirmation took about ten seconds to appear after the files were picked.
- After a double click on a folder, the names it listed could not be clicked.
- With a second monitor, a maximized window could go under the Ubuntu dock.

### Changed
- Updated pty4j (local shell) to 0.14.0 and Maven to 3.10.0.
- Interactive mode: the names of an earlier `ls` stay clickable as long as the prompt is the same.
- Ghost mode: the controls on the right of the bar are centred and grouped by separators; no light border around its menus.
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
