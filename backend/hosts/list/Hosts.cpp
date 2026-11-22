#include "hosts/list/Hosts.h"

#include "base/core/Error.h"
#include "base/system/Files.h"
#include "base/config/I18n.h"
#include "base/config/Legacy.h"
#include "base/core/Params.h"
#include "base/config/Prefs.h"
#include "base/core/Runtime.h"
#include "hosts/list/Folders.h"
#include "hosts/routes/SshConfig.h"
#include "vault/Vault.h"
#include "vault/VaultAccess.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QUuid>
#include <cstdio>

Hosts::Hosts(Events &events, Loop &loop, Activity &activity, Vault &vault) : events(events), loop(loop), activity(activity), vault(vault) {
    load();
    // The passwords of hosts deleted while the vault was locked are erased as soon as it is unlocked.
    vault.onUnlock([this] { this->loop.post([this] { forgetLeftovers(); }); });
}

QString Hosts::file() {
    return Files::data() + "/hosts.json";
}

QString Hosts::uid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

const Host *Hosts::find(const QString &id) const {
    for (const Host &h : hosts)
        if (h.id() == id) return &h;
    return nullptr;
}

const Host &Hosts::host(const QString &id) const {
    const Host *h = find(id);
    if (!h) throw Fail{"No host " + id};
    return *h;
}

// ── The file ──

void Hosts::load() {
    QFile f(file());
    if (!f.exists()) return;
    QJsonParseError error;
    QJsonDocument d = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &error) : QJsonDocument();
    f.close();
    if (d.isNull() || (!d.isArray() && !d.isObject())) {
        // Starting empty and then saving would erase it: it is moved aside under a new name; if that fails, nothing is saved.
        QString why = d.isNull() && error.error != QJsonParseError::NoError ? error.errorString() : f.errorString();
        QString aside = file() + ".corrupt-" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmmss");
        if (QFile::rename(file(), aside)) loadingNotice = I18n::t("hosts.load.kept", why, aside);
        else {
            blocked = true;
            loadingNotice = I18n::t("hosts.load.blocked", why, file());
        }
        std::fprintf(stderr, "Cannot read %s: %s\n", qPrintable(file()), qPrintable(why));
        return;
    }
    // The first versions saved only the list of hosts, and Italian names.
    QJsonArray list = d.isArray() ? d.array() : d.object()["hosts"].toArray();
    QJsonArray folders = d.isObject() ? Legacy::field(d.object(), "folders", "cartelle").toArray() : QJsonArray();
    for (const QJsonValue &v : list) hosts << Host::from(v.toObject());
    for (const QJsonValue &v : folders) {
        QString c = Folders::normalize(v.toString());
        if (!c.isEmpty() && !made.contains(c)) made << c;
    }
    QJsonObject today{{"folders", QJsonArray::fromStringList(made)}};
    QJsonArray saved;
    for (const Host &h : hosts) saved << h.saved();
    today["hosts"] = saved;
    if (!d.isObject() || d.object() != today) store();
}

void Hosts::store() {
    if (blocked) {
        std::fprintf(stderr, "Not saving %s: it could not be read at startup and was not moved aside\n", qPrintable(file()));
        return;
    }
    QJsonArray list;
    for (const Host &h : hosts) list << h.saved();
    QJsonObject d{{"folders", QJsonArray::fromStringList(made)}, {"hosts", list}};
    QString error;
    if (!Files::writePrivate(file(), QJsonDocument(d).toJson(QJsonDocument::Indented), &error))
        std::fprintf(stderr, "Cannot save %s: %s\n", qPrintable(file()), qPrintable(error));
}

// ── The list ──

QStringList Hosts::folders() const {
    QMap<QString, QString> all;  // lower case -> as written, the first one seen
    QStringList base = made;
    for (const Host &h : hosts) base << h.group();
    for (const QString &c : base)
        for (QString p = c; !p.isEmpty(); p = Folders::parent(p))
            if (!all.contains(p.toLower())) all.insert(p.toLower(), p);
    return all.values();
}

bool Hosts::folderExists(const QString &path) const {
    for (const QString &c : folders())
        if (c.compare(path, Qt::CaseInsensitive) == 0) return true;
    return false;
}

QJsonObject Hosts::snapshot() const {
    QJsonArray list;
    for (const Host &h : hosts) list << h.saved();
    return {{"hosts", list}, {"created", QJsonArray::fromStringList(made)}, {"folders", QJsonArray::fromStringList(folders())}};
}

void Hosts::changed() {
    store();
    events.event("hosts", snapshot());
    if (listChanged) listChanged();
}

