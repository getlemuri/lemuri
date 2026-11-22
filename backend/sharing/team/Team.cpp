#include "sharing/team/Team.h"

#include "base/core/Error.h"
#include "base/system/Files.h"
#include "base/config/I18n.h"
#include "base/core/Params.h"
#include "base/core/Runtime.h"
#include "hosts/list/Folders.h"
#include "hosts/list/Hosts.h"
#include "vault/Vault.h"
#include "vault/VaultAccess.h"

#include <QDateTime>
#include <QJsonDocument>

namespace {
const QString PRIVATE = QStringLiteral("sharing:private-key"), PUBLIC = QStringLiteral("sharing:public-key");
/** inner id -> when it was sent. */
const QString DECIDED = QStringLiteral("sharing:decided-envelopes");
constexpr qint64 DAY_MS = 24LL * 60 * 60 * 1000;

QJsonValue orNull(const QString &s) {
    return s.isNull() ? QJsonValue() : QJsonValue(s);
}

QString owner() {
    QString u = Files::localUser();
    return u.isEmpty() ? QString("user") : u;
}
}  // namespace

Team::Team(Events &events, Loop &loop, Prompter &prompter, Activity &activity, VaultAccess &vaultAccess, Hosts &hosts) : events(events), loop(loop), prompter(prompter), activity(activity), vaultAccess(vaultAccess), hosts(hosts),
      lan(Lan::Environment{
          [this] { return this->vaultAccess.vault().isOpen() ? myKeys() : Envelope::Keys(); },
          [this](const QString &key) { return this->vaultAccess.vault().isOpen() ? this->vaultAccess.vault().password(key) : QString(); },
          [this](const QString &key, const QString &value) {
              if (!this->vaultAccess.vault().isOpen()) throw Fail{I18n::t("sharing.msg.vault")};
              this->vaultAccess.vault().remember(key, value);
          },
          [this](const Delivery &d) {
              // Already accepted or rejected once: the local network keeps its ids only in memory.
              QString why = decidedCheck(d.content);
              if (!why.isNull()) {
                  std::fprintf(stderr, "Local network delivery refused: %s\n", qPrintable(I18n::format(why, {d.from})));
                  return;
              }
              if (!inboxAdd(d)) return;
              this->activity.done(d.from, I18n::t("sharing.receivedLan", d.content.name + " (" + d.content.address + ")"));
              this->loop.post([this, d] { this->events.event("arrival", {{"delivery", json(d)}}); });
          }}) {
    worker = std::thread([this] {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> l(queueLock);
                queueAlarm.wait(l, [this] { return ending || !queue.empty(); });
                if (ending) return;
                f = std::move(queue.front());
                queue.pop_front();
            }
            try {
                f();
            } catch (const std::exception &e) {
                std::fprintf(stderr, "Sharing: %s\n", qPrintable(errorText(e)));
            }
        }
    });
    listen();
    // Sharing restarts by itself when the vault opens or closes: it needs its keys.
    vaultAccess.vault().onUnlock([this] { restart(); });
    // Vault locked: the local network stops and forgets the private key, the incoming deliveries are dropped.
    vaultAccess.vault().onLock([this] { restart(); });
    loop.onStop([this] {
        {
            std::lock_guard<std::mutex> l(queueLock);
            ending = true;
        }
        queueAlarm.notify_all();
        lan.stop();
    });
}

Team::~Team() {
    {
        std::lock_guard<std::mutex> l(queueLock);
        ending = true;
    }
    queueAlarm.notify_all();
    if (worker.joinable()) worker.join();
}

void Team::serial(std::function<void()> f) {
    {
        std::lock_guard<std::mutex> l(queueLock);
        if (ending) return;
        queue.push_back(std::move(f));
    }
    queueAlarm.notify_one();
}

void Team::restart() {
    serial([this] { restartNow(); });
}

