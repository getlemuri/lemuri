# Security policy

Lemuri holds SSH access to servers and the passwords for them, and the installed package updates itself as root, so
security reports are very welcome.

## Supported versions

Lemuri is at version 0.x. Security fixes go into `main` and the latest release only; older versions are not
patched.

## Reporting a vulnerability

Please **do not open a public issue** with the details.

- **Preferred**: GitHub's private reporting, in the **Security** tab of this repository → **Report a vulnerability**.
- **If that is not available**: open a public issue that only says you have a security report and asks for a private
  contact, with no details, and you will get one.

Please include what you found, how to reproduce it, the version or commit, and your operating system.

What to expect: an acknowledgement within **7 days**; a first assessment (accepted or not, and how serious) within
**14 days**; a fix, or a plan with a date, within **90 days** for accepted reports, serious issues first. Fixes are
published with a GitHub security advisory (and a CVE when it applies), crediting you unless you prefer not to be.
Please keep the details private until the fix is released, or until 90 days have passed, whichever comes first.

## Threat model

**In scope**

- **A malicious or compromised SSH server**: what it prints in the terminal, a changed host key (or one of another
  kind than the remembered one), the prompts it sends during sign-in. It must not be able to run commands on your
  computer or get a saved password you did not mean for it.
- **Other computers on the local network**, when local network sharing is on: they must not be able to send you hosts,
  link to your PC without the matching code on both screens, put a link request on your screen while you are not
  waiting for one, or read what you send.
- **Whoever handles a host sent as text**: they must not be able to read it, change it without it failing to open,
  pass themselves off as one of your contacts, or make an old envelope look new.
- **Someone with a copy of your `~/.lemuri` files** but not your master password: they must not be able to read the
  saved passwords.
- **The update channel**: a release counts only if its package is signed by a key the installed copy carries; a
  changed package, a tag signature, another package or a version not newer than the installed one are refused before
  apt is called, by the backend and again as root by `lemuri-install`.
- **Anything that runs commands on a server, or on your computer, without you asking.**

**Out of scope**

