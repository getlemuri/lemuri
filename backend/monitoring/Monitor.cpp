#include "monitoring/Monitor.h"

#include "base/Error.h"
#include "base/Params.h"
#include "base/Prefs.h"
#include "base/Runtime.h"
#include "hosts/Hosts.h"
#include "hosts/Jumps.h"
#include "vault/Vault.h"
#include "vault/VaultAccess.h"

#include <QDateTime>
#include <QJsonArray>
#include <QTimer>

namespace {
/** A good reading is taken again after this. */
constexpr qint64 READ_EVERY_MS = 5 * 60'000;
/** After an error, much later: many failed logins trigger fail2ban. */
constexpr qint64 RETRY_AFTER_ERROR_MS = 30 * 60'000;
}  // namespace

Monitor::Monitor(Events &events, Loop &loop, Hosts &hosts, Vault &vault) : events(events), loop(loop), hosts(hosts), vault(vault), metrics(&vault) {
    hosts.listChanged = [this] {
        if (!launched) return;
        probe.list(targets());
        readDue();
    };
    hosts.removed = [this](const QString &id) {
        metrics.forget(id);
        resources.remove(id);
    };
    loop.onStop([this] {
        probe.stop();
        metrics.stop();
    });
}

int Monitor::probeMinutes() {
    return minutes(Prefs("lemuri/ui").getInt("probeMinutes", Probe::DEFAULT_MINUTES));
}

int Monitor::minutes(int n) {
    // Every minute was a choice once: it becomes the shortest there is now.
    return n <= 0 ? 0 : std::max(Probe::MIN_MINUTES, n);
}

QList<Probe::Target> Monitor::targets() const {
    QList<Probe::Target> l;
    for (const Host &h : hosts.all()) {
        // Behind a jump host only the first hop can be reached from here: if it answers, the way in is open.
        Jumps::Route r = Jumps::resolve(h, hosts.all());
        const Host &first = r.hops.isEmpty() ? h : r.hops.first();
        l.append({h.id(), first.address(), first.port()});
    }
    return l;
}

void Monitor::ops(Router &router) {
    router.op("monitor.start", [this](const QJsonObject &) {
        launch();
        return QJsonValue();
    });
    router.op("monitor.probeMinutes", [this](const QJsonObject &m) {
        if (m.contains("minutes")) {
            int n = minutes(Params::integer(m, "minutes", Probe::DEFAULT_MINUTES));
            Prefs("lemuri/ui").put("probeMinutes", QString::number(n));
            probe.every(n);
        }
        return QJsonValue(probeMinutes());
    });
    router.op("monitor.tryAll", [this](const QJsonObject &) {
        probe.all();
        return QJsonValue();
    });
    router.op("metrics.refresh", [this](const QJsonObject &m) {
        refresh(Params::need(m, "host"), Params::flag(m, "force"));
        return QJsonValue();
    });
    router.op("metrics.last", [this](const QJsonObject &) {
        QJsonObject r;
        for (auto it = resources.begin(); it != resources.end(); ++it) r[it.key()] = it.value();
        return r;
    });
    router.op("metrics.history", [this](const QJsonObject &m) {
        QString id = Params::need(m, "host");
        return QJsonObject{{"cpu", metrics.cpuHistory(id)}, {"running", metrics.running(id)}};
    });
    router.later("host.updates", [this](const QJsonObject &m, const Reply &r) {
        const Host &h = hosts.host(Params::need(m, "host"));
        metrics.updates(h, Jumps::resolve(h, hosts.all()), Params::flag(m, "force"), [r](const QJsonObject &u) { r.ok(u); });
    });
    router.later("host.security", [this](const QJsonObject &m, const Reply &r) {
        const Host &h = hosts.host(Params::need(m, "host"));
        metrics.security(h, Jumps::resolve(h, hosts.all()), Params::flag(m, "force"), [r](const QJsonObject &s) { r.ok(s); });
    });
}

void Monitor::launch() {
    if (launched) return;
    launched = true;
    probe.every(probeMinutes());
    probe.launch([this](const QHash<QString, Host::Reach> &states) { loop.post([this, states] { hosts.refreshStatuses(states); }); },
                 [this](const QString &id) { return metrics.connected(id); });
    probe.list(targets());
    // The check is cheap and often, so that unlocking the vault shows up soon; the readings keep their own pace.
    auto *timer = new QTimer(&loop);
    QObject::connect(timer, &QTimer::timeout, &loop, [this] { readDue(); });
    timer->start(30'000);
    readDue();
}

void Monitor::readDue() {
    if (probeMinutes() == 0) return;
    bool open = vault.isOpen();
    bool unlocked = open && !vaultWasOpen;
    vaultWasOpen = open;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Host &h : hosts.all()) {
        if (h.received()) continue;
        auto it = resources.constFind(h.id());
        if (it == resources.constEnd()) {
            refresh(h.id(), false);
            continue;
        }
        qint64 age = now - qint64((*it)["when"].toDouble());
        if (age >= ((*it)["error"].isString() ? (unlocked ? 0 : RETRY_AFTER_ERROR_MS) : READ_EVERY_MS)) refresh(h.id(), false);
    }
}

void Monitor::refresh(const QString &id, bool force) {
    const Host &h = hosts.host(id);
    metrics.refresh(h, Jumps::resolve(h, hosts.all()), force, [this, id](const QJsonObject &e) {
        loop.post([this, id, e] {
            if (!hosts.find(id)) return;
            resources.insert(id, e);
            events.event("metrics", {{"host", id}, {"result", e}});
        });
    });
}
