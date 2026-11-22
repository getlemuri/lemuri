#pragma once

#include <QByteArray>
#include <QString>

/** The primitives of the vault, the backups and the envelopes (OpenSSL, libargon2): the same as the first Lemuri's. */
namespace Crypto {
QByteArray random(int n);
QByteArray sha256(const QByteArray &data);
QByteArray hmacSha256(const QByteArray &key, const QByteArray &data);
/** PBKDF2-HMAC-SHA256 of the password's UTF-8 bytes. */
QByteArray pbkdf2(const QString &password, const QByteArray &salt, int iterations, int bytes = 32);
/** Argon2id version 0x13 of the password's UTF-8 bytes, as Bouncy Castle's Argon2BytesGenerator. */
QByteArray argon2id(const QString &password, const QByteArray &salt, int passes, int memoryKb, int lanes, int bytes = 32);
/** AES-GCM with a 128-bit tag after the cipher text. */
QByteArray sealGcm(const QByteArray &key, const QByteArray &iv, const QByteArray &plain, const QByteArray &aad = {});
/** Null if the tag does not match (wrong key or data changed). */
QByteArray openGcm(const QByteArray &key, const QByteArray &iv, const QByteArray &sealed, const QByteArray &aad = {}, bool *ok = nullptr);
/** Compares without telling how much matched. */
bool same(const QByteArray &a, const QByteArray &b);
void wipe(QByteArray &b);
/** The same for a string, if no other copy shares its memory (then only this one is let go). */
void wipe(QString &s);
}  // namespace Crypto