- Someone who can already run code as your user (they can read the app's memory or log your keyboard).
- A contact's computer being compromised; a weak master password; adding a contact without comparing the fingerprint.
- Bugs in libssh, OpenSSL, Qt or the servers themselves.

## Known limitations

Documented choices, not vulnerabilities, although ideas to improve them are welcome:

- **The sharing protocol is our own and has had no external review.** An envelope is sealed with X25519 (an ephemeral
  key with the recipient's, and the sender's key with the recipient's), HKDF and AES-256-GCM; linking two PCs is a
  commit-reveal exchange that ends in a 6-digit code compared on both screens. Each piece is standard, the
  composition is not, and it is kept byte for byte compatible with the first Lemuri. Two properties it does not have:
  - *Key compromise impersonation*: whoever steals your private key can also pass themselves off **to you** as any of
    your contacts, since the static part of the exchange is the same computed from either side.
  - *No forward secrecy for the recipient*: with the recipient's private key, every envelope sent to them that was
    recorded can be opened.
  A version 2 may move to a reviewed design (Noise `KK`/`XX`, or the age format).
- **Commands typed into the terminal for you** (double click on a name, uploads into the terminal's folder) are off
  until turned on in *Settings → Servers*. When on, they are sent only when the line under the cursor looks like an
  empty shell prompt; that check reads the screen, so a program printing a fake prompt can fool it. Names with a `\`,
  a `!` (expanded by csh/tcsh even in quotes) or control characters are never typed.
- **While the vault is unlocked**, passwords are in the backend's memory as Qt strings, which are not reliably wiped
  when it locks. The vault key is.
- **Updates** reach root. The backend checks the package as you; then pkexec runs `lemuri-install`, which copies it
  into a folder only root can write and checks it again there: signature, name, version, and that the version is
  newer than the installed one, so an old package with holes fixed since cannot be put back. pkexec asks for an
  administrator's password every time, with a message of its own (the polkit action
  `io.github.getlemuri.lemuri.install-update`).
- **Local network sharing** is off by default. When on, anyone on the network can see that you run Lemuri, with your
  `user@hostname`. Link requests are refused unless you opened *Team → Wait for a link* (for 5 minutes at most), at
  most one a minute from each address and five a minute in all; one address cannot hold more than 4 connections.
  100.64.0.0/10 (Tailscale, carrier-grade NAT) counts as local only if you turn it on in *Settings → Local network*:
  on a phone's hotspot those addresses are the operator's other customers.
  Someone on the network can still keep those limits busy while you wait for a link, so that your own request is
  refused: a denial of service, never a way in, since a link still needs the code compared on both screens.
- **In the background**, without you doing anything, the backend (as long as *Settings → Servers → Check whether
  hosts answer* is not *Never*):
  - every 5 or 15 minutes opens a TCP connection to the SSH port of each host that has no connection open already,
    exchanges the version line and closes, without logging in (at most 16 hosts at a time). Each leaves a line in
    the server's sshd log;
  - signs in to each host that is not one of those received from your contacts, and keeps that connection open to
    read resources, updates and the SSH security checks. It uses only what needs nobody: keys without passphrase,
    ssh-agent, and passwords of a vault that is already unlocked, each tried once. Keys that want a touch (FIDO,
    `sk-` types) are never used in the background, nor is an agent that holds one. An agent that asks to confirm each
    use (`ssh-add -c`) cannot be told apart and would ask; with one, set the check to *Never*.
- **A `~/.ssh/config` from a backup** is never imported unless you tick it, and whatever in it is more than a plain
  connection setting (an allowlist: commands, libraries such as `PKCS11Provider`, `ForwardAgent`, `Include`, control
  sockets, forwarded ports...) is named next to it first. `authorized_keys`, `rc` and `environment` are never written.

## Package signing keys

The installed copies accept an update signed by any key in their `backend/package-signers`:

| Key | Where | Use |
|---|---|---|
| `packages@getlemuri` | the `LEMURI_PACKAGE_KEY` secret of the `release` environment (approval required, tags `v*` only) | signs every release, in a job that only signs: the build runs elsewhere, without secrets |
| `packages-backup@getlemuri` | offline, never on a computer that is online | only to replace the release key |

Every package also has a build provenance attestation: `gh attestation verify lemuri_<version>_amd64.deb --repo
getlemuri/lemuri` shows that it was built by this repository's workflow, from which commit.

**If the release key is lost or stolen:**

1. Delete the secret and stop the releases (Settings → Environments → release).
2. Make a new release key, and put its public part in `backend/package-signers` in place of the old one, next to the
   backup key.
3. Release a new version signed **with the backup key** (`packaging/sign.sh` with it, from the offline computer). The
   installed copies accept it, and from then on trust only the new key and the backup key.
4. Publish a security advisory saying from which version the old key is no longer trusted. Until each copy has
   updated, a stolen key can still sign packages it accepts: that is why the advisory goes out at once, and why an
   update is never installed without the system password.

**What the backup key does not protect against:** the two keys weigh the same. Whoever steals the release key can
sign an update that removes the backup key from `package-signers`, and a copy that installs it before the
advisory trusts the thief from then on. Requiring two signatures for an update that changes the keys (as TUF does) is
planned; until then, the system password before every update is the last check.

**If both keys are lost**, the installed copies cannot be updated any more: a new package, with new keys, has to be
installed by hand. After using the backup key, make a new backup key the same way.

The tags of the releases are signed by the keys in `.github/release-signers`; the workflow refuses any other.

## Where to look

| Area | Files |
|---|---|
| Vault (encryption of saved passwords), AES-GCM and Argon2id | `backend/vault/Vault.cpp`, `backend/vault/Crypto.cpp` |
| SSH connections, host key checks, jump hosts | `backend/ssh/Ssh.cpp`, `backend/hosts/routes/Jumps.cpp`, `backend/hosts/routes/SshConfig.cpp` |
| Background sign-in with saved passwords (fixed commands, output and time limits) | `backend/monitoring/live/Metrics.cpp`, `backend/monitoring/live/Probe.cpp` |
| Uploads and downloads over SFTP (paths sent to the server) | `backend/sessions/transfers/Transfers.cpp`, `backend/ssh/Sftp.cpp` |
| Backups, and which files of `~/.ssh` an import may write | `backend/backup/Backup.cpp`, `backend/backup/SshFolder.cpp` |
| Envelopes, contacts, hosts arriving from a contact | `backend/sharing/crypto/Envelope.cpp`, `backend/sharing/team/Team.cpp`, `backend/sharing/crypto/Parcel.cpp` (`admit`), `backend/sharing/team/ContactNames.cpp` |
| Local network protocol (announcements, pairing, deliveries) | `backend/sharing/lan/Lan.cpp` |
| Package updates (signature check, installation) | `backend/updates/Updates.cpp`, `packaging/linux/lemuri-install`, `.github/workflows/release.yml` |
| The local shell | `backend/sessions/terminals/LocalShell.cpp` |
| Commands typed into the terminal, what a paste may contain | `src/terminal/TerminalView.cpp` |
| Single running copy | `src/app/launch/Instance.cpp` |
