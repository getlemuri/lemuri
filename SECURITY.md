# Security policy

Lemuri holds SSH access to servers and the passwords for them, so security reports are very welcome.

## Supported versions

Lemuri is at version 0.x and has no stable releases yet. Security fixes go into `main` and the latest release only; older versions are not patched.

## Reporting a vulnerability

Please **do not open a public issue** with the details.

- **Preferred**: GitHub's private reporting, in the **Security** tab of this repository → **Report a vulnerability**.
- **If that is not available**: open a public issue that only says you have a security report and asks for a private contact, with no details, and you will get one.

Please include what you found, how to reproduce it, the version or commit, and your operating system.

What to expect:

- an acknowledgement within **7 days**;
- a first assessment (accepted or not, and how serious) within **14 days**;
- a fix, or a plan with a date, within **90 days** for accepted reports. Serious issues are fixed first.

Fixes are published with a GitHub security advisory (and a CVE when it applies). You are credited in the advisory and in the release notes, unless you prefer not to be. Please keep the details private until the fix is released, or until 90 days have passed, whichever comes first.

## Threat model

What Lemuri tries to protect against, so you can tell whether something is a vulnerability.

**In scope**

- **A malicious or compromised SSH server**: what it prints in the terminal, a changed host key, and the prompts it sends during sign-in. It must not be able to run commands on your computer or get a saved password that you did not mean for it.
- **Other computers on the local network**, when local network sharing is on: they must not be able to send you hosts, link to your PC without the matching code on both screens, or read what you send.
- **Whoever handles a host sent as text** (the mail server, the chat, anyone the text passes through): they must not be able to read it, change it without it failing to open, pass themselves off as one of your contacts, or make an old envelope look new.
- **Someone with a copy of your `~/.lemuri` files** but not your master password: they must not be able to read the saved passwords.
- **Anything that runs commands on a server, or on your computer, without you asking.**

**Out of scope**

