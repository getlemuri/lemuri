# Lemuri core protocol

The backend (`lemuri-core`, started by the app from its own folder, or from `LEMURI_CORE`) talks to the frontend
over standard input and output, one JSON object per line. Its own logs go to standard error and to
`~/.lemuri/logs/lemuri-0.log`.
Closing its standard input ends it: every session and transfer is closed.

It reads and writes the same data as the first Lemuri (`~/.lemuri`, and the settings in the `lemuri/*` preference
files under `~/.java/.userPrefs`, where the first Lemuri keeps them), and speaks to it over the local network.

## Messages

| Direction | Shape |
|---|---|
| request | `{"id": 7, "op": "hosts", ...params}`; `id` may be omitted when no answer is wanted |
| answer | `{"id": 7, "ok": <value>}` or `{"id": 7, "error": "text for the user"}` |
| event | `{"event": "hosts", ...fields}` |
| question | event `ask`: `{"event": "ask", "ask": 12, "kind": "...", ...}` |
| answer to a question | `{"op": "answer", "ask": 12, "value": <value>}` |

Answers may come in any order: some ops (metrics, SFTP, team, updates) answer when their work ends.
Every request with an `id` gets exactly one answer: a handler that ends without giving one answers with an error.
Error texts are already translated. When the backend ends, the app answers its own waiting requests with an error.

The protocol has a version, `protocol` in `hello` (now 1), raised when a change would break an app of the
previous version; the app warns if the backend it found speaks another.

Objects keep the shapes the first Lemuri uses: `Host` (`id, name, group, address, port, user,
authMethod: KEY|PASSWORD|AGENT, key, note, os, state: ONLINE|OFFLINE|UNKNOWN, fromContact, received, jump`),
`MetricsResult` (`resources {cpu, cpuUsage, memTotalKb, memFreeKb, disks [{dot, totalKb, usedKb, freeKb}], onForS},
error, when`), `Updates`, `Security` (plus `score`), `Release` (`tag, version, notes, url`), `TrustedPeer` (`key, name`).

## Application

| op | params | ok |
|---|---|---|
| `hello` | | `version, protocol, language, user, vaultExists, vaultOpen, loadingNotice, data (the data folder), languageChoice (auto, en, it)` |
| `protocol.ops` | | every request this backend knows, sorted (the tests check them against this file) |
| `texts` | | every interface text, `key → MessageFormat pattern` (`{0}`, `''` for `'`) |
| `prefs.get` | `node` (`ui`, `updates`...) | `{key: value}` as strings |
| `prefs.put` | `node, values {key: value or null}` | |

## Vault

| op | params | ok |
|---|---|---|
| `vault.state` | | `exists, open` |
| `vault.create` | `password, replace?` | `exists, open` |
| `vault.open` | `password` | `exists, open, ok` (ok false: wrong password) |
| `vault.lock` | | `exists, open` |
| `vault.change` | `current, password` | `exists, open, ok` |
| `vault.has` | `host` | whether that host has a saved password |
| `vault.remember` / `vault.forget` | `host, password` / `host` | |

Event `vault {exists, open}` at every unlock and lock.

## Hosts and folders

| op | params | ok |
|---|---|---|
| `hosts` | | `hosts [Host], created [folders made by hand], folders [all, sorted]` |
| `host.save` | `host` (no `id`: a new one) | the saved Host |
| `host.remove` | `host` (id) | |
| `host.move` | `host, folder` ("" = datacenter) | |
| `folder.create` | `path` | |
| `folder.move` | `from, to` (rename or move, with subfolders and hosts) | |
| `folder.delete` | `path` (its hosts go to the parent) | |
| `sshconfig.read` | | `file, entries [{alias, hostName, user, port, identityFile, proxyJump, destination, chosen, why}]` |
| `sshconfig.import` | `aliases [..], folder` | |

Event `hosts` (same fields as the op) after every change, and when the probe finds a host's state changed.

