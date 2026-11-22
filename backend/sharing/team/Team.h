#pragma once

#include "sharing/lan/Lan.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

class Events;
class Loop;
class Prompter;
class Activity;
class VaultAccess;
class Hosts;
class Reply;
class Router;

/**
 * Sharing hosts with contacts, by text envelope or over the local network: keys in the
 * vault, sending, opening pasted envelopes, replays refused, incoming deliveries. News
 * goes out as events (sharing, deliveries, arrival, lan, peers, contacts, firewall); a PC asking to pair is an ask of
 * kind "pair". Passwords never leave the backend: a delivery says only whether it carries one.
 *
 * The lists the frontend sees live on the main thread; the network and the key work run on the "sharing" thread.
 */
class Team {
public:
    Team(Events &events, Loop &loop, Prompter &prompter, Activity &activity, VaultAccess &vaultAccess, Hosts &hosts);
    /** Registers its requests. */
    void ops(Router &router);
    ~Team();
    QList<TrustedPeer> contacts() const { return contactList; }
    /** Restarts sharing (keys, local network): when the vault opens or closes, or after a backup was imported. */
    void restart();

private:
    enum class State { VaultLocked, Ready };
    /** As the "sharing" event says it. */
    static QString name(State s);
    void listen();
    /** On the sharing thread. */
    void restartNow();
    Envelope::Keys myKeys();
    void setState(State s, const QString &message);
    /** One after the other on the sharing thread: it waits for the other PCs. */
    void serial(std::function<void()> f);

    // Incoming deliveries (any thread)
    bool inboxAdd(const Delivery &d);
    void inboxRemove(const QString &id);
    void inboxClear();
    void inboxPublish();
    /** The i18n key of the reason to reject the parcel, or null if it can be offered. */
    QString decidedCheck(const Parcel &p) const;
    /** Accepted or rejected: the id is remembered. False if it could not be saved. */
    bool decidedRemember(const Parcel &p);

    /** Everything the Team page shows at once. */
    QJsonObject overview() const;
    /** A host to contacts: over the local network to those reachable, as text for the others; on the sharing thread. */
    void send(const QJsonObject &m, const Reply &r);
    /** A contact from their key card, under the name the user gave. */
    QJsonValue addContact(const QJsonObject &m);
    /** Whether a name can be given to a contact, and why not. */
    QJsonObject checkName(const QJsonObject &m) const;
    /** Pairing asked from here: the code goes out as a "pairCode" event, the answer is whether it succeeded. */
    void pair(const QJsonObject &m, const Reply &r);
    Delivery open(const QString &text);
    QJsonValue accept(const QJsonObject &m);
    const Delivery &delivery(const QJsonObject &m) const;
    const Host *twin(const Host &h) const;
    QJsonObject json(const Delivery &d) const;
    QJsonArray deliveriesJson() const;
    QJsonArray contactsJson() const;
    QJsonArray peersJson() const;
    QJsonObject peerJson(const Peer &p) const;

    Events &events;
    Loop &loop;
    Prompter &prompter;
    Activity &activity;
    VaultAccess &vaultAccess;
    Hosts &hosts;
    Lan lan;

    // Main thread
    QList<Delivery> deliveries;
    QList<Peer> peerList;
    QList<TrustedPeer> contactList;
    State state = State::VaultLocked;
    Lan::State lanState = Lan::State::Off;
    QString message, lanMessage, firewall;

    // Any thread
    mutable std::mutex guard;
    Envelope::Keys keys;
    QList<Delivery> inbox;
    std::mutex making;

    std::mutex queueLock;
    std::condition_variable queueAlarm;
    std::deque<std::function<void()>> queue;
    bool ending = false;
    std::thread worker;
};
