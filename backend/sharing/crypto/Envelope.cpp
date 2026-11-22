#include "sharing/crypto/Envelope.h"

#include "base/system/Handles.h"
#include "sharing/team/ContactNames.h"
#include "vault/Crypto.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <memory>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <stdexcept>

namespace {
const QByteArray LAN_INFO = "lemuri-lan-v1";

using Pkey = Owned<EVP_PKEY, EVP_PKEY_free>;
using PkeyContext = Owned<EVP_PKEY_CTX, EVP_PKEY_CTX_free>;
using Pkcs8 = Owned<PKCS8_PRIV_KEY_INFO, PKCS8_PRIV_KEY_INFO_free>;

/** OpenSSL's own buffers, of a given size, wiped before they are freed. */
struct ClearFree {
    size_t size;
    void operator()(unsigned char *p) const { OPENSSL_clear_free(p, size); }
};

Pkey none() {
    return Pkey();
}

Pkey publicKey(const QString &b64) {
    bool ok = false;
    QByteArray der = Envelope::fromBase64(b64, &ok);
    if (!ok || der.isEmpty()) return none();
    const auto *p = reinterpret_cast<const unsigned char *>(der.constData());
    Pkey k(d2i_PUBKEY(nullptr, &p, long(der.size())));
    // The whole text must be the key, and an X25519 one.
    if (!k || p != reinterpret_cast<const unsigned char *>(der.constData()) + der.size() || !EVP_PKEY_is_a(k.get(), "X25519")) return none();
    return k;
}

Pkey privateKey(const QString &b64) {
    bool ok = false;
    QByteArray der = Envelope::fromBase64(b64, &ok);
    const auto *p = reinterpret_cast<const unsigned char *>(der.constData());
    Pkey k(ok ? d2i_AutoPrivateKey(nullptr, &p, long(der.size())) : nullptr);
    Crypto::wipe(der);
    if (!k || !EVP_PKEY_is_a(k.get(), "X25519")) throw std::runtime_error("Not an X25519 private key");
    return k;
}

QByteArray encodedPublic(EVP_PKEY *k) {
    unsigned char *der = nullptr;
    int n = i2d_PUBKEY(k, &der);
    std::unique_ptr<unsigned char, ClearFree> owned(der, ClearFree{size_t(std::max(n, 0))});
    if (n <= 0) throw std::runtime_error("Cannot encode a public key");
    return QByteArray(reinterpret_cast<const char *>(der), n);
}

QByteArray encodedPrivate(EVP_PKEY *k) {
    Pkcs8 info(EVP_PKEY2PKCS8(k));
    if (!info) throw std::runtime_error("Cannot encode a private key");
    unsigned char *der = nullptr;
    int n = i2d_PKCS8_PRIV_KEY_INFO(info.get(), &der);
    std::unique_ptr<unsigned char, ClearFree> owned(der, ClearFree{size_t(std::max(n, 0))});
    if (n <= 0) throw std::runtime_error("Cannot encode a private key");
    return QByteArray(reinterpret_cast<const char *>(der), n);
}

Pkey generate() {
    Pkey k(EVP_PKEY_Q_keygen(nullptr, nullptr, "X25519"));
    if (!k) throw std::runtime_error("Cannot create an X25519 key");
    return k;
}

QByteArray agreement(EVP_PKEY *priv, EVP_PKEY *pub) {
    PkeyContext context(EVP_PKEY_CTX_new(priv, nullptr));
    EVP_PKEY_CTX *c = context.get();
    size_t n = 0;
    QByteArray out;
    bool ok = c && EVP_PKEY_derive_init(c) == 1 && EVP_PKEY_derive_set_peer(c, pub) == 1 && EVP_PKEY_derive(c, nullptr, &n) == 1;
    if (ok) {
        out.resize(qsizetype(n));
        ok = EVP_PKEY_derive(c, reinterpret_cast<unsigned char *>(out.data()), &n) == 1;
    }
    // OpenSSL refuses an all-zero result (a small-order key), as RFC 7748 advises.
    if (!ok) throw std::runtime_error("X25519 agreement failed");
    return out;
}

/**
 * Two X25519 agreements: throwaway key with recipient, and sender with recipient. Sending: (ephemeral, mine,
 * recipient, recipient); receiving: (mine, mine, ephemeral, sender). The salt binds the key to the three public keys.
 */
QByteArray derive(EVP_PKEY *privOne, EVP_PKEY *privTwo, EVP_PKEY *pubOne, EVP_PKEY *pubTwo, const QByteArray &pubEphemeral,
                  const QByteArray &pubRecipient, const QByteArray &pubSender) {
    QByteArray one = agreement(privOne, pubOne), two = agreement(privTwo, pubTwo);
    QByteArray secret = one + two;
    QByteArray key = Envelope::hkdf(secret, pubEphemeral + pubRecipient + pubSender, LAN_INFO);
    Crypto::wipe(one);
    Crypto::wipe(two);
    Crypto::wipe(secret);
    return key;
}

QByteArray aad(const QString &from, const QString &to) {
    return (from.toLower() + "\n" + to.toLower()).toUtf8();
}

QString b64(const QByteArray &b) {
    return QString::fromLatin1(b.toBase64());
}
}  // namespace

