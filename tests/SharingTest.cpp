// Sharing between contacts in the backend: envelopes, key cards, contact names, parcels and the local network, the
// same cases as the first Lemuri's; the envelopes it sealed must keep opening here.
#include "base/Error.h"
#include "base/I18n.h"
#include "sharing/ContactNames.h"
#include "sharing/Envelope.h"
#include "sharing/Lan.h"
#include "sharing/Parcel.h"

#include <QtTest>
#include <arpa/inet.h>
#include <atomic>
#include <future>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
QByteArray hex(const char *h) {
    return QByteArray::fromHex(h);
}

QByteArray range(int from, int to) {
    QByteArray b;
    for (int i = from; i < to; i++) b.append(char(i));
    return b;
}

// Throwaway keys and an envelope made by the first Lemuri: if a change breaks them, old deliveries stop opening.
const Envelope::Keys ALICE{"MCowBQYDK2VuAyEAtxhjHlzQeM3GQbMMIQ4PBXUS2dcQgeiMlj3t37MAG3g=", "MC4CAQAwBQYDK2VuBCIEIICTUKmv97bil+cq2xkX22zCiYkUtZhOhFwe4Z7DQjSF"};
const Envelope::Keys BOB{"MCowBQYDK2VuAyEA4ac3Lgs+xVG2P+0+m8gbkokxHnYyfWwP1E29OXCysz0=", "MC4CAQAwBQYDK2VuBCIEIKTACdIj3nc8JX5kMfs0SMxiOeC38Z8fROiNkKO1dTh6"};

QString open(const Envelope::Sealed &b, const Envelope::Keys &mine, const QString &sender, const QString &from, const QString &to, bool *ok) {
    return Envelope::openFrom(b, mine, sender, from, to, ok);
}

Envelope::Sealed tampered(Envelope::Sealed b) {
    QByteArray d = QByteArray::fromBase64(b.data.toLatin1());
    d[0] = char(d[0] ^ 1);
    b.data = QString::fromLatin1(d.toBase64());
    return b;
}

/** An in-memory vault for a Lan of the test, and what arrives to it. */
struct Memory {
    std::mutex m;
    QHash<QString, QString> vault;
    std::function<void(const Delivery &)> arrivals;
};

Lan::Environment environment(const Envelope::Keys &k, std::shared_ptr<Memory> mem) {
    return {[k] { return k; },
            [mem](const QString &key) {
                std::lock_guard<std::mutex> l(mem->m);
                return mem->vault.value(key);
            },
            [mem](const QString &key, const QString &value) {
                std::lock_guard<std::mutex> l(mem->m);
                mem->vault.insert(key, value);
            },
            [mem](const Delivery &d) {
                if (mem->arrivals) mem->arrivals(d);
            }};
}

Peer toward(const Envelope::Keys &k, int port) {
    return {k.publicKey, "b", QString(), "127.0.0.1", port, QDateTime::currentMSecsSinceEpoch(), true, QString()};
}

QString parcel(const QString &name, const QString &address, const QString &password, const QString &message) {
    Host h = Host::from({{"name", name}, {"address", address}});
    return Parcel::from(h, password, message).toJson();
}

/** A listener that accepts every pairing request and records the code shown. */
Lan::Listener acceptAll(std::shared_ptr<std::promise<QString>> code) {
    Lan::Listener l;
    l.request = [code](const Peer &p, const QString &c) {
        try {
            code->set_value(c);
        } catch (const std::future_error &) {
        }
        return p.who();
    };
    return l;
}

/** A PC on loopback that reads one request and says {"ok":true} to everything, without being able to open it. */
struct Fake {
    int fd = -1, port = 0;
    std::atomic<int> calls{0};
    std::atomic<bool> stopped{false};
    std::thread t;

    Fake() {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof a);
        ::listen(fd, 10);
        socklen_t size = sizeof a;
        ::getsockname(fd, reinterpret_cast<sockaddr *>(&a), &size);
        port = ntohs(a.sin_port);
        t = std::thread([this] {
            for (;;) {
                int c = ::accept(fd, nullptr, nullptr);
                if (c < 0 || stopped) break;
                char b[65536];
                QByteArray got;
                while (!got.contains('\n')) {
                    ssize_t n = ::recv(c, b, sizeof b, 0);
                    if (n <= 0) break;
                    got.append(b, n);
                }
                calls++;
                ::send(c, "{\"ok\":true}\n", 12, MSG_NOSIGNAL);
                ::close(c);
            }
        });
    }
    ~Fake() {
        stopped = true;
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
        t.join();
    }
};