void Team::restartNow() {
    lan.restart();
    if (!vaultAccess.vault().isOpen()) {
        {
            std::lock_guard<std::mutex> l(guard);
            keys = {};
        }
        inboxClear();
        setState(State::VaultLocked, I18n::t("sharing.msg.vault"));
        return;
    }
    try {
        Envelope::Keys k = myKeys();
        {
            std::lock_guard<std::mutex> l(guard);
            keys = k;
        }
        setState(State::Ready, I18n::t("sharing.msg.ready"));
    } catch (const std::exception &e) {
        std::lock_guard<std::mutex> l(guard);
        keys = {};
        setState(State::VaultLocked, I18n::t("sharing.msg.keys", errorText(e)));
    }
}

/** The keys are in the vault; the first time they are created (only once even if two threads ask together). */
Envelope::Keys Team::myKeys() {
    std::lock_guard<std::mutex> l(making);
    QString priv = vaultAccess.vault().password(PRIVATE), pub = vaultAccess.vault().password(PUBLIC);
    if (!priv.isNull() && !pub.isNull()) return {pub, priv};
    Envelope::Keys k = Envelope::newKeys();
    vaultAccess.vault().remember(PRIVATE, k.privateKey);
    vaultAccess.vault().remember(PUBLIC, k.publicKey);
    return k;
}

QString Team::name(State s) {
    return s == State::Ready ? "READY" : "VAULT_LOCKED";
}

void Team::setState(State s, const QString &m) {
    loop.post([this, s, m] {
        if (s != state) std::fprintf(stderr, "Sharing: %s\n", qPrintable(m));
        state = s;
        message = m;
        events.event("sharing", {{"state", name(s)}, {"message", m}});
    });
}

void Team::listen() {
    Lan::Listener l;
    l.state = [this](Lan::State s, const QString &m) {
        loop.post([this, s, m] {
            lanState = s;
            lanMessage = m;
            events.event("lan", {{"state", Lan::name(s)}, {"message", m}, {"on", lan.isOn()}});
        });
    };
    l.peers = [this](const QList<Peer> &p) {
        loop.post([this, p] {
            peerList = p;
            events.event("peers", {{"peers", peersJson()}});
            // "Nearby" of the contacts changes with the PCs seen on the network.
            events.event("contacts", {{"contacts", contactsJson()}});
        });
    };
    l.trusted = [this](const QList<TrustedPeer> &t) {
        loop.post([this, t] {
            contactList = t;
            events.event("contacts", {{"contacts", contactsJson()}});
            events.event("peers", {{"peers", peersJson()}});
        });
    };
    l.request = [this](const Peer &p, const QString &code) {
        // From a network thread, which may wait: the other PC waits for an answer.
        QJsonValue a = prompter.ask("pair", {{"peer", peerJson(p)}, {"code", code}});
        return a.isString() ? a.toString() : QString();
    };
    l.firewall = [this](const QString &w) {
        loop.post([this, w] {
            firewall = w;
            events.event("firewall", {{"warning", w}});
        });
    };
    lan.listen(l);
}

// ── Incoming deliveries ──

bool Team::inboxAdd(const Delivery &d) {
    {
        std::lock_guard<std::mutex> l(guard);
        for (const Delivery &x : inbox)
            if (x.id == d.id) return false;
        inbox.append(d);
    }
    inboxPublish();
    return true;
}

void Team::inboxRemove(const QString &id) {
    bool removed;
    {
        std::lock_guard<std::mutex> l(guard);
        removed = inbox.removeIf([&](const Delivery &d) { return d.id == id; }) > 0;
    }
    if (removed) inboxPublish();
}

/** With the vault locked: the deliveries hold decrypted passwords. */
void Team::inboxClear() {
    {
        std::lock_guard<std::mutex> l(guard);
        inbox.clear();
    }
    inboxPublish();
}

void Team::inboxPublish() {
    QList<Delivery> l;
    {
        std::lock_guard<std::mutex> g(guard);
        l = inbox;
    }
    std::stable_sort(l.begin(), l.end(), [](const Delivery &x, const Delivery &y) { return x.when > y.when; });
    loop.post([this, l] {
        deliveries = l;
        events.event("deliveries", {{"deliveries", deliveriesJson()}});
    });
}

QString Team::decidedCheck(const Parcel &p) const {
    QString j = vaultAccess.vault().password(DECIDED);
    return DecidedEnvelopes::check(p, j.isNull() ? QJsonObject() : QJsonDocument::fromJson(j.toUtf8()).object(),
                                   QDateTime::currentMSecsSinceEpoch());
}

