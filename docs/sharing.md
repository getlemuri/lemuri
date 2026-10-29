# Sharing hosts with your team

Send a host, with its password if you want, to a contact without pasting credentials in chat. There is no server and no account to set up: the host is sealed for one person, signed by you, and travels either directly over the local network or as a block of text that you pass on by any channel you like.

## Contacts

You can send hosts only to contacts, and open hosts only from contacts. A contact is a public key you trust, with a name you choose. There are two ways to add one:

- **On the same network**: turn on local network sharing in *Team*, find the other PC in the list and *Link* it. The same 6-digit code appears on both screens; if it matches, each PC becomes a contact of the other.
- **From a distance**: in *Team → Share my key*, copy your key line (`LEMURI-KEY 1 …`) and send it to your contact by email or chat; they do the same. Each of you adds the other in *Team → Add a contact*. **Then compare the fingerprint by voice or in person**, not in the same message as the key: whoever can change the message could swap the key, but not the fingerprint you hear from your contact. Lemuri adds the contact only after you tick that you compared it.

## Sending

Right-click a host → *Send to team*, choose the contacts and whether to include the saved password. A private key is never sent, nor where yours is: with a key, the recipient signs in with their own (from the agent or `~/.ssh`, or the one they choose by editing the host), which must be authorized on the server.

- If a contact's PC is on the local network, the host goes straight to it, and their PC confirms it with a receipt signed by its key.
- Otherwise Lemuri shows the host as encrypted text between `-----BEGIN LEMURI HOST-----` and `-----END LEMURI HOST-----`. Copy it and send it by email, chat, a file or a USB stick: only that contact can open it, and if anyone changes it, it no longer opens.

## Receiving

A host sent over the local network appears at once. One sent as text: copy it, then *Team → Paste a received host* (the lines around it, quotes and line breaks added by mail programs do not matter). Either way you see who sent it and their fingerprint, and you accept it into a folder of your choice or reject it.

Every envelope carries, inside the encryption, a random id and the time it was sent. One older than 30 days, or one you already accepted or rejected, is refused: whoever kept a copy cannot make it look new.

Incoming hosts are kept only in memory: one you have not accepted when you close Lemuri is gone, and can be pasted again.

## What is protected, and what is not

- Nobody but the recipient can read a host, wherever the text goes. Nobody can forge one from a contact without that contact's private key, which never leaves their vault.
- The text shows nothing about the host, but whoever sees it knows that you sent something to someone.
- The keys are in your vault: with the vault locked you can neither send nor open hosts.
- Removing a contact does not take back what you already sent them.

The exact construction is in [crypto.md](crypto.md).
