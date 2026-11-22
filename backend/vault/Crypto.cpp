#include "vault/Crypto.h"

#include "base/system/Handles.h"

#include <argon2.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <stdexcept>

QByteArray Crypto::random(int n) {
    QByteArray b(n, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char *>(b.data()), n) != 1) throw std::runtime_error("No randomness");
    return b;
}

QByteArray Crypto::sha256(const QByteArray &data) {
    QByteArray out(32, '\0');
    unsigned int n = 0;
    EVP_Digest(data.constData(), size_t(data.size()), reinterpret_cast<unsigned char *>(out.data()), &n, EVP_sha256(), nullptr);
    return out;
}

QByteArray Crypto::hmacSha256(const QByteArray &key, const QByteArray &data) {
    QByteArray out(32, '\0');
    unsigned int n = 0;
    HMAC(EVP_sha256(), key.constData(), int(key.size()), reinterpret_cast<const unsigned char *>(data.constData()), size_t(data.size()),
         reinterpret_cast<unsigned char *>(out.data()), &n);
    return out;
}

QByteArray Crypto::pbkdf2(const QString &password, const QByteArray &salt, int iterations, int bytes) {
    QByteArray p = password.toUtf8(), out(bytes, '\0');
    int ok = PKCS5_PBKDF2_HMAC(p.constData(), int(p.size()), reinterpret_cast<const unsigned char *>(salt.constData()), int(salt.size()),
                               iterations, EVP_sha256(), bytes, reinterpret_cast<unsigned char *>(out.data()));
    wipe(p);
    if (ok != 1) throw std::runtime_error("PBKDF2 failed");
    return out;
}

QByteArray Crypto::argon2id(const QString &password, const QByteArray &salt, int passes, int memoryKb, int lanes, int bytes) {
    QByteArray p = password.toUtf8(), out(bytes, '\0');
    int r = argon2id_hash_raw(uint32_t(passes), uint32_t(memoryKb), uint32_t(lanes), p.constData(), size_t(p.size()), salt.constData(),
                              size_t(salt.size()), out.data(), size_t(bytes));
    wipe(p);
    if (r != ARGON2_OK) throw std::runtime_error(std::string("Argon2: ") + argon2_error_message(r));
    return out;
}

namespace {
/** AES-128 or AES-256 by the key's length; any other length is a mistake, not a weaker cipher. */
const EVP_CIPHER *gcmFor(const QByteArray &key) {
    if (key.size() == 16) return EVP_aes_128_gcm();
    if (key.size() == 32) return EVP_aes_256_gcm();
    throw std::runtime_error("AES-GCM: a key of " + std::to_string(key.size()) + " bytes");
}

using CipherContext = Owned<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>;

CipherContext newContext() {
    CipherContext c(EVP_CIPHER_CTX_new());
    if (!c) throw std::runtime_error("AES-GCM: out of memory");
    return c;
}
}  // namespace

QByteArray Crypto::sealGcm(const QByteArray &key, const QByteArray &iv, const QByteArray &plain, const QByteArray &aad) {
    const EVP_CIPHER *cipher = gcmFor(key);
    CipherContext context = newContext();
    EVP_CIPHER_CTX *c = context.get();
    QByteArray out(plain.size() + 16, '\0');
    int n = 0, m = 0;
    auto *o = reinterpret_cast<unsigned char *>(out.data());
    bool ok = EVP_EncryptInit_ex(c, cipher, nullptr, nullptr, nullptr) == 1
              && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, int(iv.size()), nullptr) == 1
              && EVP_EncryptInit_ex(c, nullptr, nullptr, reinterpret_cast<const unsigned char *>(key.constData()),
                                    reinterpret_cast<const unsigned char *>(iv.constData())) == 1
              && (aad.isEmpty() || EVP_EncryptUpdate(c, nullptr, &n, reinterpret_cast<const unsigned char *>(aad.constData()), int(aad.size())) == 1)
              && EVP_EncryptUpdate(c, o, &n, reinterpret_cast<const unsigned char *>(plain.constData()), int(plain.size())) == 1
              && EVP_EncryptFinal_ex(c, o + n, &m) == 1
              && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, o + n + m) == 1;
    if (!ok) throw std::runtime_error("AES-GCM failed");
    return out;
}

QByteArray Crypto::openGcm(const QByteArray &key, const QByteArray &iv, const QByteArray &sealed, const QByteArray &aad, bool *good) {
    if (good) *good = false;
    if (sealed.size() < 16) return QByteArray();
    const EVP_CIPHER *cipher = gcmFor(key);
    CipherContext context = newContext();
    EVP_CIPHER_CTX *c = context.get();
    qsizetype len = sealed.size() - 16;
    QByteArray out(len, '\0'), tag = sealed.right(16);
    int n = 0, m = 0;
    auto *o = reinterpret_cast<unsigned char *>(out.data());
    bool ok = EVP_DecryptInit_ex(c, cipher, nullptr, nullptr, nullptr) == 1
              && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, int(iv.size()), nullptr) == 1
              && EVP_DecryptInit_ex(c, nullptr, nullptr, reinterpret_cast<const unsigned char *>(key.constData()),
                                    reinterpret_cast<const unsigned char *>(iv.constData())) == 1
              && (aad.isEmpty() || EVP_DecryptUpdate(c, nullptr, &n, reinterpret_cast<const unsigned char *>(aad.constData()), int(aad.size())) == 1)
              && EVP_DecryptUpdate(c, o, &n, reinterpret_cast<const unsigned char *>(sealed.constData()), int(len)) == 1
              && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag.data()) == 1
              && EVP_DecryptFinal_ex(c, o + n, &m) == 1;
    if (!ok) {
        wipe(out);
        return QByteArray();
    }
    if (good) *good = true;
    return out;
}

bool Crypto::same(const QByteArray &a, const QByteArray &b) {
    return a.size() == b.size() && CRYPTO_memcmp(a.constData(), b.constData(), size_t(a.size())) == 0;
}

void Crypto::wipe(QString &s) {
    // Only when this is the sole owner: writing into shared data would first copy it, leaving the original as it was.
    if (!s.isDetached()) {
        s.clear();
        return;
    }
    OPENSSL_cleanse(s.data(), size_t(s.size()) * sizeof(QChar));
    s.clear();
}

void Crypto::wipe(QByteArray &b) {
    if (!b.isEmpty()) OPENSSL_cleanse(b.data(), size_t(b.size()));
    b.clear();
}