bool Team::decidedRemember(const Parcel &p) {
    if (p.id.isNull()) return true;
    if (!vaultAccess.vault().isOpen()) return false;
    try {
        QString j = vaultAccess.vault().password(DECIDED);
        QJsonObject m = j.isNull() ? QJsonObject() : QJsonDocument::fromJson(j.toUtf8()).object();
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        // Remembered until the envelope would be too old anyway.
        for (auto it = m.begin(); it != m.end();)
            it = now - qint64(it.value().toDouble()) > DecidedEnvelopes::MAX_AGE_MS + DAY_MS ? m.erase(it) : ++it;
        m[p.id] = p.when;
        vaultAccess.vault().remember(DECIDED, QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact)));
        return true;
    } catch (const Fail &f) {
        // not writable, or the vault locked in the meantime
        std::fprintf(stderr, "Cannot remember a handled envelope: %s\n", qPrintable(f.why));
        return false;
    }
}

Delivery Team::open(const QString &text) {
    Envelope::Keys k;
    {
        std::lock_guard<std::mutex> l(guard);
        k = keys;
    }
    if (k.isNull()) throw Fail{I18n::t("sharing.msg.vault")};
    TextEnvelope::Posted e = TextEnvelope::unarmor(text);
    if (e.isNull()) throw Fail{I18n::t("paste.notAnEnvelope")};
    if (e.to != k.publicKey) throw Fail{I18n::t("paste.notForYou")};
    QString who = lan.trustedName(e.from);
    // Only from a contact: a stranger's envelope would open, but nobody could say who sent it.
    if (who.isNull()) throw Fail{I18n::t("paste.unknownSender", Envelope::fingerprint(e.from))};
    bool ok = false;
    QString inside = Envelope::openFrom(e.sealed, k, e.from, e.from, e.to, &ok);
    Parcel p;
    if (ok) p = Parcel::fromJson(inside, &ok);
    if (!ok) throw Fail{I18n::t("sharing.envelope.cannotOpen", who)};
    QString problem = p.admit();
    if (!problem.isNull()) throw Fail{I18n::t("sharing.envelope.invalid", who, problem)};
    QString why = decidedCheck(p);
    if (!why.isNull()) throw Fail{I18n::format(why, {who})};
    Delivery d{"text:" + p.id, who, e.from, p.when, p, p.name, QString()};
    if (!inboxAdd(d)) throw Fail{I18n::t("paste.already")};
    activity.done(who, I18n::t("sharing.receivedText", p.name + " (" + p.address + ")"));
    return d;
}

// ── Requests ──