void Hosts::save(Host h) {
    for (int i = 0; i < hosts.size(); i++) {
        if (hosts[i].id() == h.id()) {
            QString old = hosts[i].name();
            if (!old.isEmpty() && old != h.name()) renameJumps(old, h.name());
            hosts[i] = h;
            changed();
            return;
        }
    }
    hosts << h;
    changed();
}

void Hosts::renameJumps(const QString &old, const QString &fresh) {
    for (Host &x : hosts) {
        if (x.jump().isEmpty()) continue;
        QStringList hops;
        for (const QString &t : x.jump().split(',')) hops << (t.trimmed().compare(old, Qt::CaseInsensitive) == 0 ? fresh : t.trimmed());
        QString j = hops.join(',');
        if (j != QString(x.jump()).remove(' ')) x.set("jump", j);
    }
}

void Hosts::importHosts(QList<Host> l, const QString &source) {
    if (l.isEmpty()) return;
    hosts << l;
    changed();
    activity.done(source, I18n::t("task.hostsImported", QString::number(l.size())));
}

void Hosts::replaceAll(const QStringList &folders, QList<Host> l) {
    hosts = l;
    made.clear();
    for (const QString &c : folders) {
        QString n = Folders::normalize(c);
        if (!n.isEmpty() && !made.contains(n)) made << n;
    }
    changed();
}

void Hosts::refreshStatuses(const QHash<QString, Host::Reach> &states) {
    bool modified = false;
    for (Host &h : hosts) {
        auto it = states.constFind(h.id());
        if (it != states.constEnd() && *it != h.reach()) {
            h.set("state", Host::name(*it));
            modified = true;
        }
    }
    if (modified) events.event("hosts", snapshot());
}

void Hosts::refreshOs(const QString &id, const QString &os) {
    const Host *h = find(id);
    if (!h || os.isEmpty() || os == h->os()) return;
    Host c = *h;
    c.set("os", os);
    save(c);
}

void Hosts::remove(const QString &id) {
    QString name = host(id).name();
    hosts.removeIf([&](const Host &x) { return x.id() == id; });
    if (removed) removed(id);
    changed();
    // With the vault locked the password cannot be removed now: the id is noted (it is not a secret) and the
    // password is removed at the next unlock. A deleted host's password must not stay in the file forever.
    try {
        if (vault.isOpen()) vault.forget(id);
        else toForget(id, true);
    } catch (const Fail &) {
        toForget(id, true);
    }
    activity.done(name, I18n::t("task.hostRemoved"));
}

void Hosts::toForget(const QString &id, bool add) {
    Prefs p("lemuri/host");
    QStringList l;
    for (const QString &x : p.get("passwordsToForget").split(','))
        if (!x.isEmpty() && !l.contains(x)) l << x;
    if (add && !l.contains(id)) l << id;
    if (!add) l.removeAll(id);
    p.put("passwordsToForget", l.join(','));
}

void Hosts::forgetLeftovers() {
    for (const QString &id : Prefs("lemuri/host").get("passwordsToForget").split(',')) {
        if (id.trimmed().isEmpty()) continue;
        if (!find(id)) {
            try {
                vault.forget(id);
            } catch (const Fail &) {
                continue;  // retried at the next unlock
            }
        }
        toForget(id, false);
    }
}

void Hosts::moveFolder(const QString &old, const QString &to) {
    QString n = Folders::normalize(to);
    if (n.isEmpty() || n == old || Folders::inside(n, old)) return;
    QStringList c;
    for (const QString &x : made) {
        QString r = Folders::rename(x, old, n);
        if (!c.contains(r)) c << r;
    }
    if (!c.contains(n)) c << n;
    made = c;
    for (Host &h : hosts) {
        QString g = Folders::rename(h.group(), old, n);
        if (g != h.group()) h.set("group", g);
    }
    changed();
    bool rename = Folders::parent(old) == Folders::parent(n);
    activity.done(old, rename ? I18n::t("task.renamedTo", Folders::name(n)) : I18n::t("task.movedTo", Folders::label(Folders::parent(n))));
}

void Hosts::deleteFolder(const QString &path) {
    QString on = Folders::parent(path);
    made.removeIf([&](const QString &c) { return Folders::inside(c, path); });
    if (!on.isEmpty() && !made.contains(on)) made << on;
    for (Host &h : hosts)
        if (Folders::inside(h.group(), path)) h.set("group", on);
    changed();
    activity.done(path, I18n::t("task.folderDeleted"));
}

// ── Requests ──