void announce(int udp, const QString &key, int port) {
    QByteArray data = QString("{\"app\":\"lemuri\",\"v\":2,\"type\":\"announce\",\"name\":\"b\",\"key\":\"%1\",\"port\":%2}").arg(key).arg(port).toUtf8();
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(quint16(udp));
    ::sendto(s, data.constData(), size_t(data.size()), 0, reinterpret_cast<sockaddr *>(&a), sizeof a);
    ::close(s);
}

template <typename F>
bool eventually(F test, int ms = 3000) {
    for (int i = 0; i < ms / 50; i++) {
        if (test()) return true;
        QThread::msleep(50);
    }
    return test();
}

bool fails(const std::function<void()> &f, QString *why = nullptr) {
    try {
        f();
        return false;
    } catch (const Fail &e) {
        if (why) *why = e.why;
        return true;
    }
}
}  // namespace

class SharingTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        Q_INIT_RESOURCE(backend);
        I18n::choose("en");
        // Any free ports and no announcements: an app open on this PC does not interfere, preferences untouched.
        qputenv("LEMURI_LAN_PORTS", "0,0");
    }

    // \u2500\u2500 Known answers \u2500\u2500

    void hkdfMatchesRfc5869() {
        QCOMPARE(Envelope::hkdf(QByteArray(22, '\x0b'), range(0x00, 0x0d), range(0xf0, 0xfa)).toHex(),
                 QByteArray("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"));
        QCOMPARE(Envelope::hkdf(range(0x00, 0x50), range(0x60, 0xb0), range(0xb0, 0x100)).toHex(),
                 QByteArray("b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c"));
    }

    /** RFC 7748, section 6.1: Alice and Bob reach the same secret (raw keys little-endian, as the RFC prints them). */
    void x25519MatchesRfc7748() {
        QByteArray shared = "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";
        QCOMPARE(Envelope::agreementRaw(hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a"),
                                        hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f")).toHex(), shared);
        QCOMPARE(Envelope::agreementRaw(hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb"),
                                        hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a")).toHex(), shared);
    }

    void envelopesSealedByTheFirstLemuriStillOpen() {
        Envelope::Sealed fromAlice{"MCowBQYDK2VuAyEAN+C9ihA3cbTaq6I1aeB3U0ramYSsBu0+2mbdJtKYOX0=", "Ye9/mwpqhKGnM9DN", "TRlOBpJ5WD/b5UNP7wLUcijmpyaN0sdnwUc="};
        bool ok = false;
        QCOMPARE(open(fromAlice, BOB, ALICE.publicKey, ALICE.publicKey, BOB.publicKey, &ok), QString("from alice"));
        QVERIFY(ok);
    }

    void pairingValuesAsTheFirstLemuri() {
        QCOMPARE(Envelope::commitment(BOB.publicKey, ALICE.publicKey, "bm9uY2UtYg=="), QString("ead57d8f9a786aa73700bd7bf847c8f504c53dfda231797a358cfadf5099359f"));
        QCOMPARE(Envelope::code(ALICE.publicKey, BOB.publicKey, "bm9uY2UtYQ==", "bm9uY2UtYg=="), QString("273 556"));
        QCOMPARE(Envelope::fingerprint(ALICE.publicKey), QString("4BAC 4C42 6D6B B4C4 F044 0E1C E119 3DA5"));
    }

    /** New keys are X.509 / PKCS#8 in base64, so the first Lemuri reads the vault entries written here. */
    void keysEncodedAsX509AndPkcs8() {
        Envelope::Keys k = Envelope::newKeys();
        QVERIFY(k.publicKey.startsWith("MCowBQYDK2VuAyEA"));
        QVERIFY(k.privateKey.startsWith("MC4CAQAwBQYDK2VuBCIEI"));
        QCOMPARE(k.publicKey.size(), ALICE.publicKey.size());
        QCOMPARE(k.privateKey.size(), ALICE.privateKey.size());
    }

    // \u2500\u2500 Envelopes \u2500\u2500

    void opensOnlyWithTheRightKeysAndPair() {
        Envelope::Keys anna = Envelope::newKeys(), bruno = Envelope::newKeys(), carla = Envelope::newKeys();
        Envelope::Sealed b = Envelope::sealFrom("password segreta", anna, bruno.publicKey, anna.publicKey, bruno.publicKey);
        bool ok = false;
        QCOMPARE(open(b, bruno, anna.publicKey, anna.publicKey, bruno.publicKey, &ok), QString("password segreta"));
        QVERIFY(ok);
        open(b, carla, anna.publicKey, anna.publicKey, bruno.publicKey, &ok);
        QVERIFY(!ok);
        open(b, bruno, anna.publicKey, carla.publicKey, bruno.publicKey, &ok);
        QVERIFY2(!ok, "sender bound to the envelope");
        open(b, bruno, anna.publicKey, anna.publicKey, carla.publicKey, &ok);
        QVERIFY2(!ok, "recipient bound to the envelope");
        open(tampered(b), bruno, anna.publicKey, anna.publicKey, bruno.publicKey, &ok);
        QVERIFY2(!ok, "a changed byte");
        // Carla cannot pose as Anna.
        Envelope::Sealed fake = Envelope::sealFrom("host", carla, bruno.publicKey, anna.publicKey, bruno.publicKey);
        open(fake, bruno, anna.publicKey, anna.publicKey, bruno.publicKey, &ok);
        QVERIFY(!ok);
        Envelope::Sealed x = Envelope::sealFrom("uguale", anna, bruno.publicKey, anna.publicKey, bruno.publicKey);
        Envelope::Sealed y = Envelope::sealFrom("uguale", anna, bruno.publicKey, anna.publicKey, bruno.publicKey);
        QVERIFY(x.data != y.data && x.ephemeral != y.ephemeral);
    }

    void codesCommitmentsFingerprints() {
        Envelope::Keys anna = Envelope::newKeys(), bruno = Envelope::newKeys(), carla = Envelope::newKeys();
        QString na = Envelope::nonce(), nb = Envelope::nonce();
        QString c = Envelope::code(anna.publicKey, bruno.publicKey, na, nb);
        QVERIFY(QRegularExpression("^\\d{3} \\d{3}$").match(c).hasMatch());
        QCOMPARE(Envelope::code(anna.publicKey, bruno.publicKey, na, nb), c);
        QVERIFY(c != Envelope::code(anna.publicKey, bruno.publicKey, na, Envelope::nonce()));
        QVERIFY(c != Envelope::code(anna.publicKey, carla.publicKey, na, nb));
        QString i = Envelope::commitment(bruno.publicKey, anna.publicKey, nb);
        QVERIFY(i != Envelope::commitment(bruno.publicKey, carla.publicKey, nb));
        QVERIFY(QRegularExpression("^[0-9A-F]{4}( [0-9A-F]{4}){7}$").match(Envelope::fingerprint(anna.publicKey)).hasMatch());
        QCOMPARE(Envelope::fingerprint("not base64!"), QString("?"));
    }

    void textEnvelopesSurviveMailAndChat() {
        Envelope::Keys anna = Envelope::newKeys(), bruno = Envelope::newKeys();
        Envelope::Sealed b = Envelope::sealFrom("{\"name\":\"db\"}", anna, bruno.publicKey, anna.publicKey, bruno.publicKey);
        QString text = TextEnvelope::armor(anna.publicKey, bruno.publicKey, b);
        QVERIFY(text.startsWith(TextEnvelope::BEGIN));
        for (const QString &line : text.split('\n')) QVERIFY(line.size() <= 64 || line.startsWith("-----"));
        // Pasted from an email reply: quoted and wrapped again.
        TextEnvelope::Posted p = TextEnvelope::unarmor("Hi, here it is:\n> " + QString(text).replace("\n", "\n> "));
        QCOMPARE(p.from, anna.publicKey);
        QCOMPARE(p.to, bruno.publicKey);
        bool ok = false;
        QCOMPARE(open(p.sealed, bruno, p.from, p.from, p.to, &ok), QString("{\"name\":\"db\"}"));
        QVERIFY(TextEnvelope::unarmor("nothing here").isNull());
        QVERIFY(TextEnvelope::unarmor(TextEnvelope::BEGIN + "\nAAAA\n" + TextEnvelope::END).isNull());
    }

    void keyCards() {
        Envelope::Keys anna = Envelope::newKeys();
        TextEnvelope::KeyCard k = TextEnvelope::parseKey("my key:\n  " + TextEnvelope::keyCard(anna.publicKey, "bruno\u0007 r.") + "  \nbye");
        QCOMPARE(k.key, anna.publicKey);
        QCOMPARE(k.name, QString("bruno r."));
        QVERIFY(TextEnvelope::parseKey("LEMURI-KEY 1 notakey name").isNull());
        QCOMPARE(TextEnvelope::parseKey(TextEnvelope::keyCard(anna.publicKey, "car\u202Ela\u200B")).name, QString("carla"));
    }

    // \u2500\u2500 Contact names \u2500\u2500

    void cleanRemovesInvisibleAndDirectionCharacters() {
        QCOMPARE(ContactNames::clean("bru\u200Bno"), QString("bruno"));
        QCOMPARE(ContactNames::clean("\u202Ebruno\u2066"), QString("bruno"));
        QCOMPARE(ContactNames::clean("  bruno \t\n rossi  "), QString("bruno rossi"));
        QCOMPARE(ContactNames::clean("\uFF42\uFF52\uFF55\uFF4E\uFF4F"), QString("bruno"));
        QCOMPARE(ContactNames::clean(QString()), QString(""));
        QCOMPARE(ContactNames::clean(QString(200, 'x')).size(), ContactNames::MAX);
    }

    void lookAlikesHaveTheSameSkeleton() {
        QString bruno = ContactNames::skeleton("bruno");
        for (const QString &other : {QString("Bruno"), QString("BRUNO"), QString("brun\u043E"), QString("\u0432runo"), QString("brun\u00F3"),
                                     QString("bru no"), QString("brun0")})
            QVERIFY2(ContactNames::skeleton(other) == bruno, qPrintable(other));
        QCOMPARE(ContactNames::skeleton("rnario"), ContactNames::skeleton("mario"));
        QCOMPARE(ContactNames::skeleton("Ilaria"), ContactNames::skeleton("ilaria"));
        QVERIFY(ContactNames::skeleton("bruna") != bruno);
        QList<TrustedPeer> contacts{{"K1", "bruno"}};
        QCOMPARE(ContactNames::takenBy(contacts, "brun\u043E", "K2"), QString("bruno"));
        QVERIFY(ContactNames::takenBy(contacts, "bruno", "K1").isNull());
        QVERIFY(ContactNames::takenBy(contacts, "carla", "K2").isNull());
    }

    void mixedScriptsAndPhoneticLettersAreRefused() {
        QCOMPARE(ContactNames::problem("mari\u0585"), QString("contact.nameMixedScripts"));
        QCOMPARE(ContactNames::problem("brun\u043E"), QString("contact.nameMixedScripts"));
        QCOMPARE(ContactNames::problem("m\u0251rio"), QString("contact.nameOddLetters"));
        QCOMPARE(ContactNames::problem(" \u200B "), QString("contact.nameEmpty"));
        QCOMPARE(ContactNames::problem("mar\u04C0o"), QString("contact.nameMixedScripts"));
        QCOMPARE(ContactNames::takenBy(QList<TrustedPeer>{TrustedPeer{"K1", "anna@pc"}}, "anna\uFE6Bpc", "K2"), QString("anna@pc"));
        for (const QString &ok : {QString("bruno"), QString("Bruno Rossi"), QString("anna@pc-01"), QString("\u041C\u0430\u0440\u0438\u044F"),
                                  QString("\u5C71\u7530\u30BF\u30ED\u30A6"), QString("\uAE40\uBBFC\uC218")})
            QVERIFY2(ContactNames::problem(ok).isNull(), qPrintable(ok));
    }

    // \u2500\u2500 Parcels \u2500\u2500

    void parcelsAreCheckedAndCleaned() {
        bool ok = false;
        Parcel p = Parcel::fromJson(parcel("web\u202E-01\n", "203.0.113.21", "s", "line 1\nline 2\u200B"), &ok);
        QVERIFY(ok);
        QVERIFY(p.admit().isNull());
        QCOMPARE(p.name, QString("web-01"));
        QCOMPARE(p.message, QString("line 1\nline 2"));
        QCOMPARE(p.port, 22);
        QCOMPARE(p.password, QString("s"));
        Parcel bad = Parcel::fromJson("{\"name\":\"x\",\"address\":\"a b\",\"user\":\"u\"}", &ok);
        QVERIFY(!bad.admit().isNull());
        Parcel noPort = Parcel::fromJson("{\"name\":\"x\",\"address\":\"a\",\"user\":\"u\",\"port\":0}", &ok);
        QVERIFY(!noPort.admit().isNull());
        // The sender's key path never comes along: it names a file on their PC.
        QVERIFY(!p.asHost().json.contains("key"));
        QCOMPARE(Parcel::fromJson("{\"name\":\"x\",\"authMethod\":\"CHIAVE\"}", &ok).authMethod, QString("KEY"));
        QVERIFY(Parcel::fromJson("{\"name\":\"x\",\"authMethod\":\"SOMETHING\"}", &ok).asHost().authMethod() == "PASSWORD");
    }

    /** A host from a contact is checked before being offered. */
    void parcelBounds() {
        auto good = [] {
            Parcel p;
            p.name = "web-01";
            p.address = "203.0.113.21";
            p.user = "deploy";
            return p;
        };
        QVERIFY(good().problem().isNull());
        for (int port : {0, -1, 65536, 99999}) {
            Parcel p = good();
            p.port = port;
            QVERIFY2(!p.problem().isNull(), qPrintable(QString::number(port)));
        }
        Parcel p = good();
        p.address = QString();
        QVERIFY(!p.problem().isNull());
        p = good();
        p.user = "  ";
        QVERIFY(!p.problem().isNull());
        p = good();
        p.message = QString(1'000'000, 'x');
        QVERIFY(!p.problem().isNull());
        p = good();
        p.address = "host\n-oProxyCommand=x";
        QVERIFY(!p.problem().isNull());
        // "root@server" would make libssh sign in as root, whatever the user field says; "-o..." looks like an option.
        for (const char *a : {"root@203.0.113.20", "-oProxyCommand=x"}) {
            p = good();
            p.address = a;
            QVERIFY2(!p.problem().isNull(), a);
        }
        p = good();
        p.user = "deploy@other";
        QVERIFY(!p.problem().isNull());
        // The same bounds as the host dialog.
        p = good();
        p.name = QString(64, 'n');
        QVERIFY(p.problem().isNull());
        p.name += "n";
        QVERIFY(!p.problem().isNull());
        p = good();
        p.user = QString(65, 'u');
        QVERIFY(!p.problem().isNull());
        p = good();
        p.address = QString(256, 'a');
        QVERIFY(!p.problem().isNull());
    }

    /** What the acceptance dialog shows cannot be disguised with invisible or bidi characters. */
    void invisibleAndDirectionCharactersDoNotDisguiseTheHost() {
        auto good = [] {
            Parcel p;
            p.name = "web-01";
            p.address = "203.0.113.21";
            p.user = "deploy";
            return p;
        };
        for (const QString &address : {QString("1.2.3.4") + QChar(0x202E), QString("1.2.3.4") + QChar(0x2028) + "evil"}) {
            Parcel p = good();
            p.address = address;
            QVERIFY(!p.admit().isNull());
        }
        Parcel p = good();
        p.user = QString("ro") + QChar(0x200B) + "ot";
        QVERIFY(!p.admit().isNull());
        p = good();
        p.name = QString("prod-db") + QChar(0x202E) + QChar(0x200B);
        p.os = QString("deb") + QChar(0x200B) + "ian";
        p.message = QString("first line\nok") + QChar(0x202E) + "evil" + QChar(7);
        QVERIFY(p.admit().isNull());
        QCOMPARE(p.name, QString("prod-db"));
        QCOMPARE(p.os, QString("debian"));
        QCOMPARE(p.message, QString("first line\nokevil"));
        p = good();
        p.name = "line1\nline2";
        QVERIFY(p.admit().isNull());
        QCOMPARE(p.name, QString("line1 line2"));
        // A name made only of invisible characters is a missing name.
        p = good();
        p.name = QString(QChar(0x200B)) + QChar(0x202E);
        QVERIFY(!p.admit().isNull());
    }

    /** The sender's key path is a file on their PC: here it could name any file of the recipient. */
    void theKeyPathOfTheSenderNeverArrives() {
        Host h = Host::from({{"name", "web-01"}, {"address", "203.0.113.21"}, {"user", "deploy"}, {"authMethod", "KEY"},
                             {"key", "/home/sender/.ssh/id_ed25519"}});
        QVERIFY(!Parcel::from(h, QString(), QString()).toJson().contains("id_ed25519"));
        bool ok = false;
        Parcel old = Parcel::fromJson("{\"name\":\"web-01\",\"address\":\"203.0.113.21\",\"user\":\"deploy\",\"authMethod\":\"KEY\","
                                      "\"key\":\"/home/recipient/.ssh/id_ed25519\\u202Eevil\"}", &ok);
        QVERIFY(ok && old.admit().isNull());
        Host arrived = old.asHost();
        QCOMPARE(arrived.authMethod(), QString("KEY"));
        QVERIFY(arrived.key().isEmpty());
    }

    void replaysAreRefused() {
        bool ok = false;
        Parcel a = Parcel::fromJson(parcel("web-01", "203.0.113.21", QString(), QString()), &ok);
        Parcel b = Parcel::fromJson(parcel("web-01", "203.0.113.21", QString(), QString()), &ok);
        QVERIFY(a.id != b.id);
        QVERIFY(DecidedEnvelopes::check(a, {}, a.when + 60'000).isNull());
        QCOMPARE(DecidedEnvelopes::check(a, {{a.id, a.when}}, a.when + 60'000), QString("sharing.envelope.replay"));
        QCOMPARE(DecidedEnvelopes::check(a, {}, a.when + DecidedEnvelopes::MAX_AGE_MS + 1), QString("sharing.envelope.tooOld"));
        a.id = QString();
        QCOMPARE(DecidedEnvelopes::check(a, {}, a.when), QString("sharing.envelope.old"));
    }

    // \u2500\u2500 Local network \u2500\u2500

    void answersOnlyFromTheLocalNetwork() {
        for (const char *a : {"10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.1.20", "127.0.0.1", "169.254.1.1", "::1",
                              "fe80::1", "fd12:3456::1", "::ffff:192.168.1.2"})
            QVERIFY2(Lan::privateAddress(a), a);
        for (const char *a : {"8.8.8.8", "172.32.0.1", "100.128.0.1", "203.0.113.10", "2001:db8::1", "::ffff:8.8.8.8"})
            QVERIFY2(!Lan::privateAddress(a) && !Lan::privateAddress(a, true), a);
        // 100.64/10 (Tailscale, carrier-grade NAT): strangers on a phone's hotspot, so only if the user says so.
        for (const char *a : {"100.64.0.1", "100.127.255.255", "::ffff:100.100.1.1"}) {
            QVERIFY2(!Lan::privateAddress(a), a);
            QVERIFY2(Lan::privateAddress(a, true), a);
        }
    }

    void contactNamesAreCheckedWhenSaved() {
        auto mem = std::make_shared<Memory>();
        Lan lan(environment(Envelope::newKeys(), mem));
        Envelope::Keys bruno = Envelope::newKeys(), carla = Envelope::newKeys();
        lan.trust(bruno.publicKey, "bruno");
        for (const QString &name : {QString("bruno\u200B"), QString("brun\u043E"), QString("BRUNO"), QString("bru no"), QString("\u202Ebruno"),
                                    QString("brun0"), QString(" ")})
            QVERIFY2(fails([&] { lan.trust(carla.publicKey, name); }), qPrintable(name));
        // Pairing again with the same key keeps it one contact, with the new name.
        lan.trust(bruno.publicKey, "Bruno");
        QCOMPARE(lan.contacts().size(), 1);
        QCOMPARE(lan.contacts()[0].name, QString("Bruno"));
        // Saved in the vault as the first Lemuri saves it.
        QJsonArray saved = QJsonDocument::fromJson(mem->vault.value("lan:trusted").toUtf8()).array();
        QCOMPARE(saved[0].toObject()["key"].toString(), bruno.publicKey);
        lan.forget(bruno.publicKey);
        QVERIFY(lan.contacts().isEmpty());
    }

    void codeMatchesOnBothPcsAndResponderRemembersTheOther() {
        Envelope::Keys ka = Envelope::newKeys(), kb = Envelope::newKeys();
        Lan a(environment(ka, std::make_shared<Memory>())), b(environment(kb, std::make_shared<Memory>()));
        auto codeOfB = std::make_shared<std::promise<QString>>();
        b.listen(acceptAll(codeOfB));
        a.turnOn(true);
        b.turnOn(true);
        QCOMPARE(b.state(), Lan::State::Active);
        // B's user is not waiting for a link: refused, and nothing shows up on B's screen.
        QString why;
        QVERIFY(fails([&] { a.pair(toward(kb, b.tcpPort()), [](const QString &) {}); }, &why));
        QCOMPARE(why, I18n::t("lan.wire.notWaiting"));
        auto f = codeOfB->get_future();
        QCOMPARE(f.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);  // B's user was never asked
        b.waitForLink(true);
        QString codeOfA;
        QVERIFY(a.pair(toward(kb, b.tcpPort()), [&](const QString &c) { codeOfA = c; }));
        QVERIFY(QRegularExpression("^\\d{3} \\d{3}$").match(codeOfA).hasMatch());
        QCOMPARE(f.wait_for(std::chrono::seconds(1)), std::future_status::ready);
        QCOMPARE(f.get(), codeOfA);
        // B accepted: it trusts A. A will trust B only when its user confirms.
        QVERIFY(b.trusted(ka.publicKey));
        QVERIFY(!a.trusted(kb.publicKey));
        // A second request from the same address within a minute is rejected, the reason translated here.
        QVERIFY(fails([&] { a.pair(toward(kb, b.tcpPort()), [](const QString &) {}); }, &why));
        QCOMPARE(why, I18n::t("lan.wire.tooMany"));
    }

    void linkRequestsAreLimitedForAllAddressesTogether() {
        // Changing address (IPv6 privacy, several IPs) does not buy more requests: five a minute in all.
        RequestLimit l;
        qint64 t = 1'000'000;
        QVERIFY(l.allow("10.0.0.1", t));
        QVERIFY(!l.allow("10.0.0.1", t + 1000));  // the same address within a minute
        for (int i = 2; i <= 5; i++) QVERIFY(l.allow(QString("10.0.0.%1").arg(i), t + i));
        QVERIFY(!l.allow("10.0.0.6", t + 10));     // a new address, but six in a minute already
        QVERIFY(!l.allow("fd00::7", t + 20));
        // A minute after the first ones, room again.
        QVERIFY(l.allow("10.0.0.8", t + RequestLimit::PAUSE_MS + 30));
        QVERIFY(l.allow("10.0.0.1", t + 2 * RequestLimit::PAUSE_MS));
    }

    void oneAddressCannotTakeEveryConnection() {
        // Four silent connections from one address are kept (until their deadline); a fifth is closed at once.
        Lan b(environment(Envelope::newKeys(), std::make_shared<Memory>()));
        b.turnOn(true);
        auto connect = [&] {
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(quint16(b.tcpPort()));
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a);
            return fd;
        };
        auto closedSoon = [](int fd) {
            pollfd p{fd, POLLIN, 0};
            char c;
            return ::poll(&p, 1, 1500) == 1 && ::recv(fd, &c, 1, 0) == 0;
        };
        QList<int> kept;
        for (int i = 0; i < 4; i++) kept << connect();
        QTest::qWait(200);
        int fifth = connect();
        QVERIFY(closedSoon(fifth));
        QVERIFY(!closedSoon(kept[0]));
        for (int fd : kept) ::close(fd);
        ::close(fifth);
    }

    void aPairedPcReceivesTheHostAndAStrangerIsRefused() {
        Envelope::Keys ka = Envelope::newKeys(), kb = Envelope::newKeys();
        auto atB = std::make_shared<Memory>();
        auto arrived = std::make_shared<std::promise<Delivery>>();
        atB->arrivals = [arrived](const Delivery &d) { arrived->set_value(d); };
        Lan a(environment(ka, std::make_shared<Memory>())), b(environment(kb, atB));
        b.listen(acceptAll(std::make_shared<std::promise<QString>>()));
        a.turnOn(true);
        b.turnOn(true);
        Peer towardB = toward(kb, b.tcpPort());
        b.waitForLink(true);
        QVERIFY(a.pair(towardB, [](const QString &) {}));
        a.trust(kb.publicKey, "b");
        a.send(towardB, parcel("web-01", "203.0.113.21", QString(), "hi"));
        auto f = arrived->get_future();
        QCOMPARE(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
        Delivery d = f.get();
        QCOMPARE(d.content.name, QString("web-01"));
        QCOMPARE(d.content.message, QString("hi"));
        QVERIFY(d.id.startsWith("lan:"));
        // A third PC that trusts B on its own, but B never paired with it: B refuses what it sends.
        Lan c(environment(Envelope::newKeys(), std::make_shared<Memory>()));
        c.turnOn(true);
        c.trust(kb.publicKey, "b");
        QString why;
        QVERIFY(fails([&] { c.send(towardB, "{}"); }, &why));
        QVERIFY2(why.contains(I18n::t("lan.wire.notLinked")), qPrintable(why));
    }

    /** A PC on the same network announces the key of a trusted PC with its own port, and answers {"ok":true} to everything. */
    void anOkWithoutReceiptIsNotADelivery() {
        Envelope::Keys ka = Envelope::newKeys(), kb = Envelope::newKeys();
        auto atB = std::make_shared<Memory>();
        auto arrived = std::make_shared<std::promise<Delivery>>();
        atB->arrivals = [arrived](const Delivery &d) {
            try {
                arrived->set_value(d);
            } catch (const std::future_error &) {
            }
        };
        Lan a(environment(ka, std::make_shared<Memory>())), b(environment(kb, atB));
        a.turnOn(true);
        b.turnOn(true);
        // Paired in the past, on both sides.
        a.trust(kb.publicKey, "b");
        b.trust(ka.publicKey, "a");
        Fake fake;
        // The fake is the first to announce B's key: it is the only address A knows.
        announce(a.udpPort(), kb.publicKey, fake.port);
        QVERIFY(eventually([&] { return !a.reachable(kb.publicKey).isNull(); }));
        Peer p = a.reachable(kb.publicKey);
        QCOMPARE(p.port, fake.port);
        QVERIFY(fails([&] { a.send(p, parcel("db-prod", "203.0.113.5", "secret", "new db")); }));
        QCOMPARE(fake.calls.load(), 1);
        // B's real announcement does not replace the address: it is kept aside and tried when the first one fails.
        announce(a.udpPort(), kb.publicKey, b.tcpPort());
        QThread::msleep(300);
        p = a.reachable(kb.publicKey);
        QCOMPARE(p.port, fake.port);
        a.send(p, parcel("db-prod", "203.0.113.5", "secret", "new db"));
        auto f = arrived->get_future();
        QCOMPARE(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
        QCOMPARE(f.get().content.name, QString("db-prod"));
        QCOMPARE(a.reachable(kb.publicKey).port, b.tcpPort());
        // Once proven, another announcement of the fake no longer moves it.
        announce(a.udpPort(), kb.publicKey, fake.port);
        QThread::msleep(300);
        QCOMPARE(a.reachable(kb.publicKey).port, b.tcpPort());
        int before = fake.calls;
        a.send(a.reachable(kb.publicKey), parcel("db-prod", "203.0.113.5", "secret", "new db"));
        QCOMPARE(fake.calls.load(), before);
    }

    /** A reply shaped by someone else (objects where values go) is a failure, never a crash. */
    void malformedHelloRepliesAreNotFatal() {
        Lan lan(environment(Envelope::newKeys(), std::make_shared<Memory>()));
        lan.turnOn(true);
        QString key = Envelope::newKeys().publicKey;
        for (const QString &reply : {QString("{\"app\":{}}"), "{\"app\":\"lemuri\",\"key\":\"" + key + "\",\"port\":{}}", QString("[1,2]"),
                                     QString("{\"ok\":{}}"), QString("not json")}) {
            int s = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ::bind(s, reinterpret_cast<sockaddr *>(&a), sizeof a);
            ::listen(s, 5);
            socklen_t size = sizeof a;
            ::getsockname(s, reinterpret_cast<sockaddr *>(&a), &size);
            std::thread t([s, reply] {
                int c = ::accept(s, nullptr, nullptr);
                char b[4096];
                ::recv(c, b, sizeof b, 0);
                QByteArray r = (reply + "\n").toUtf8();
                ::send(c, r.constData(), size_t(r.size()), MSG_NOSIGNAL);
                ::close(c);
            });
            try {
                lan.addAddress("127.0.0.1:" + QString::number(ntohs(a.sin_port)));
            } catch (const Fail &) {
                // the only way it may fail
            }
            t.join();
            ::close(s);
        }
    }
};

QTEST_GUILESS_MAIN(SharingTest)
#include "SharingTest.moc"