## Monitoring

| op | params | ok |
|---|---|---|
| `monitor.start` | | starts the reachability probe and the periodic readings (call once, after `hosts`) |
| `monitor.probeMinutes` | `minutes?` (0: never on its own; otherwise at least 5) | the minutes |
| `monitor.tryAll` | | |
| `metrics.refresh` | `host, force?` | (the reading arrives as an event) |
| `metrics.last` | | `{hostId: MetricsResult}` |
| `metrics.history` | `host` | `cpu [..], running` |
| `host.updates` | `host, force?` | `Updates` (when read) |
| `host.security` | `host, force?` | `Security` + `score` (when read) |

Event `metrics {host, result: MetricsResult}`.

## Sessions

| op | params | ok |
|---|---|---|
| `session.open` | `host` or `local: true`, `cols, rows` | `session, title, host` ("local" for a local shell) |
| `session.input` | `session, data` | |
| `session.resize` | `session, cols, rows` | |
| `session.close` | `session` | |
| `session.disconnectHost` | `host` | |

Events `data {session, data}`, `state {session, state: CONNECTING|CONNECTED|CLOSED|ERROR, message}`.
Tabs, splits and the local shell that replaces an SSH shell ended with `exit` are the frontend's.

## Transfers (SFTP on a session's connection)

| op | params | ok |
|---|---|---|
| `sftp.home` | `session` | the home folder |
| `sftp.existing` | `session, folder, files [local paths]` | names already there |
| `sftp.locate` | `session, shell (its folder), listed (ls argument or null), name` | `path, name` |
| `sftp.upload` | `session, folder, files, known (the names the user agreed to overwrite)` | `transfer` |
| `sftp.download` | `session, path, into (local folder), replace` | `transfer` |
| `sftp.list` | `session, path ("" is the home folder)` | `path` (absolute), `entries [{name, dir, link, size, mtime (seconds)}]`: links not followed, but a link to a folder has `dir` |
| `sftp.mkdir` | `session, path` | `true` |
| `sftp.rename` | `session, from, to` | `true`; refused if `to` is already there |
| `sftp.remove` | `session, paths [absolute]` | `true`: files, links (never what they point to) and folders with what is inside; `/` and relative paths refused |
| `fs.check` | `session, shell (its folder), listed (ls argument or null), name` | `path, typed (the name quoted for the shell), kind: UNTYPABLE (a \\ or ! in it: not typed), FOLDER, TEXT, BINARY, LARGE or OTHER` (the double click on a name; local shells too) |
| `transfer.cancel` | `transfer` | |

Event `transfer {transfer, direction: up|down, what, file, started, done, total, finished}`; the last one
has `finished: true, outcome: DONE|CANCELLED|FAILED, message`.

## Team and local network

| op | params | ok |
|---|---|---|
| `team.start` / `team.restart` | | |
| `team.state` | | `state, message, lanState, lanMessage, lanOn, lanCarrierGrade, firewall, fingerprint, keyCard, contacts, peers, deliveries` |
| `team.send` | `host, to [keys], withPassword, message` | `[{to: TrustedPeer, text}]`: `text` is the envelope to pass by hand, null if delivered over the network |
| `team.open` | `text` (a pasted envelope) | a delivery |
| `team.accept` | `delivery, mode: apart|folder|update, folder` | `host, remembered` |
| `team.reject` | `delivery` | false if the vault could not remember it |
| `team.addContact` | `keyCard, name` | |
| `lan.on` | `on` | |
| `lan.waitForLink` | `on` | waiting for a link from another PC for 5 minutes (`on: false` stops); requests that come otherwise are refused; returns whether it waits |
| `lan.carrierGrade` | `on?` | whether 100.64.0.0/10 (Tailscale, carrier-grade NAT) counts as local (off by default); changed and restarted if `on` is given |
| `lan.refresh` / `lan.restart` | | |
| `lan.trust` / `lan.forget` | `key, name` / `key` | |
| `lan.nameCheck` | `name, key` | `clean` (the name as it would be saved), `problem` (why it cannot be used, translated) or null |
| `team.parseKey` | `card` (a pasted key card) | `name, fingerprint`, or null if it is not one |
| `lan.addAddress` / `lan.removeAddress` | `address` | the peer / |
| `lan.pair` | `key` | true if paired; the code arrives first as event `pairCode {key, code}` |