void Hosts::ops(Router &router) {
    router.op("hosts", [this](const QJsonObject &) { return snapshot(); });
    router.op("host.save", [this](const QJsonObject &m) {
        Host h = Host::from(m["host"].toObject());
        if (h.name().trimmed().isEmpty() || h.address().trimmed().isEmpty()) throw Fail{"A host needs a name and an address"};
        bool fresh = h.id().isEmpty() || !find(h.id());
        if (h.id().isEmpty()) h.set("id", uid());
        save(h);
        activity.done(h.name(), I18n::t(fresh ? "task.hostAdded" : "task.hostChanged"));
        return h.saved();
    });
    router.op("host.remove", [this](const QJsonObject &m) {
        remove(host(Params::need(m, "host")).id());
        return QJsonValue();
    });
    router.op("host.move", [this](const QJsonObject &m) { return move(m); });
    router.op("folder.create", [this](const QJsonObject &m) {
        QString p = Folders::normalize(Params::str(m, "path"));
        if (p.isEmpty() || folderExists(p)) return QJsonValue();
        made << p;
        changed();
        activity.done(p, I18n::t("task.folderCreated"));
        return QJsonValue();
    });
    router.op("folder.move", [this](const QJsonObject &m) {
        moveFolder(Params::need(m, "from"), Params::need(m, "to"));
        return QJsonValue();
    });
    router.op("folder.delete", [this](const QJsonObject &m) {
        deleteFolder(Params::need(m, "path"));
        return QJsonValue();
    });
    // The hosts of ~/.ssh/config, each with why it starts unticked (or how it is reached).
    router.op("sshconfig.read", [this](const QJsonObject &) { return readSshConfig(); });
    router.op("sshconfig.import", [this](const QJsonObject &m) { return importSshConfig(m); });
}

/** Hosts dragged into a folder. */
QJsonValue Hosts::move(const QJsonObject &m) {
    Host c = host(Params::need(m, "host"));
    QString group = Folders::normalize(Params::str(m, "folder"));
    // Put in a folder (or in the datacenter), a received host becomes one of your own.
    if (group == c.group() && !c.received()) return QJsonValue();
    c.set("group", group);
    c.set("received", false);
    save(c);
    activity.done(c.name(), I18n::t("task.movedTo", Folders::label(group)));
    return QJsonValue();
}

/** The hosts of ~/.ssh/config that can be imported, and which are here already. */
QJsonObject Hosts::readSshConfig() const {
    QJsonArray rows;
    for (const SshConfig::Entry &e : SshConfig::read()) {
        Host h = e.toHost("");
        bool already = false, sameName = false;
        for (const Host &x : hosts) {
            already = already || (x.address().compare(h.address(), Qt::CaseInsensitive) == 0 && x.port() == h.port() && x.user() == h.user());
            sameName = sameName || x.name().compare(e.alias, Qt::CaseInsensitive) == 0;
        }
        QString unchecked = already ? I18n::t("sshImport.already") : sameName ? I18n::t("sshImport.sameName")
                            : e.gitService() ? I18n::t("sshImport.git") : QString();
        QString why = !unchecked.isNull() ? unchecked : !e.proxyJump.isEmpty() ? I18n::t("sshImport.proxyJump", e.proxyJump) : QString();
        auto orNull = [](const QString &s) { return s.isEmpty() ? QJsonValue() : QJsonValue(s); };
        rows << QJsonObject{{"alias", e.alias}, {"hostName", e.hostName}, {"user", orNull(e.user)}, {"port", e.port},
                            {"identityFile", orNull(e.identityFile)}, {"proxyJump", orNull(e.proxyJump)},
                            {"destination", e.destination()}, {"chosen", unchecked.isNull()}, {"why", orNull(why)}};
    }
    return QJsonObject{{"file", SshConfig::file()}, {"entries", rows}};
}

/** The chosen hosts of ~/.ssh/config added, as one import. */
QJsonValue Hosts::importSshConfig(const QJsonObject &m) {
    QStringList aliases = Params::strings(m, "aliases");
    QString folder = Folders::normalize(Params::str(m, "folder"));
    QList<SshConfig::Entry> found = SshConfig::read();
    QList<Host> l;
    for (const SshConfig::Entry &e : found)
        if (aliases.contains(e.alias)) l << e.toHost(folder);
    // A ProxyJump naming another alias: by name if that host is (or will be) in the list, else user@host:port.
    QSet<QString> names;
    for (const Host &h : hosts) names << h.name().toLower();
    for (const Host &h : l) names << h.name().toLower();
    QHash<QString, SshConfig::Entry> byAlias;
    for (const SshConfig::Entry &e : found) byAlias.insert(e.alias.toLower(), e);
    for (Host &h : l) {
        if (h.jump().isEmpty()) continue;
        QStringList hops;
        for (const QString &raw : h.jump().split(',')) {
            QString t = raw.trimmed();
            hops << (names.contains(t.toLower()) || !byAlias.contains(t.toLower()) ? t : byAlias.value(t.toLower()).destination());
        }
        h.set("jump", hops.join(','));
    }
    importHosts(l, "~/.ssh/config");
    return QJsonValue();
}