void Team::ops(Router &router) {
    router.op("team.state", [this](const QJsonObject &) { return overview(); });
    // At startup, once the frontend listens: the first state and lists arrive as events.
    router.op("team.start", [this](const QJsonObject &) {
        restart();
        return QJsonValue();
    });
    router.op("team.restart", [this](const QJsonObject &) {
        restart();
        return QJsonValue();
    });
    // A host to contacts: delivered directly to those nearby, as text envelopes for the others.
    router.later("team.send", [this](const QJsonObject &m, const Reply &r) { send(m, r); });
    router.op("team.open", [this](const QJsonObject &m) { return json(open(Params::need(m, "text"))); });
    router.op("team.reject", [this](const QJsonObject &m) {
        Delivery d = delivery(m);
        inboxRemove(d.id);
        // Accepted or rejected: the same envelope must not be able to come back, by text or over the network.
        return QJsonValue(decidedRemember(d.content));
    });
    router.op("team.accept", [this](const QJsonObject &m) { return accept(m); });
    router.op("team.addContact", [this](const QJsonObject &m) { return addContact(m); });

    router.op("lan.on", [this](const QJsonObject &m) {
        bool on = Params::flag(m, "on");
        serial([this, on] { lan.turnOn(on); });
        return QJsonValue();
    });
    // Waiting for a link from another PC (Team → Wait for a link), or no longer: requests are refused otherwise.
    router.op("lan.waitForLink", [this](const QJsonObject &m) {
        lan.waitForLink(Params::flag(m, "on"));
        return QJsonValue(lan.waitingForLink());
    });
    // Whether 100.64/10 (Tailscale, carrier-grade NAT) is local: the setting, changed if "on" is given.
    router.op("lan.carrierGrade", [this](const QJsonObject &m) {
        if (m.contains("on")) {
            bool on = Params::flag(m, "on");
            serial([this, on] { lan.allowCarrierGrade(on); });
            return QJsonValue(on);
        }
        return QJsonValue(lan.carrierGrade());
    });
    router.op("lan.refresh", [this](const QJsonObject &) {
        serial([this] { lan.refresh(); });
        return QJsonValue();
    });
    router.op("lan.restart", [this](const QJsonObject &) {
        serial([this] { lan.restart(); });
        return QJsonValue();
    });
    router.op("lan.trust", [this](const QJsonObject &m) {
        lan.trust(Params::need(m, "key"), Params::need(m, "name"));
        activity.done(Params::need(m, "name"), I18n::t("task.linked"));
        return QJsonValue();
    });
    router.op("lan.forget", [this](const QJsonObject &m) {
        lan.forget(Params::need(m, "key"));
        return QJsonValue();
    });
    // The name a contact would get: cleaned as it will be saved, and why it cannot be used, or null.
    router.op("lan.nameCheck", [this](const QJsonObject &m) { return checkName(m); });
    // A key card pasted: its fingerprint and the name it suggests, or null if it is not one.
    router.op("team.parseKey", [](const QJsonObject &m) -> QJsonValue {
        TextEnvelope::KeyCard k = TextEnvelope::parseKey(Params::str(m, "card"));
        if (k.isNull()) return QJsonValue();
        return QJsonObject{{"name", k.name}, {"fingerprint", Envelope::fingerprint(k.key)}};
    });
    router.later("lan.addAddress", [this](const QJsonObject &m, const Reply &r) {
        QString address = Params::need(m, "address");
        lan.outgoing([this, address, r] {
            try {
                Peer p = lan.addAddress(address);
                loop.post([this, p, r] { r.ok(peerJson(p)); });
            } catch (const Fail &f) {
                r.fail(f.why);
            }
        });
    });
    router.op("lan.removeAddress", [this](const QJsonObject &m) {
        lan.removeAddress(Params::need(m, "address"));
        return QJsonValue();
    });
    // Pairing asked from here: the code goes out as a "pairCode" event, the answer is whether it succeeded.
    router.later("lan.pair", [this](const QJsonObject &m, const Reply &r) { pair(m, r); });
}

/** Everything the Team page shows at once. */
QJsonObject Team::overview() const {
    Envelope::Keys k;
    {
        std::lock_guard<std::mutex> l(guard);
        k = keys;
    }
    return QJsonObject{{"state", name(state)},
                       {"message", message},
                       {"lanState", Lan::name(lanState)},
                       {"lanMessage", lanMessage},
                       {"lanOn", lan.isOn()},
                       {"lanCarrierGrade", lan.carrierGrade()},
                       {"firewall", firewall},
                       {"fingerprint", k.isNull() ? QJsonValue() : QJsonValue(Envelope::fingerprint(k.publicKey))},
                       {"keyCard", k.isNull() ? QJsonValue() : QJsonValue(TextEnvelope::keyCard(k.publicKey, owner()))},
                       {"contacts", contactsJson()},
                       {"peers", peersJson()},
                       {"deliveries", deliveriesJson()}};
}

