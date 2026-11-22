# Fuzz tests

The parsers that anyone can feed, run by libFuzzer (clang) with AddressSanitizer and UBSan:

| Target | What it reads | Who can send it |
|---|---|---|
| `FuzzLanLine` | a line of the local network protocol: hello, delivery (with a trusted sender, past the envelope), anything else | any PC on the network |
| `FuzzEnvelope` | pasted text: a host envelope, a key card, the parcel inside | whoever sends a mail or a chat message |
| `FuzzKnownHosts` | the `~/.ssh` files of a backup: kinds, `known_hosts` merge, markers, commands in a config | whoever made the backup |
| `FuzzSshConfig` | `~/.ssh/config` as the import of hosts reads it, with `Include` | the user's own file, or one copied from elsewhere |

    tests/fuzz.sh 300        # each target for 300 s

A finding is saved in `build-fuzz/` as `crash-…` or `timeout-…`. Run the target on it alone to see the stack:

    build-fuzz/FuzzSshConfig build-fuzz/timeout-…

then turn it into a test in `tests/*Test.cpp` before fixing it (as `SshTest::aConfigIncludingItselfIsReadOnce`, the
first finding: a config including itself was read 2^16 times). The seeds are in `corpus/`; what each run finds is
kept in `build-fuzz/corpus/` for the next one.