QByteArray Envelope::fromBase64(const QString &b64, bool *ok) {
    auto r = QByteArray::fromBase64Encoding(b64.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    // Latin-1 would turn other characters into '?': not base64 either.
    bool good = r.decodingStatus == QByteArray::Base64DecodingStatus::Ok && QString::fromLatin1(b64.toLatin1()) == b64;
    if (ok) *ok = good;
    return good ? r.decoded : QByteArray();
}

Envelope::Keys Envelope::newKeys() {
    Pkey k = generate();
    QByteArray priv = encodedPrivate(k.get());
    Keys r{b64(encodedPublic(k.get())), b64(priv)};
    Crypto::wipe(priv);
    return r;
}

Envelope::Sealed Envelope::sealFrom(const QString &text, const Keys &mine, const QString &publicRecipient, const QString &from,
                                    const QString &to) {
    Pkey dest = publicKey(publicRecipient);
    if (!dest) throw std::runtime_error("Not an X25519 public key");
    Pkey eph = generate(), me = privateKey(mine.privateKey);
    QByteArray destEncoded = encodedPublic(dest.get());
    QByteArray key = derive(eph.get(), me.get(), dest.get(), dest.get(), encodedPublic(eph.get()), destEncoded, fromBase64(mine.publicKey));
    QByteArray iv = Crypto::random(12);
    QByteArray data = Crypto::sealGcm(key, iv, text.toUtf8(), aad(from, to));
    Crypto::wipe(key);
    return {b64(encodedPublic(eph.get())), b64(iv), b64(data)};
}

QString Envelope::openFrom(const Sealed &b, const Keys &mine, const QString &publicSender, const QString &from, const QString &to, bool *ok) {
    *ok = false;
    Pkey eph = publicKey(b.ephemeral), sender = publicKey(publicSender);
    bool good = false;
    QByteArray iv = fromBase64(b.iv, &good), data;
    if (good) data = fromBase64(b.data, &good);
    if (!eph || !sender || !good || iv.isEmpty()) return QString();
    try {
        Pkey me = privateKey(mine.privateKey);
        QByteArray key = derive(me.get(), me.get(), eph.get(), sender.get(), encodedPublic(eph.get()), fromBase64(mine.publicKey),
                                encodedPublic(sender.get()));
        QByteArray plain = Crypto::openGcm(key, iv, data, aad(from, to), ok);
        Crypto::wipe(key);
        QString text = *ok ? QString::fromUtf8(plain) : QString();
        Crypto::wipe(plain);
        return text;
    } catch (const std::runtime_error &) {
        *ok = false;
        return QString();
    }
}

QString Envelope::nonce() {
    return b64(Crypto::random(32));
}

QString Envelope::commitment(const QString &publicMine, const QString &publicOther, const QString &myNonce) {
    return QString::fromLatin1(Crypto::sha256(("lemuri-commitment-v2\n" + publicMine + "\n" + publicOther + "\n" + myNonce).toUtf8()).toHex());
}

QString Envelope::code(const QString &publicAsker, const QString &publicResponder, const QString &nonceAsker, const QString &nonceResponder) {
    QByteArray h = Crypto::sha256(("lemuri-pairing-v2\n" + publicAsker + "\n" + publicResponder + "\n" + nonceAsker + "\n" + nonceResponder).toUtf8());
    auto u = [&](int i) { return quint32(quint8(h[i])); };
    quint32 n = (u(0) << 24 | u(1) << 16 | u(2) << 8 | u(3)) & 0x7fffffffu;
    QString c = QString::number(n % 1000000).rightJustified(6, '0');
    return c.left(3) + " " + c.mid(3);
}

QString Envelope::fingerprint(const QString &publicKey) {
    bool ok = false;
    QByteArray der = fromBase64(publicKey, &ok);
    if (!ok) return "?";
    QByteArray h = Crypto::sha256(der).left(16).toHex().toUpper();
    QString s;
    for (int i = 0; i < h.size(); i += 4) {
        if (i > 0) s += ' ';
        s += QString::fromLatin1(h.mid(i, 4));
    }
    return s;
}

bool Envelope::isKey(const QString &b64) {
    return bool(publicKey(b64));
}

QByteArray Envelope::agreementRaw(const QByteArray &privateRaw, const QByteArray &publicRaw) {
    Pkey priv(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, reinterpret_cast<const unsigned char *>(privateRaw.constData()),
                                           size_t(privateRaw.size())));
    Pkey pub(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, reinterpret_cast<const unsigned char *>(publicRaw.constData()),
                                         size_t(publicRaw.size())));
    if (!priv || !pub) throw std::runtime_error("Bad raw X25519 key");
    return agreement(priv.get(), pub.get());
}