/** A host to contacts: over the local network to those reachable, as text for the others; on the sharing thread. */
void Team::send(const QJsonObject &m, const Reply &r) {
    Host h = hosts.host(Params::need(m, "host"));
    QStringList wanted = Params::strings(m, "to");
    QList<TrustedPeer> to;
    QStringList names;
    for (const TrustedPeer &c : contactList)
        if (wanted.contains(c.key)) {
            to << c;
            names << c.name;
        }
    QString password = Params::flag(m, "withPassword") ? vaultAccess.vault().password(h.id()) : QString();
    QString note = Params::str(m, "message");
    int task = activity.task(h.name(), I18n::t("task.sendingTo", names.join(", ")));
    serial([this, h, to, password, note, r, task] {
        try {
            Envelope::Keys k;
            {
                std::lock_guard<std::mutex> l(guard);
                k = keys;
            }
            if (k.isNull()) throw Fail{I18n::t("sharing.msg.vault")};
            QJsonArray out;
            for (const TrustedPeer &c : to) {
                if (!lan.trusted(c.key)) throw Fail{I18n::t("lan.notLinkedTo", c.name)};
                // A new id and date for each recipient: each envelope is decided on its own.
                QString parcel = Parcel::from(h, password, note).toJson();
                QJsonObject contact{{"key", c.key}, {"name", c.name}};
                // Their PC on the local network: direct. If that fails, as text.
                Peer nearby = lan.reachable(c.key);
                if (!nearby.isNull()) {
                    try {
                        lan.send(nearby, parcel);
                        out.append(QJsonObject{{"to", contact}, {"text", QJsonValue()}});
                        continue;
                    } catch (const std::exception &e) {
                        std::fprintf(stderr, "Local network delivery failed, falling back to text: %s\n", qPrintable(errorText(e)));
                    }
                }
                Envelope::Sealed s = Envelope::sealFrom(parcel, k, c.key, k.publicKey, c.key);
                out.append(QJsonObject{{"to", contact}, {"text", TextEnvelope::armor(k.publicKey, c.key, s)}});
            }
            activity.finish(task, Activity::Outcome::Ok);
            r.ok(out);
        } catch (const std::exception &e) {
            activity.finish(task, Activity::Outcome::Error, errorText(e));
            r.fail(errorText(e));
        }
    });
}

/** A contact from their key card, under the name the user gave. */
QJsonValue Team::addContact(const QJsonObject &m) {
    QString name = Params::need(m, "name");
    TextEnvelope::KeyCard c = TextEnvelope::parseKey(Params::need(m, "keyCard"));
    if (c.isNull()) throw Fail{I18n::t("contact.notAKey")};
    {
        std::lock_guard<std::mutex> l(guard);
        if (c.key == keys.publicKey) throw Fail{I18n::t("contact.yours")};
    }
    QString other = lan.trustedName(c.key);
    if (!other.isNull()) throw Fail{I18n::t("contact.already", other)};
    // The name is checked (empty, or looking like another contact's) where every contact is saved.
    lan.trust(c.key, name);
    activity.done(name, I18n::t("task.contactAdded"));
    return QJsonValue();
}

/** Whether a name can be given to a contact, and why not. */
QJsonObject Team::checkName(const QJsonObject &m) const {
    QString n = ContactNames::clean(Params::str(m, "name"));
    QString wrong = ContactNames::problem(n);
    QString other = wrong.isNull() ? ContactNames::takenBy(contactList, n, Params::str(m, "key")) : QString();
    QJsonValue problem;
    if (!wrong.isNull()) {
        problem = I18n::format(wrong, {});
    } else if (!other.isNull()) {
        QString otherKey;
        for (const TrustedPeer &t : contactList)
            if (t.name == other) {
                otherKey = t.key;
                break;
            }
        problem = I18n::t("link.nameTaken", other, otherKey.isNull() ? QString("?") : Envelope::fingerprint(otherKey));
    }
    return QJsonObject{{"clean", n}, {"problem", problem}};
}

/** Pairing asked from here: the code goes out as a "pairCode" event, the answer is whether it succeeded. */
void Team::pair(const QJsonObject &m, const Reply &r) {
    QString key = Params::need(m, "key");
    Peer p;
    for (const Peer &x : peerList)
        if (x.key == key) p = x;
    if (p.isNull()) throw Fail{"No peer " + key};
    lan.outgoing([this, p, key, r] {
        try {
            r.ok(lan.pair(p, [this, key](const QString &code) { events.event("pairCode", {{"key", key}, {"code", code}}); }));
        } catch (const std::exception &e) {
            r.fail(errorText(e));
        }
    });
}

/**
 * A delivery accepted: `mode` "apart" (in the received section), "folder" (into `folder`) or "update" (the
 * password goes to the host already in the list with the same login).
 */
