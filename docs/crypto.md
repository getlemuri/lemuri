# Cryptography

What Lemuri encrypts, and exactly how, so that it can be reviewed without reading the code. Every primitive comes from the JDK (`javax.crypto`, `java.security`) except Argon2id, which comes from Bouncy Castle. Nothing here is a new primitive: the parts are standard, the way they are put together is described below. **It has not been audited.** Reviews are welcome; see [SECURITY.md](../SECURITY.md).

The code is in `be/sharing/Envelope.java` (envelopes, pairing values, fingerprints) and `be/storage/FileVault.java` (the vault). `EnvelopeVectorsTest` checks HKDF, X25519 and AES-GCM against their published test vectors, and opens envelopes sealed by the first version of the format.

Notation: `||` is concatenation, `DH(a, B)` is X25519 between private key `a` and public key `B`, public keys are their X.509 encoding (44 characters in base64), `HKDF` is HKDF-SHA256 (RFC 5869) with a 32-byte output.

## The vault

Host passwords, and the user's private key for sharing, are kept in `~/.lemuri/vault.json`:

```json
{"version": 5, "kdf": "argon2id", "iterations": 3, "memory": 65536, "lanes": 4,
 "salt": "<16 random bytes>", "iv": "<12 random bytes>", "data": "<AES-256-GCM ciphertext>"}
```

- **Key**: Argon2id (version 0x13) of the master password, with the salt and the parameters of the file. Today: 64 MiB, 3 passes, 4 lanes (RFC 9106, second recommended option), about 0.2 s. The master password itself is never stored.
- **Data**: the JSON map of entries, encrypted with AES-256-GCM, a new random IV at every save, 128-bit tag.
- **AAD**: `"lemuri-vault|5|argon2id|<passes>|<memory>|<lanes>|<salt>"`. Changing a parameter in the file (to make the key cheaper to guess) makes it fail to open, instead of making it weaker.
- **Limits**: a file asking for more than 256 MiB, more than 8 passes or more than 16 lanes is refused before anything is derived.
- **Older files**: versions 1 to 3 used PBKDF2-HMAC-SHA256 (310,000, later 600,000 iterations). Version 4 used Argon2id with the label `"vpsman-vault"`, the project's former name. They open with the same password and are rewritten as version 5 at once.
- The file is written to a temporary file with mode 600 and moved into place atomically. If it changed on disk since it was read (another copy of the app), nothing is written.

## Envelopes

Each user has one X25519 key pair, created on first use. The private key stays in the vault; the public key goes to contacts, over the local network or as a key line.

`sealFrom(text, S, R)` seals `text` from the sender's key pair `(s, S)` to the recipient's public key `R`:

1. a throwaway key pair `(e, E)`;
2. `k = HKDF(ikm = DH(e, R) || DH(s, R), salt = E || R || S, info = "lemuri-lan-v1")`;
3. `c = AES-256-GCM(k, iv = 12 random bytes, aad = lower(S) || "\n" || lower(R), text)`.

What travels: `E`, `iv`, `c`. The recipient computes `DH(r, E) || DH(r, S)` and opens it only with the sender's key it already trusts. The second agreement authenticates the sender: only the holder of `s` (or of `r`) can compute `k`. These are the `es` and `ss` tokens of Noise's `K` pattern, and the same idea as HPKE's authenticated mode. A known property: whoever steals Bob's private key can forge envelopes "from anyone" to Bob. That is outside the threat model (a compromised computer).

The AAD holds the two public keys in base64, lowercased (a quirk of the first version, kept for compatibility: the keys are bound to the encryption key anyway through the salt). The `info` label still says `lan` for the same reason.

**Content**: the host (name, address, port, user, key file name, operating system), the password if one was chosen, a message, a random id and the time it was sent. The id and time are *inside* the encryption. An envelope older than 30 days, or one already accepted or declined, is refused, so whoever kept a copy cannot make it look new: the ids of decided envelopes are kept in the vault.

## Contacts

There is no central authority. A contact is a public key the user decided to trust, with a name they chose, kept in the vault. It is added in one of two ways:

- **pairing on the local network** (below), where both users compare a code on their screens;
- **a key line** sent by any channel, `LEMURI-KEY 1 <public key in base64> <suggested name>`. The app shows its fingerprint and adds it only after the user ticks that they compared it with the contact by voice or in person. The fingerprint is the first 16 bytes (128 bits) of SHA-256 of the public key, in hexadecimal: `4BAC 4C42 6D6B B4C4 F044 0E1C E119 3DA5`.

A key that is already a contact, the user's own key, or a name already in use is refused.

## Hosts as text

When the contact's PC is not on the local network, the envelope becomes text:

```
-----BEGIN LEMURI HOST-----
base64 of {"v": 1, "from": S, "to": R, "ephemeral": E, "iv": iv, "data": c}, 64 characters per line
-----END LEMURI HOST-----
```

On paste, everything between the two lines that is not a base64 character is ignored (mail programs re-wrap and quote). It opens only if `to` is the user's key, `from` is a contact, the envelope opens with that contact's key as sender, and the content passes the same checks as any other: valid fields, age and replay.

## Pairing two PCs on the local network

Two PCs pair with a 6-digit code compared by the two users, like Bluetooth's Numeric Comparison:

1. A connects to B and sends its public key `A`.
2. B picks a 32-byte random `nB` and sends its key `B` and `commit = hex(SHA-256("lemuri-commitment-v2\n" || B || "\n" || A || "\n" || nB))`.
3. A sends a 32-byte random `nA`.
4. B reveals `nB`; A checks it against `commit`.
5. Both show `code = (first 4 bytes of SHA-256("lemuri-pairing-v2\n" || A || "\n" || B || "\n" || nA || "\n" || nB), big-endian, top bit cleared) mod 10^6`.

B commits to `nB` before seeing `nA`, so a man in the middle who swaps the keys cannot choose values that make the two screens agree. Their chance is one in a million per attempt. Requests are limited to one at a time, one per minute from each address, and the dialog expires after two minutes. If the users accept, each PC adds the other as a contact.

## Deliveries on the local network

Announcements over UDP are not authenticated: they only say that some PC claims a key at an address. So:

- a delivery is accepted only from a contact, and only if the envelope opens with that key as sender. The envelope id and time are also checked within a 10-minute window, and against the ids already decided;
- the receiver answers with a **receipt**: the delivery id, sealed by the receiver to the sender with `sealFrom`. A delivery without a valid receipt counts as failed, and the app shows the host as text instead;
- for a trusted PC, an announcement from a new address does not replace the known one. The new address is only tried if the known one does not produce a valid receipt, and it becomes the known address once it does.