A delivery: `id, from, fingerprint, when, label, error, content {name, address, user, port, os, message,
authMethod, hasPassword, twin (id of the host with the same login, or null)}`. Passwords never leave the backend.
A peer: `key, name, email, address, port, seen, manual, warning, fingerprint, nearby, who, trusted`.

Events `sharing {state, message}`, `deliveries {deliveries}`, `arrival {delivery}`, `lan {state, message, on}`,
`peers {peers}`, `contacts {contacts}`, `firewall {warning}`.

## Backups

| op | params | ok |
|---|---|---|
| `backup.sshFiles` | | `files [{name, kind, detail, chosen, withPub}], dir, minPassphrase, hosts, folders, secrets` (what an export would hold) |
| `backup.export` | `path, passphrase, files [names]` | `message` (the vault must be open) |
| `backup.open` | `path, passphrase` | `token, from, created, hosts, folders, secrets, replaceByDefault, hostsHere, files [{name, kind, detail, chosen, tip}]` |
| `backup.import` | `token, replace, files [names], source` | `message` (the vault must be open) |
| `backup.close` | `token` | |

## Updates

| op | params | ok |
|---|---|---|
| `update.start` | | starts the daily check; the state |
| `update.state` | | `current, available, offered, checking, automatic, lastCheck` |
| `update.automatic` | `on?` | |
| `update.check` | | the newer `Release`, or null |
| `update.skip` | `version` | |
| `update.later` | | |
| `update.blocker` | | why this copy cannot update itself, or null |
| `update.install` | | when installed; steps as events `updateStep {step}` |
| `update.restart` | | the new copy starts when this one ends: quit right after |

Event `update` (same fields as `update.state`).

## Activity log

Event `task {id, target, text, outcome: RUNNING|OK|ERROR, detail, when}`; a later `task` with the same id
(`outcome, detail, ended`) ends that line.

## Questions

`ask` events wait for an answer; the backend's thread that asked waits meanwhile.

| kind | fields | answer `value` |
|---|---|---|
| `confirm` | `title, message, action, session?` | `true` / `false` |
| `password` | `title, message, note, remember (checkbox label), session` | `{text, remember}` or null |
| `secret` | `title, message, note, session` | `{text}` or null |
| `vault` | `message (why), error (previous try)` | `{text}`: the master password, or null to give up |
| `vaultCreate` | `message, error` | `{text}`: the new master password (asked twice by the frontend), or null |
| `pair` | `peer, code` | the contact's name to accept, null to refuse |

## Environment, for the tests

| Variable | Effect |
|---|---|
| `LEMURI_CORE` | (read by the app) the backend to start instead of `lemuri-core` beside the app |
| `LEMURI_LAN_PORTS` | `tcp,udp`: the local network on these ports (`0,0`: any free one, no announcements); the on/off choice and the typed addresses stay in memory |
| `LEMURI_UPDATE_API` | the address of the "latest release" answer instead of GitHub's |
| `LEMURI_UPDATE_SIGNERS` | a file of allowed signers instead of `backend/package-signers` |
| `LEMURI_UPDATE_INSTALL` | the installing command instead of `pkexec /usr/bin/apt-get install -y` (the package's path is added); any copy may then update itself |

The three `LEMURI_UPDATE_*` are read only by the tests' build of the backend, `lemuri-core-testing`: the package's
`lemuri-core` ignores them.

`HOME` decides everything else: `~/.lemuri`, `~/.ssh`, the preferences.
