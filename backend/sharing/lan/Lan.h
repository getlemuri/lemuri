#pragma once

#include "base/core/Runtime.h"
#include "sharing/team/ContactNames.h"
#include "sharing/crypto/Envelope.h"
#include "sharing/crypto/Parcel.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

/**
 * How many link requests are answered: one a minute from each address, and five a minute from all of them together,
 * so that changing address (IPv6 privacy, several IPs) does not buy more. Not thread-safe: Lan holds its lock.
 */
class RequestLimit {
public:
    static constexpr qint64 PAUSE_MS = 60'000;
    static constexpr int PER_MINUTE = 5;
    /** Whether a request from `from` at time `t` (ms) is answered; a refused one still delays that address. */
    bool allow(const QString &from, qint64 t);
    /** Drops what is older than a minute. */
    void forget(qint64 t);

private:
    QHash<QString, qint64> last;
    QList<qint64> all;
};

/** Another app seen on the local network . `warning`: why it cannot be reached, null if it answers. */
struct Peer {
    /** With no announcements for longer than this, it is no longer considered nearby. */
    static constexpr qint64 SEEN_MS = 20'000;
    QString key, name, email, address;
    int port = 0;
    qint64 seen = 0;
    bool manual = false;
    QString warning;

    bool nearby() const;
    QString who() const { return email.isEmpty() ? name : email; }
    bool isNull() const { return key.isEmpty(); }
    bool operator==(const Peer &o) const = default;
};

/** An incoming delivery : `from` is the contact's name, `fromKey` their public key. */
struct Delivery {
    QString id, from, fromKey;
    qint64 when = 0;
    Parcel content;
    QString label, error;
};

/**
 * Direct exchange between PCs on the same local network (the first Lemuri's wire format and
 * ports, so the two apps talk to each other). Every app announces
 * itself over UDP; deliveries travel over TCP in an envelope that opens only if the sender's key is trusted: a contact,
 * added by pairing with the 6-digit code shown on both screens or by pasting their key card.
 *
 * Networking runs on threads of its own (blocking sockets); the listener is called from them. Thread-safe.
 */
class Lan {
public:
    static constexpr int ANNOUNCE_PORT = 47820, PORT = 47821;
    enum class State { Off, VaultLocked, Active, Error };
    static QString name(State s);

    /** What is needed from the app: keys, vault, arrivals. */
    struct Environment {
        /** Null keys if the vault is locked; throws std::exception if they cannot be read. */
        std::function<Envelope::Keys()> keys;
        /** Null if the vault is locked. */
        std::function<QString(const QString &key)> read;
        std::function<void(const QString &key, const QString &value)> write;
        /** A host arrived from a trusted PC, from a network thread. */
        std::function<void(const Delivery &d)> arrival;
    };
    /** Notices for the interface, from any thread. */
    struct Listener {
        std::function<void(State s, const QString &message)> state;
        std::function<void(const QList<Peer> &peers)> peers;
        std::function<void(const QList<TrustedPeer> &trusted)> trusted;
        /** A PC asks to pair: the name the user gives the contact, or null if refused. Blocks a network thread. */
        std::function<QString(const Peer &p, const QString &code)> request;
        std::function<void(const QString &warning)> firewall;
    };

    /** The ports: the app's fixed ones, or others in tests (LEMURI_LAN_PORTS="tcp,udp", 0 for any free one). */
    explicit Lan(Environment env);
    ~Lan();
    void listen(Listener l);

    State state() const { return current; }
    /** Stops everything and starts again according to preferences and vault. */
    void restart();
    void stop();
    /** Announces itself again and checks the other PCs right away. */
    void refresh();
    bool isOn() const;
    void turnOn(bool yes);
    /**
     * Link requests from other PCs are answered only while the user waits for one (for 5 minutes, or until the window
     * that asked closes): the others are refused without a word on this screen.
     */
    void waitForLink(bool yes);
    bool waitingForLink() const;
    /** Whether 100.64.0.0/10 (Tailscale, carrier-grade NAT) counts as the local network; off by default. */
    bool carrierGrade() const;
    void allowCarrierGrade(bool yes);
    /** The TCP and UDP ports listened on, 0 when stopped. */
    int tcpPort() const;
    int udpPort() const;
    /** True for a numeric address of the local network: only those get an answer. */
    static bool privateAddress(const QString &numeric, bool carrierGrade = false);