QJsonValue Team::accept(const QJsonObject &m) {
    Delivery d = delivery(m);
    const Parcel &p = d.content;
    if (!p.password.isNull()) vaultAccess.requireOpen();
    QString mode = Params::need(m, "mode");
    QString savedId, savedName;
    if (mode == "update") {
        const Host *t = twin(p.asHost());
        if (!t || p.password.isNull()) throw Fail{"Nothing to update"};
        savedId = t->id();
        savedName = t->name();
    } else {
        Host h = p.asHost();
        h.set("id", Hosts::uid());
        h.set("fromContact", d.from);
        bool apart = mode == "apart";
        h.set("received", apart);
        h.set("group", apart ? QString("") : Folders::normalize(Params::str(m, "folder")));
        for (const Host &x : hosts.all())
            if (x.name().compare(h.name(), Qt::CaseInsensitive) == 0) {
                h.set("name", h.name() + "-" + d.from.split('@').first());
                break;
            }
        hosts.save(h);
        savedId = h.id();
        savedName = h.name();
    }
    if (!p.password.isNull()) vaultAccess.vault().remember(savedId, p.password);
    activity.done(savedName, I18n::t("task.acceptedFrom", d.from));
    inboxRemove(d.id);
    return QJsonObject{{"host", savedId}, {"remembered", decidedRemember(p)}};
}

/** The host with the same address, port and user: exactly the same address, never a look-alike. */
const Host *Team::twin(const Host &h) const {
    QString address = h.address().toLower();
    for (const Host &x : hosts.all())
        if (x.address().toLower() == address && x.port() == h.port() && x.user() == h.user()) return &x;
    return nullptr;
}

const Delivery &Team::delivery(const QJsonObject &m) const {
    QString id = Params::need(m, "delivery");
    for (const Delivery &d : deliveries)
        if (d.id == id) return d;
    throw Fail{"No delivery " + id};
}

QJsonArray Team::deliveriesJson() const {
    QJsonArray a;
    for (const Delivery &d : deliveries) a.append(json(d));
    return a;
}

/** A delivery without its password: only whether there is one, and the host it would update. */
QJsonObject Team::json(const Delivery &d) const {
    const Parcel &p = d.content;
    const Host *t = twin(p.asHost());
    QJsonObject c{{"name", orNull(p.name)},
                  {"address", orNull(p.address)},
                  {"user", orNull(p.user)},
                  {"port", p.port},
                  {"os", orNull(p.os)},
                  {"message", orNull(p.message)},
                  {"authMethod", orNull(p.authMethod)},
                  {"hasPassword", !p.password.isNull()},
                  {"twin", t ? QJsonValue(t->id()) : QJsonValue()}};
    return {{"id", d.id},
            {"from", d.from},
            {"fingerprint", d.fromKey.isNull() ? QString("?") : Envelope::fingerprint(d.fromKey)},
            {"when", d.when},
            {"label", orNull(d.label)},
            {"error", orNull(d.error)},
            {"content", c}};
}

/** The contacts, each with its fingerprint and whether its PC is reachable on the local network. */
QJsonArray Team::contactsJson() const {
    QJsonArray a;
    for (const TrustedPeer &t : contactList)
        a.append(QJsonObject{{"key", t.key}, {"name", t.name}, {"fingerprint", Envelope::fingerprint(t.key)}, {"nearby", !lan.reachable(t.key).isNull()}});
    return a;
}

QJsonArray Team::peersJson() const {
    QJsonArray a;
    for (const Peer &p : peerList) a.append(peerJson(p));
    return a;
}

QJsonObject Team::peerJson(const Peer &p) const {
    return {{"key", p.key},
            {"name", orNull(p.name)},
            {"email", orNull(p.email.isEmpty() ? QString() : p.email)},
            {"address", p.address},
            {"port", p.port},
            {"seen", p.seen},
            {"manual", p.manual},
            {"warning", orNull(p.warning.isEmpty() ? QString() : p.warning)},
            {"fingerprint", Envelope::fingerprint(p.key)},
            {"nearby", p.nearby()},
            {"who", p.who()},
            {"trusted", lan.trusted(p.key)},
            {"trustedName", orNull(lan.trustedName(p.key))},
            {"suggested", ContactNames::clean(p.who())}};
}