QByteArray Envelope::hkdf(const QByteArray &secret, const QByteArray &salt, const QByteArray &info) {
    QByteArray prk = Crypto::hmacSha256(salt, secret);
    QByteArray okm = Crypto::hmacSha256(prk, info + QByteArray(1, '\x01'));
    Crypto::wipe(prk);
    return okm;
}

// ── Text forms ──

QString TextEnvelope::keyCard(const QString &publicKey, const QString &name) {
    return KEY_PREFIX + publicKey + " " + ContactNames::clean(name);
}

TextEnvelope::KeyCard TextEnvelope::parseKey(const QString &text) {
    static const QRegularExpression lines("\\r\\n|[\\n\\x0B\\f\\r\\x{85}\\x{2028}\\x{2029}]");
    for (const QString &line : text.split(lines)) {
        QString l = ContactNames::strip(line);
        if (!l.startsWith(KEY_PREFIX)) continue;
        QString rest = l.mid(KEY_PREFIX.size());
        qsizetype space = rest.indexOf(' ');
        QString key = space < 0 ? rest : rest.left(space);
        if (!Envelope::isKey(key)) return {};
        return {key, space < 0 ? QString("") : ContactNames::clean(rest.mid(space + 1))};
    }
    return {};
}

QString TextEnvelope::armor(const QString &from, const QString &to, const Envelope::Sealed &b) {
    QJsonObject o{{"v", 1}, {"from", from}, {"to", to}, {"ephemeral", b.ephemeral}, {"iv", b.iv}, {"data", b.data}};
    QByteArray all = QJsonDocument(o).toJson(QJsonDocument::Compact).toBase64();
    QString body;
    for (qsizetype i = 0; i < all.size(); i += 64) {
        if (i > 0) body += '\n';
        body += QString::fromLatin1(all.mid(i, 64));
    }
    return BEGIN + "\n" + body + "\n" + END + "\n";
}

TextEnvelope::Posted TextEnvelope::unarmor(const QString &text) {
    if (text.size() > MAX_TEXT) return {};
    qsizetype a = text.indexOf(BEGIN), z = text.indexOf(END);
    if (a < 0 || z < a) return {};
    // Mail and chat programs re-wrap lines and quote them with "> ": only base64 characters count.
    static const QRegularExpression other("[^A-Za-z0-9+/=]");
    QString body = text.mid(a + BEGIN.size(), z - a - BEGIN.size()).remove(other);
    bool ok = false;
    QByteArray json = Envelope::fromBase64(body, &ok);
    if (!ok) return {};
    QJsonDocument d = QJsonDocument::fromJson(json);
    if (!d.isObject()) return {};
    QJsonObject o = d.object();
    auto text_ = [&](const char *k) { return o[k].isString() ? o[k].toString() : o[k].isDouble() ? QString::number(o[k].toDouble()) : QString(); };
    QJsonValue v = o["v"];
    if (!(v.isDouble() ? v.toDouble() == 1 : v.isString() && v.toString().trimmed() == "1")) return {};
    QString from = text_("from"), to = text_("to");
    if (!Envelope::isKey(from) || !Envelope::isKey(to)) return {};
    if (!o["ephemeral"].isString() || !o["iv"].isString() || !o["data"].isString()) return {};
    return {from, to, {o["ephemeral"].toString(), o["iv"].toString(), o["data"].toString()}};
}
