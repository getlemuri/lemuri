#include "base/config/Legacy.h"

#include "base/config/Prefs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
struct Move {
    const char *node, *key, *toNode, *toKey;
};
const Move PREFERENCES[] = {
    {"lemuri/ui", "lingua", "lemuri/ui", "language"},
    // The modern theme is gone: its choice, under both names, is dropped.
    {"lemuri/ui", "moderno", nullptr, nullptr},
    {"lemuri/ui", "modernTheme", nullptr, nullptr},
    {"lemuri/ui", "scuro", "lemuri/ui", "darkTheme"},
    {"lemuri/ui", "risorse", "lemuri/ui", "hostColumn"},
    {"lemuri/ui", "cornice-personalizzata", "lemuri/ui", "customFrame"},
    {"lemuri/ui", "layoutVs", nullptr, nullptr},
    {"lemuri/condivisione", "attiva", "lemuri/sharing", "lanOn"},
    {"lemuri/condivisione", "indirizzi", "lemuri/sharing", "lanAddresses"},
    {"lemuri/host", "daDimenticare", "lemuri/host", "passwordsToForget"},
};

/** A node of the preferences under `root`, as a path relative to it ("lemuri/ui"). */
Prefs node(const QString &root, const QString &path) {
    // Prefs works below Prefs::root(); a path climbing out of it and back in reaches any root.
    QString relative = QDir(Prefs::root()).relativeFilePath(root + "/" + path);
    return Prefs(relative);
}

bool exists(const QString &root, const QString &path) {
    return QFileInfo(root + "/" + path).isDir();
}

void copy(const QString &from, const QString &to, const QString &root) {
    QDir f(root + "/" + from);
    Prefs target = node(root, to);
    QHash<QString, QString> mine = target.all(), add;
    const auto theirs = node(root, from).all();
    for (auto it = theirs.begin(); it != theirs.end(); ++it)
        if (!mine.contains(it.key())) add.insert(it.key(), it.value());
    if (!add.isEmpty()) target.change(add);
    for (const QString &child : f.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) copy(from + "/" + child, to + "/" + child, root);
}
}  // namespace

void Legacy::migrate() {
    QString home = QDir::homePath();
    folder(home + "/." + OLD_NAME, home + "/.lemuri");
    tree(Prefs::root(), OLD_NAME, "lemuri");
    preferences(Prefs::root());
    files(home + "/.lemuri");
}

void Legacy::folder(const QString &old, const QString &now) {
    // If it cannot move, the old one stays: the app starts empty and the user can move it by hand.
    if (QFileInfo(old).isDir() && !QFileInfo::exists(now)) QDir().rename(old, now);
}

void Legacy::tree(const QString &prefsRoot, const QString &old, const QString &now) {
    if (!exists(prefsRoot, old)) return;
    copy(old, now, prefsRoot);
    QDir(prefsRoot + "/" + old).removeRecursively();
}

void Legacy::preferences(const QString &prefsRoot) {
    for (const Move &m : PREFERENCES) {
        if (!exists(prefsRoot, m.node)) continue;
        Prefs from = node(prefsRoot, m.node);
        QHash<QString, QString> all = from.all();
        if (!all.contains(m.key)) continue;
        if (m.toNode) {
            Prefs to = node(prefsRoot, m.toNode);
            if (!to.all().contains(m.toKey)) to.put(m.toKey, all.value(m.key));
        }
        from.remove(m.key);
    }
    QDir old(prefsRoot + "/lemuri/condivisione");
    if (old.exists() && node(prefsRoot, "lemuri/condivisione").all().isEmpty() && old.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
        old.removeRecursively();
}

void Legacy::files(const QString &dataDir) {
    QFile::remove(dataDir + "/istanza.porta");
    QFile::remove(dataDir + "/istanza.lock");
    QDir icons(dataDir + "/icone");
    if (icons.exists()) icons.removeRecursively();
}

QJsonValue Legacy::field(const QJsonObject &o, const char *now, const char *old) {
    return o.contains(now) ? o[now] : o[old];
}

QJsonObject Legacy::host(QJsonObject o) {
    static const QList<QPair<QString, QString>> italian{{"nome", "name"}, {"gruppo", "group"}, {"indirizzo", "address"},
                                                       {"porta", "port"}, {"utente", "user"}, {"autenticazione", "authMethod"},
                                                       {"chiave", "key"}, {"sistema", "os"}, {"stato", "state"},
                                                       {"fromColleague", "fromContact"}, {"daCollega", "fromContact"},
                                                       {"ricevuto", "received"}};
    for (const auto &[old, now] : italian) {
        if (!o.contains(old)) continue;
        // A copy first: a reference into the object moves when a key is added to it.
        QJsonValue v = o.value(old);
        if (!o.contains(now)) o.insert(now, v);
        o.remove(old);
    }
    if (o["authMethod"].isString()) o["authMethod"] = authMethod(o["authMethod"].toString());
    return o;
}

QString Legacy::authMethod(const QString &m) {
    return m == "CHIAVE" ? QString("KEY") : m;
}

void Legacy::vaultFile(const QString &dataDir, const QString &now) {
    QString old = dataDir + "/cassaforte.json";
    if (!QFile::exists(now) && QFile::exists(old)) QFile::rename(old, now);
}

bool Legacy::vaultEntries(QHash<QString, QString> &secrets) {
    static const QList<QPair<QString, QString>> renamed{{"condivisione:privata", "sharing:private-key"},
                                                        {"condivisione:pubblica", "sharing:public-key"},
                                                        {"condivisione:buste-decise", "sharing:decided-envelopes"},
                                                        {"lan:fidati", "lan:trusted"}};
    static const QStringList dropped{"firebase:", "sharing:colleague-keys", "sharing:verified", "condivisione:chiavi-colleghi",
                                     "condivisione:verificati"};
    bool changed = false;
    for (auto it = secrets.begin(); it != secrets.end();) {
        bool drop = false;
        for (const QString &p : dropped) drop = drop || it.key().startsWith(p);
        if (drop) {
            it = secrets.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    for (const auto &[old, now] : renamed) {
        if (!secrets.contains(old)) continue;
        QString v = secrets.take(old);
        if (!secrets.contains(now)) secrets.insert(now, v);
        changed = true;
    }
    return changed;
}