    bool trusted(const QString &key) const;
    /** The PC with this key, if it is visible on the network now and trusted: deliveries go there directly. */
    Peer reachable(const QString &key) const;
    QList<TrustedPeer> contacts() const;
    QString trustedName(const QString &key) const;
    QString myKey() const;
    /** Saves a contact; throws Fail if the name is empty, odd or looks like another contact's. */
    void trust(const QString &key, const QString &name);
    void forget(const QString &key);

    /** Hand-typed addresses, called every now and then even if their announcements do not arrive. */
    QStringList addresses() const;
    /** Calls a PC at an address typed by hand and remembers it. Blocking; throws Fail. */
    Peer addAddress(const QString &address);
    void removeAddress(const QString &address);
    /** Asks for pairing: true if the user over there accepted. The code reaches `onCode` first. Blocking; throws Fail. */
    bool pair(const Peer &p, const std::function<void(const QString &code)> &onCode);
    /** Sends a parcel (JSON) to a trusted PC, delivered only with a receipt sealed by its key. Blocking; throws Fail. */
    void send(const Peer &p, const QString &parcel);
    /** Runs `f` on a thread of its own while the local network is on; throws Fail if it is not. */
    void outgoing(std::function<void()> f);
    /**
     * The answer to one line another PC sent from `from` (a hello, a delivery): what serve() does with a connection,
     * without it, for the fuzz tests. Throws on a malformed line, as serve() then drops the connection.
     */
    QJsonObject answerLine(const QByteArray &line, const QString &from);

private:
    struct Run;
    struct Conn;
    std::shared_ptr<Run> active() const;
    QJsonObject identity(const Run &r, const QString &type) const;
    QJsonObject reply(bool ok, const QString &reason) const;
    void set(State s, const QString &message);
    static QString myName();
    void loadTrusted();
    void changeTrusted(const QList<TrustedPeer> &l);
    void saveAddresses(const QStringList &l);
    void publish();

    // Discovery
    void announce(const Run &r);
    void listenUdp(std::shared_ptr<Run> r);
    Peer notice(const QJsonObject &o, const QString &address, bool manual, bool proven = false);
    QList<QPair<QString, int>> unproven(const QString &key) const;
    void proven(const QString &key, const QString &address, int port);
    QList<Peer> peers(qint64 now);
    void tryNearby();
    void tryPeer(const Peer &p);
    void callAddresses();
    Peer hello(const QString &address);

    // Incoming connections
    void listenTcp(std::shared_ptr<Run> r);
    void serve(std::shared_ptr<Run> r, int fd, const QString &from);
    QJsonObject answer(const Run &r, const QJsonObject &m, const QString &from);
    QJsonObject arrival(const Run &r, const QJsonObject &m, const QString &from);
    QJsonObject answerPairing(const Run &r, const QJsonObject &m, const QString &from, Conn &c);
    QString receipt(const Run &r, const QJsonObject &reply, const QString &sender) const;

    void timers(std::shared_ptr<Run> r);

    Environment env;
    Listener listener;
    const int preferredPort, announcePort;
    /** In tests the on/off choice and the typed addresses stay in memory: the user's preferences are not touched. */
    const bool testing;
    std::atomic<bool> onInTest{false}, carrierGradeInTest{false};
    std::atomic<State> current{State::Off};

    /** restart() and stop() one at a time. */
    std::recursive_mutex life;
    /** The lists for the interface are worked out one at a time. */
    std::mutex publishing;
    mutable std::mutex guard;
    std::shared_ptr<Run> run;
    QList<TrustedPeer> trustedList;
    QSet<QString> trustedKeys;
    QHash<QString, Peer> seen;
    /** Trusted key -> "address port" announced by someone who has not proved to hold that key yet. */
    QHash<QString, QStringList> unprovenAddresses;
    QList<Peer> published;
    QString firewallHere, firewallWarning;
    bool firewallSent = false;
    /** Last connection received from another PC: proof that the firewall here lets traffic in. */
    qint64 lastEntry = 0;
    /** id of the received envelopes -> when: a repeat is rejected. */
    QHash<QString, qint64> alreadyArrived;
    /** address -> last pairing request: whoever insists does not make dialogs pop up in a burst. */
    RequestLimit requests;
    /** A failure the network threads let go, to the log once (again only when its message changes). */
    void note(const QString &about, const QString &message);
    std::mutex notedGuard;
    QHash<QString, QString> noted;
    std::atomic<qint64> waitingUntil{0};
    std::atomic<bool> pairingBusy{false};
    /** Every thread but the three of a run: refreshes, connections served, peers tried, outgoing work, pairing. */
    ThreadGroup threads;
};