- Someone who can already run code as your user on your computer (they can read the app's memory or log your keyboard).
- A contact's computer being compromised.
- A weak master password.
- Adding a contact without comparing the fingerprint with them (see *Known limitations*).
- Bugs in OpenSSH or in the servers themselves.

## Known limitations

These are documented choices, not vulnerabilities, although ideas to improve them are welcome:

- **Contacts' keys are trusted as you add them**: a key line sent by email or chat could have been swapped on the way. Lemuri asks you to compare the fingerprint with the contact by voice or in person before it adds them; it cannot check that you really did. Linking PCs on the local network compares a code on both screens instead.
- **A host sent as text shows that you sent something**: its content is sealed, but whoever sees the text knows it is a Lemuri host.
- **Local network sharing** is off by default. When on, anyone on the network can see that you run Lemuri, with your `user@hostname`. Turn it on only on networks you trust.
- **Commands typed into the terminal for you** (double-click on a name, uploads to the terminal's folder) are visible, can be turned off in *Settings → Servers*, and are sent only when the line under the cursor looks like an empty shell prompt. That check reads the screen, so a program that prints a fake prompt can fool it.
- **`lemuri -s` gives saved passwords to ssh in an environment variable**, never as arguments: other users cannot see it, but your own user can read it in `/proc` while ssh runs. Each saved password is tried once; after that ssh asks you.
- **While the vault is unlocked**, passwords are in the app's memory as Java strings, which cannot be reliably wiped.
- There are no signed packages yet: build from source.

## Design questions

Questions that come up about the design, answered here rather than in passing.

**Isn't this rolling your own crypto?**
The primitives are standard and come from the JDK: X25519, HKDF-SHA256, AES-256-GCM, SHA-256. Argon2id comes from Bouncy Castle. What is Lemuri's own is how they are put together: a sealed envelope with an authenticated sender, the same shape as the `es` + `ss` tokens of Noise's `K` pattern or HPKE's authenticated mode. It is all in one file, `be/sharing/Envelope.java`, specified in [docs/crypto.md](docs/crypto.md) and checked against the published test vectors of each primitive.

**Why not age, a libsodium sealed box or Noise?**
age does not authenticate the sender, and Lemuri needs to know who sent a password. libsodium would mean native libraries for every platform; the Noise libraries for Java are not maintained. The envelope is small and specified, so swapping it for a library later is possible if a review finds a reason.

**Has it been audited?**
No. There has been no external audit. What exists: the specification in [docs/crypto.md](docs/crypto.md), the test vectors, attack tests in the repository (local network, hosts as text, SFTP names, replayed envelopes, hostile command output). Reviews are welcome.

**Why Argon2id, and why these parameters?**
It is memory-hard: guessing the master password on a GPU costs far more than with PBKDF2, which Lemuri used before (600,000 iterations). The parameters are RFC 9106's second recommended option (64 MiB, 3 passes, 4 lanes), about 0.2 s to unlock. They are written in the vault file and authenticated, so they can be raised later; older vaults are moved to Argon2id on their first unlock.

**Why a vault file instead of the system keychain (Keychain, Secret Service)?**
The same format and the same protection on Linux and macOS, a master password that you control, and a vault you can back up or move as one file. Using the system keychain to unlock the vault is on the roadmap.

**Why are the local network pairing codes only 6 digits?**
They are compared by two people, after a commitment, like Bluetooth's Numeric Comparison. A man in the middle gets one attempt per pairing, with a one in a million chance, and pairing requests are limited to one per minute from each address.

**Why no server for team sharing?**
A server would hold, at least, who sends what to whom, would need accounts and would be one more thing to trust and to keep running. A host sealed for one person and signed by the sender can travel through any channel, so Lemuri lets you use the ones you already have: the local network, or email and chat for the encrypted text.

**Why does Lemuri type commands into my shell?**
Only when you ask for something that needs the shell's current folder or a file opened in it: a double-click on a name, an upload into the terminal's folder. You see the command in the terminal as if you had typed it; only the `printf "$PWD"` that asks for the folder is not shown. It is sent only when the line under the cursor looks like an empty prompt, a check that can be fooled (see *Known limitations*). Nothing is typed on its own, and *Settings → Servers* turns it all off.

**Why does it connect to every host every few minutes?**
To show whether each host is reachable: a TCP connection to the SSH port that exchanges the version line and closes, without logging in. By default every 5 minutes; every minute or never in *Settings → Servers*. It leaves one line in sshd's log each time, and it is written to avoid what fail2ban's `aggressive` and `ddos` modes count as attempts, though not tested against every fail2ban version. Details in [docs/servers-and-network.md](docs/servers-and-network.md).

## Where to look

The code that matters most for security, under `src/main/java/lemuri/`:

| Area | Files |
|---|---|
| Vault (encryption of saved passwords) | `be/storage/FileVault.java` |
| SSH connections and host key checks | `be/terminal/SshTransport.java`, `be/terminal/KnownKeys.java` |
| Background sign-in with saved passwords (metrics, pending updates and reboot: fixed command strings, output and time limits) | `be/monitoring/Metrics.java` |
| Uploads over SFTP (paths sent to the server) | `fe/v/Transfers.java`, `be/terminal/SftpFiles.java`, `be/util/RemoteNames.java` |
| Backups: the encrypted file, and which files of `~/.ssh` an import may write (keys, `known_hosts` without `@cert-authority`, `config`) | `be/storage/Backup.java`, `be/util/SshFolder.java`, `fe/v/BackupDialogs.java` |
| Contact names (no two may look alike) | `be/util/ContactNames.java` |
| Commands typed into the terminal for you, and what a paste may contain (no escape sequences) | `fe/v/terminal/Terminal.java` (`pastable`), `src/main/resources/lemuri/terminal/terminal.html` |
| Envelope encryption and linking codes (specified in [docs/crypto.md](docs/crypto.md)) | `be/sharing/Envelope.java` |
| Sharing with contacts, hosts as text, replayed envelopes | `be/sharing/Sharing.java`, `be/sharing/TextEnvelope.java`, `be/sharing/DecidedEnvelopes.java` |
| A host arriving from a contact: checks, cleaning of what is shown, the acceptance dialog | `be/dto/Parcel.java` (`admit`), `fe/v/SharingDialogs.java` |
| Local network protocol (announcements, pairing, deliveries and receipts) | `be/sharing/Lan.java`, `be/sharing/LanServer.java`, `be/sharing/LanPairing.java`, `be/sharing/LanDiscovery.java`, `be/sharing/LanWire.java` |
| Single running copy | `be/util/SingleInstance.java` |
| `lemuri -s` from a terminal: the ssh configuration it writes, saved passwords given to ssh | `Cli.java`, `be/terminal/SshCli.java`, `bin/lemuri` |

[docs/servers-and-network.md](docs/servers-and-network.md) describes what Lemuri runs on your servers and its network behaviour.
