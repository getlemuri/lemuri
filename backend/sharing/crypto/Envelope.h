#pragma once

#include <QByteArray>
#include <QString>

/**
 * Encryption for a single recipient, like a sealed envelope: X25519 with a throwaway key and with the sender's key,
 * HKDF-SHA256 and AES-256-GCM. Only the recipient's private key opens the envelope, and only if it was sealed by the
 * sender's. Sender and recipient are part of the authenticated data. Specified in the first Lemuri's docs/crypto.md:
 * the two apps open each other's envelopes.
 *
 * Keys travel in base64: public keys as X.509 SubjectPublicKeyInfo, private keys as PKCS#8.
 * Thread-safe (no state).
 */
namespace Envelope {
/** What travels: throwaway public key, IV and encrypted data, in base64. */
struct Sealed {
    QString ephemeral, iv, data;
};
/** The user's key pair in base64: the public key goes to the contacts, the private one into the vault. */
struct Keys {
    QString publicKey, privateKey;
    bool isNull() const { return publicKey.isEmpty() || privateKey.isEmpty(); }
};

Keys newKeys();
/** Seals `text` for `publicRecipient`, proving it comes from `mine`. Throws std::runtime_error. */
Sealed sealFrom(const QString &text, const Keys &mine, const QString &publicRecipient, const QString &from, const QString &to);
/** The text, if it was really sealed by whoever holds the private key of `publicSender`; *ok false otherwise. */
QString openFrom(const Sealed &b, const Keys &mine, const QString &publicSender, const QString &from, const QString &to, bool *ok);

/** A random number for a pairing: 32 bytes in base64. */
QString nonce();
/** The commitment of whoever receives a pairing request, binding their random number to the two keys. */
QString commitment(const QString &publicMine, const QString &publicOther, const QString &myNonce);
/** The 6-digit code ("123 456") to pair two PCs, from who asked to who answers, with the two random numbers. */
QString code(const QString &publicAsker, const QString &publicResponder, const QString &nonceAsker, const QString &nonceResponder);
/** 128-bit fingerprint of a public key, to compare by voice: "4BAC 4C42 ..."; "?" if it is not base64. */
QString fingerprint(const QString &publicKey);
/** True if `b64` is an X25519 public key. */
bool isKey(const QString &b64);

/** X25519 (RFC 7748) on raw 32-byte values, for the test vectors. */
QByteArray agreementRaw(const QByteArray &privateRaw, const QByteArray &publicRaw);
/** HKDF-SHA256 (RFC 5869), one output block: 32 bytes. */
QByteArray hkdf(const QByteArray &secret, const QByteArray &salt, const QByteArray &info);
/** Strict base64 (RFC 4648, no line breaks): *ok false on any other character. */
QByteArray fromBase64(const QString &b64, bool *ok = nullptr);
}  // namespace Envelope

/**
 * The text forms that people pass on by hand: a contact's public key on one line,
 * and a host sealed for one contact between BEGIN and END lines.
 */
namespace TextEnvelope {
inline const QString KEY_PREFIX = QStringLiteral("LEMURI-KEY 1 ");
inline const QString BEGIN = QStringLiteral("-----BEGIN LEMURI HOST-----");
inline const QString END = QStringLiteral("-----END LEMURI HOST-----");
/** A host envelope is a few KB: beyond this the text is not one. */
constexpr int MAX_TEXT = 64 * 1024;

struct KeyCard {
    QString key, name;
    bool isNull() const { return key.isEmpty(); }
};
/** What travels in a host envelope: who sealed it, for whom, and the sealed content. */
struct Posted {
    QString from, to;
    Envelope::Sealed sealed;
    bool isNull() const { return from.isEmpty(); }
};

QString keyCard(const QString &publicKey, const QString &name);
/** The key card found in `text`; null if there is none or the key is not an X25519 public key. */
KeyCard parseKey(const QString &text);
QString armor(const QString &from, const QString &to, const Envelope::Sealed &b);
/** The envelope between BEGIN and END, wherever it was pasted; null if there is none or it is malformed. */
Posted unarmor(const QString &text);
}  // namespace TextEnvelope
