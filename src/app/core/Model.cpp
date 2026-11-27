#include "app/core/Model.h"

#include "app/core/Backend.h"
#include "app/tools/Texts.h"
#include "terminal/TerminalView.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

// ── Host ──

Host Host::from(const QJsonObject &o) {
    Host h;
    h.json = o;
    h.id = o["id"].toString();
    h.name = o["name"].toString();
    h.group = o["group"].toString();
    h.address = o["address"].toString();
    // The backend's defaults, for a field it would never leave out anyway.
    h.user = o["user"].toString("root");
    QString auth = o["authMethod"].toString("KEY");
    h.auth = auth == "PASSWORD" ? Auth::Password : auth == "AGENT" ? Auth::Agent : Auth::Key;
    h.key = o["key"].toString();
    h.note = o["note"].toString();
    h.os = o["os"].toString();
    QString state = o["state"].toString();
    h.reach = state == "ONLINE" ? Reach::Online : state == "OFFLINE" ? Reach::Offline : Reach::Unknown;
    h.fromContact = o["fromContact"].toString();
    h.jump = o["jump"].toString();
    h.port = o["port"].toInt(22);
    h.received = o["received"].toBool();
    return h;
}

QString Host::authName(Auth a) {
    return a == Auth::Password ? "PASSWORD" : a == Auth::Agent ? "AGENT" : "KEY";
}

Session::State Session::parse(const QString &state) {
    if (state == "CONNECTED") return State::Connected;
    if (state == "CLOSED") return State::Closed;
    if (state == "ERROR") return State::Error;
    return State::Connecting;
}

namespace {
Task::Outcome taskOutcome(const QString &o) {
    return o == "OK" ? Task::Outcome::Ok : o == "ERROR" ? Task::Outcome::Error : Task::Outcome::Running;
}

Transfer::Outcome transferOutcome(const QString &o) {
    if (o == "DONE") return Transfer::Outcome::Done;
    if (o == "CANCELLED") return Transfer::Outcome::Cancelled;
    return Transfer::Outcome::Failed;
}

Model::TeamState teamState(const QString &s) {
    return s == "READY" ? Model::TeamState::Ready : Model::TeamState::VaultLocked;
}

Model::LanState lanState(const QString &s) {
    if (s == "ACTIVE") return Model::LanState::Active;
    if (s == "VAULT_LOCKED") return Model::LanState::VaultLocked;
    if (s == "ERROR") return Model::LanState::Error;
    return Model::LanState::Off;
}
}  // namespace

// ── Pane ──

QList<Session *> Pane::sessions() const {
    QList<Session *> all;
    collect(all);
    return all;
}

void Pane::collect(QList<Session *> &all) const {
    if (!divided()) all.append(leaf);
    else {
        a->collect(all);
        b->collect(all);
    }
}

bool Pane::split(Session *target, Session *added, bool sideBySide) {
    if (!divided()) {
        if (leaf != target) return false;
        a = std::make_unique<Pane>(target);
        b = std::make_unique<Pane>(added);
        across = sideBySide;
        divider = 0.5;
        leaf = nullptr;
        return true;
    }
    return a->split(target, added, sideBySide) || b->split(target, added, sideBySide);
}

bool Pane::replace(Session *old, Session *fresh) {
    if (!divided()) {
        if (leaf != old) return false;
        leaf = fresh;
        return true;
    }
    return a->replace(old, fresh) || b->replace(old, fresh);
}

bool Pane::remove(Session *s) {
    if (!divided()) return false;
    for (int i = 0; i < 2; i++) {
        Pane *gone = i == 0 ? a.get() : b.get();
        if (gone->divided() || gone->leaf != s) continue;
        std::unique_ptr<Pane> kept = std::move(i == 0 ? b : a);
        leaf = kept->leaf;
        across = kept->across;
        divider = kept->divider;
        a = std::move(kept->a);
        b = std::move(kept->b);
        return true;
    }
    return a->remove(s) || b->remove(s);
}

// ── Model ──

Model::Model(Backend *backend, QObject *parent) : QObject(parent) {
    d.backend = backend;
    QString shell = qEnvironmentVariable("SHELL");
    local.id = Host::LOCAL;
    local.name = shell.isEmpty() ? "bash" : QFileInfo(shell).fileName();
    local.user = qEnvironmentVariable("USER", qEnvironmentVariable("LOGNAME"));
    local.address = "localhost";

    connect(d.backend, &Backend::data, this, [this](const QString &id, const QString &text) {
        if (Session *s = byBackend(id)) s->view->write(text);
    });
    connect(d.backend, &Backend::state, this, [this](const QString &id, const QString &state, const QString &message) {
        if (Session *s = byBackend(id)) onState(s, Session::parse(state), message);
    });
    connect(d.backend, &Backend::event, this, &Model::onEvent);
}

void Model::request(const QString &op, const QJsonObject &params, std::function<void(const QJsonValue &)> ok,
                    std::function<void(const QString &)> failed) {
    // LEMURI_SHOT: the pictures without a screen read nothing from the servers, unless the backend is a demo that
    // makes the readings up (LEMURI_SHOT_DEMO, the website's pictures).
    static const bool offline = qEnvironmentVariableIsSet("LEMURI_SHOT") && !qEnvironmentVariableIsSet("LEMURI_SHOT_DEMO");
    if (offline && (op == "metrics.refresh" || op == "host.updates" || op == "host.security")) return;
    d.backend->request(op, params, [this, op, ok, failed](const QJsonValue &v, const QString &error) {
        if (!error.isEmpty()) {
            if (failed) failed(error);
            else emit warning("Lemuri", error);
            return;
        }
        if (ok) ok(v);
    });
}

void Model::request(const QObject *owner, const QString &op, const QJsonObject &params, std::function<void(const QJsonValue &)> ok,
                    std::function<void(const QString &)> failed) {
    QPointer<const QObject> alive(owner);
    request(op, params, ok ? std::function<void(const QJsonValue &)>([alive, ok](const QJsonValue &v) { if (alive) ok(v); }) : nullptr,
            failed ? std::function<void(const QString &)>([alive, failed](const QString &e) { if (alive) failed(e); }) : nullptr);
}

void Model::onEvent(const QString &name, const QJsonObject &m) {
    if (name == "hosts") {
        showHosts(m);
    } else if (name == "metrics") {
        d.metrics.insert(m["host"].toString(), m["result"].toObject());
        emit metricsChanged(m["host"].toString());
    } else if (name == "vault") {
        d.vaultExists = m["exists"].toBool();
        d.vaultOpen = m["open"].toBool();
        emit vaultChanged();
    } else if (name == "sharing") {
        d.teamState = ::teamState(m["state"].toString());
        d.teamMessage = m["message"].toString();
        emit teamChanged();
        // The keys come and go with the vault: the fingerprint and the card with them.
        refreshTeam();
    } else if (name == "deliveries") {
        d.deliveryList = m["deliveries"].toArray();
        d.deliveries = int(d.deliveryList.size());
        emit teamChanged();
    } else if (name == "arrival") {
        emit arrival(m["delivery"].toObject());
    } else if (name == "contacts") {
        d.contacts = m["contacts"].toArray();
        emit teamChanged();
    } else if (name == "peers") {
        d.peers = m["peers"].toArray();
        emit teamChanged();
    } else if (name == "lan") {
        d.lanState = ::lanState(m["state"].toString());
        d.lanMessage = m["message"].toString();
        d.lanOn = m["on"].toBool();
        emit teamChanged();
    } else if (name == "firewall") {
        d.firewall = m["warning"].toString();
        emit teamChanged();
    } else if (name == "update") {
        d.updateOffered = m["offered"].toObject();
        d.updateAvailable = m["available"].toObject();
        emit updateChanged();
    } else if (name == "task") {
        int id = m["id"].toInt();
        auto it = std::find_if(d.tasks.begin(), d.tasks.end(), [id](const Task &t) { return t.id == id; });
        if (it == d.tasks.end()) {
            Task t;
            t.id = id;
            t.start = qint64(m["when"].toDouble(QDateTime::currentMSecsSinceEpoch()));
            t.target = m["target"].toString();
            t.text = m["text"].toString();
            t.outcome = taskOutcome(m["outcome"].toString());
            d.tasks.append(t);
        } else if (it->outcome == Task::Outcome::Running) {
            it->outcome = taskOutcome(m["outcome"].toString());
            it->detail = m["detail"].toString();
            it->end = qint64(m["ended"].toDouble(QDateTime::currentMSecsSinceEpoch()));
        }
        emit tasksChanged();
    } else if (name == "transfer") {
        int id = m["transfer"].toInt();
        auto it = std::find_if(d.transfers.begin(), d.transfers.end(), [id](const Transfer &t) { return t.id == id; });
        // A new one after a summary that ended well: the summary goes.
        if (it == d.transfers.end()) {
            bool running = std::any_of(d.transfers.begin(), d.transfers.end(), [](const Transfer &t) { return !t.finished; });
            bool failed = std::any_of(d.transfers.begin(), d.transfers.end(), [](const Transfer &t) { return t.outcome == Transfer::Outcome::Failed; });
            if (!running && !failed) d.transfers.clear();
            d.transfers.append(Transfer{});
            it = d.transfers.end() - 1;
            it->id = id;
        }
        it->up = m["direction"].toString() != "down";
        it->what = m["what"].toString();
        if (m.contains("file")) it->file = m["file"].toString();
        it->started = m["started"].toBool(it->started);
        it->done = qint64(m["done"].toDouble());
        it->total = std::max<qint64>(1, qint64(m["total"].toDouble(1)));
        it->finished = m["finished"].toBool();
        if (it->finished) {
            it->started = true;
            it->outcome = transferOutcome(m["outcome"].toString());
            it->message = m["message"].toString();
        }
        emit transfersChanged();
    }
}

void Model::showHosts(const QJsonObject &m) {
    d.hosts.clear();
    for (const QJsonValue &v : m["hosts"].toArray()) d.hosts.append(Host::from(v.toObject()));
    d.folders.clear();
    for (const QJsonValue &v : m["folders"].toArray()) d.folders.append(v.toString());
    d.created.clear();
    for (const QJsonValue &v : m["created"].toArray()) d.created.append(v.toString());
    // The node in view may have gone: a removed host, a folder emptied and deleted.
    if ((d.position.kind == Position::HostItem && !host(d.position.key)) || (d.position.kind == Position::Folder && !folderExists(d.position.key))
        || (d.position.kind == Position::Received && std::none_of(d.hosts.begin(), d.hosts.end(), [](const Host &h) { return h.received; })))
        d.position = Position{};
    emit hostsChanged();
    emit positionChanged();
}

void Model::refreshTeam() {
    request("team.state", {}, [this](const QJsonValue &v) {
        QJsonObject r = v.toObject();
        d.teamState = ::teamState(r["state"].toString());
        d.teamMessage = r["message"].toString();
        d.lanState = ::lanState(r["lanState"].toString());
        d.lanMessage = r["lanMessage"].toString();
        d.lanOn = r["lanOn"].toBool();
        d.lanCarrierGrade = r["lanCarrierGrade"].toBool();
        d.firewall = r["firewall"].toString();
        d.fingerprint = r["fingerprint"].toString();
        d.keyCard = r["keyCard"].toString();
        d.contacts = r["contacts"].toArray();
        d.peers = r["peers"].toArray();
        d.deliveryList = r["deliveries"].toArray();
        d.deliveries = int(d.deliveryList.size());
        emit teamChanged();
    });
}

const Host *Model::host(const QString &id) const {
    if (id == Host::LOCAL) return &local;
    for (const Host &h : d.hosts)
        if (h.id == id) return &h;
    return nullptr;
}

bool Model::folderExists(const QString &path) const {
    for (const QString &f : d.folders)
        if (f.compare(path, Qt::CaseInsensitive) == 0) return true;
    return false;
}

int Model::connected(const QString &hostId) const {
    int n = 0;
    for (Session *s : d.sessions)
        if ((hostId.isEmpty() || s->hostId == hostId) && s->state == Session::State::Connected) n++;
    return n;
}

void Model::select(Position::Kind kind, const QString &key, const QString &item) {
    Position p{kind, key, item};
    if (p == d.position) return;
    d.position = p;
    emit positionChanged();
}

QList<Session *> Model::tabs() const {
    QList<Session *> l;
    for (Session *s : d.sessions)
        if (!s->owner()) l.append(s);
    return l;
}

Session *Model::byBackend(const QString &id) const {
    for (Session *s : d.sessions)
        if (s->backend == id) return s;
    return nullptr;
}

// ── Sessions ──

Session *Model::create(const QString &hostId, Session *owner, std::function<void(Session *)> place, int at) {
    const Host *h = host(hostId);
    if (!h) return nullptr;
    int n = 1;
    for (Session *x : d.sessions)
        if (x->hostId == hostId) n++;
    auto *s = new Session;
    s->id = QString::number(++counter);
    s->hostId = hostId;
    s->title = h->name + " #" + QString::number(n);
    s->started = QDateTime::currentMSecsSinceEpoch();
    s->view = new TerminalView;
    s->view->setTypesCommands(typing);
    // What is typed goes to the shell, or to every part of the tab when it is synced.
    connect(s->view, &TerminalView::input, this, [this, s](const QByteArray &bytes) {
        QList<Session *> to = s->tab()->synced ? s->tab()->panes() : QList<Session *>{s};
        for (Session *p : to)
            if (!p->backend.isEmpty()) d.backend->request("session.input", {{"session", p->backend}, {"data", QString::fromUtf8(bytes)}});
    });
    connect(s->view, &TerminalView::command, this, [this, s](const QByteArray &bytes) {
        if (!s->backend.isEmpty()) d.backend->request("session.input", {{"session", s->backend}, {"data", QString::fromUtf8(bytes)}});
    });
    connect(s->view, &TerminalView::resized, this, [this, s](int cols, int rows) {
        if (!s->backend.isEmpty()) d.backend->request("session.resize", {{"session", s->backend}, {"cols", cols}, {"rows", rows}});
    });
    s->own = owner;
    // A server started anew goes first among the recent ones (the local shell is not a server).
    if (hostId != Host::LOCAL) {
        d.recent.removeAll(hostId);
        d.recent.prepend(hostId);
        d.recent = d.recent.mid(0, RECENT_HOSTS);
        request("prefs.put", {{"node", "ui"}, {"values", QJsonObject{{"recentHosts", d.recent.join(',')}}}});
    }
    if (!owner) {
        d.sessions.insert(at < 0 ? d.sessions.size() : std::min<qsizetype>(at, d.sessions.size()), s);
        d.active.insert(hostId, s);
    } else {
        place(s);
        // Right after the other parts of its tab: if the tab closes, one of them is already in its place in the row.
        int after = -1;
        for (Session *p : owner->panes())
            if (p != s) after = std::max(after, int(d.sessions.indexOf(p)));
        d.sessions.insert(after < 0 ? d.sessions.size() : after + 1, s);
    }
    start(s);
    return s;
}

void Model::start(Session *s) {
    // The real size of the terminal is known once the console has laid it out.
    QTimer::singleShot(0, this, [this, s] {
        if (!d.sessions.contains(s)) return;
        QJsonObject p{{"cols", s->view->columns()}, {"rows", s->view->lines()}};
        if (s->hostId == Host::LOCAL) p["local"] = true;
        else p["host"] = s->hostId;
        d.backend->request("session.open", p, [this, s](const QJsonValue &ok, const QString &error) {
            if (!d.sessions.contains(s)) {
                // Closed before it opened: the backend's session goes too.
                if (error.isEmpty()) d.backend->request("session.close", {{"session", ok.toObject()["session"].toString()}});
                return;
            }
            if (!error.isEmpty()) {
                s->view->write("\r\n\x1b[31m" + error + "\x1b[0m\r\n");
                onState(s, Session::State::Error, error);
                return;
            }
            s->backend = ok.toObject()["session"].toString();
            d.backend->request("session.resize", {{"session", s->backend}, {"cols", s->view->columns()}, {"rows", s->view->lines()}});
        });
    });
}

void Model::onState(Session *s, Session::State state, const QString &message) {
    using State = Session::State;
    State before = s->state;
    if (state == State::Error) s->error = message;
    s->state = state;
    emit sessionChanged(s);
    // Only the first report of the end.
    if (state != State::Closed || before == State::Closed) return;
    if (s->stopped || !d.sessions.contains(s) || !s->tab()->panes().contains(s)) return;
    bool isLocal = s->hostId == Host::LOCAL;
    // "exit" in an SSH shell that was running: a local shell takes its place, as when ssh ends in a terminal.
    if (!isLocal && before == State::Connected) replaceWithLocal(s);
    // A part of a split tab goes away when its shell ends; so does a local tab. An SSH tab that never connected stays.
    else if (s->tab()->panes().size() > 1) closePane(s);
    else if (isLocal) closeSession(s);
}

bool Model::consoleShows(Session *tab) const {
    if (d.position.kind != Position::HostItem || d.position.item != "console") return false;
    Session *a = d.active.value(d.position.key);
    return a && tab->panes().contains(a);
}

Session *Model::openLocal() {
    return openSession(Host::LOCAL);
}

Session *Model::openSession(const QString &hostId) {
    Session *s = create(hostId, nullptr, nullptr, -1);
    emit sessionsChanged();
    if (s) select(Position::HostItem, hostId, "console");
    return s;
}

Session *Model::showOrOpen(const QString &hostId) {
    Session *s = d.active.value(hostId);
    if (!s || !d.sessions.contains(s) || !s->alive()) {
        s = nullptr;
        for (Session *x : d.sessions)
            if (!x->owner() && x->hostId == hostId && x->alive()) s = x;
    }
    if (!s) return openSession(hostId);
    showSession(s);
    return s;
}

void Model::openSplit(const QStringList &hostIds) {
    QStringList ids;
    for (const QString &id : hostIds)
        if (host(id)) ids << id;
    Session *first = nullptr;
    for (int at = 0; at < ids.size(); at += PER_TAB) {
        QStringList part = ids.mid(at, PER_TAB);
        Session *a = openSession(part[0]);
        if (!a) continue;
        if (!first) first = a;
        if (part.size() < 2) continue;
        Session *b = split(a, true, part[1]);
        // Three: the right half divided; four: both halves.
        if (part.size() == 4) split(a, false, part[2]);
        if (part.size() >= 3) split(b, false, part.last());
    }
    if (first) showSession(first);
}

void Model::showSession(Session *part) {
    Session *s = part->tab();
    d.active.insert(s->hostId, s);
    // The same position again still has to redraw: the tab in view changed.
    Position p{Position::HostItem, s->hostId, "console"};
    d.position = p;
    emit positionChanged();
}

Session *Model::split(Session *target, bool sideBySide, const QString &hostId) {
    Session *tab = target->tab();
    if (!tab->pane) tab->pane = std::make_unique<Pane>(tab);
    Session *s = create(hostId.isEmpty() ? target->hostId : hostId, tab, [tab, target, sideBySide](Session *x) {
        tab->pane->split(target, x, sideBySide);
    }, -1);
    emit sessionsChanged();
    return s;
}

void Model::replaceWithLocal(Session *ended) {
    Session *tab = ended->tab();
    bool inView = consoleShows(tab);
    Session *fresh;
    if (tab->panes().size() > 1) {
        fresh = split(ended, true, Host::LOCAL);
        closePane(ended);
    } else {
        fresh = create(Host::LOCAL, nullptr, nullptr, int(d.sessions.indexOf(ended)) + 1);
        closeSession(ended);
    }
    if (inView && fresh) showSession(fresh);
    emit sessionsChanged();
}

Session *Model::retry(Session *failed) {
    Session *tab = failed->tab();
    if (!d.sessions.contains(failed)) return nullptr;
    bool inView = consoleShows(tab);
    Session *fresh;
    if (tab->panes().size() > 1) {
        fresh = create(failed->hostId, tab, [tab, failed](Session *x) { tab->pane->replace(failed, x); }, -1);
        closePane(failed);
    } else {
        fresh = create(failed->hostId, nullptr, nullptr, int(d.sessions.indexOf(failed)) + 1);
        closeSession(failed);
    }
    if (inView && fresh) showSession(fresh);
    emit sessionsChanged();
    return fresh;
}

void Model::closePane(Session *s) {
    Session *tab = s->tab();
    if (tab->panes().size() < 2) {
        closeSession(tab);
        return;
    }
    tab->pane->remove(s);
    Session *remaining = tab;
    if (s != tab) {
        if (!tab->pane->divided()) tab->pane.reset();
        d.sessions.removeOne(s);
    } else {
        // The first of the other parts becomes the tab, in its place in the row.
        Session *heir = tab->pane->sessions().first();
        heir->own = nullptr;
        heir->synced = tab->synced;
        remaining = heir;
        if (tab->pane->divided()) heir->pane = std::move(tab->pane);
        tab->pane.reset();
        for (Session *p : heir->panes())
            if (p != heir) p->own = heir;
        if (d.active.value(tab->hostId) == tab) {
            Session *other = heir->hostId == tab->hostId ? heir : nullptr;
            if (!other)
                for (Session *x : d.sessions)
                    if (!x->owner() && x != tab && x->hostId == tab->hostId) other = x;
            if (other) d.active.insert(tab->hostId, other);
            else d.active.remove(tab->hostId);
        }
        if (!d.active.contains(heir->hostId)) d.active.insert(heir->hostId, heir);
        // The heir is right after the tab in the row (see create): it takes its place.
        d.sessions.removeOne(tab);
    }
    // Back to one part, nothing left to sync.
    if (remaining->panes().size() < 2) remaining->synced = false;
    stop(s);
    emit sessionsChanged();
    drop(s);
}

void Model::closeSession(Session *part) {
    Session *s = part->tab();
    QList<Session *> parts = s->panes();
    bool inView = consoleShows(s);
    QList<Session *> row = tabs();
    qsizetype i = row.indexOf(s);
    Session *nearby = i > 0 ? row[i - 1] : i + 1 < row.size() ? row[i + 1] : nullptr;
    s->pane.reset();
    for (Session *p : parts) {
        p->own = nullptr;
        stop(p);
        d.sessions.removeOne(p);
    }
    for (Session *p : parts) {
        if (d.active.value(p->hostId) != p) continue;
        Session *other = nullptr;
        for (Session *x : d.sessions)
            if (!x->owner() && x->hostId == p->hostId) other = x;
        if (other) d.active.insert(p->hostId, other);
        else d.active.remove(p->hostId);
    }
    emit sessionsChanged();
    if (inView && nearby) showSession(nearby);
    else if (inView) emit positionChanged();
    for (Session *p : parts) drop(p);
}

void Model::disconnectHost(const QString &hostId) {
    for (Session *s : d.sessions)
        if (s->hostId == hostId) stop(s);
}

void Model::setSynced(Session *tab, bool on) {
    tab->synced = on;
    emit sessionsChanged();
}

void Model::setShowFiles(Session *s, bool on) {
    s->showFiles = on;
    emit sessionsChanged();
}

void Model::stop(Session *s) {
    if (s->alive()) s->stopped = true;
    if (!s->backend.isEmpty()) d.backend->request("session.close", {{"session", s->backend}});
    else if (s->alive()) {
        // Not opened yet: it will never report its end, so it ends here.
        s->state = Session::State::Closed;
        emit sessionChanged(s);
    }
}

/** A session that left the list: its terminal and itself go once the views have let go of them. */
void Model::drop(Session *s) {
    leaving << s;
    QTimer::singleShot(0, this, [this, s] {
        if (leaving.removeOne(s)) dispose(s);
    });
}

void Model::dispose(Session *s) {
    delete s->view.data();
    delete s->files.data();
    delete s;
}

void Model::setVault(bool exists, bool open) {
    d.vaultExists = exists;
    d.vaultOpen = open;
    emit vaultChanged();
}

void Model::setHello(const QJsonObject &hello) {
    d.info = hello;
    d.version = hello["version"].toString(d.version);
    setVault(hello["vaultExists"].toBool(), hello["vaultOpen"].toBool());
}

void Model::setUpdateAvailable(const QJsonObject &release) {
    d.updateAvailable = release;
    emit updateChanged();
}

void Model::clearTransfers() {
    d.transfers.clear();
    emit transfersChanged();
}

void Model::clearEndedTasks() {
    d.tasks.erase(std::remove_if(d.tasks.begin(), d.tasks.end(), [](const Task &t) { return t.outcome != Task::Outcome::Running; }),
                  d.tasks.end());
    emit tasksChanged();
}

QStringList Model::recentHosts() const {
    QStringList out;
    for (const QString &id : d.recent)
        if (host(id)) out << id;
    return out.mid(0, RECENT_HOSTS);
}

void Model::setRecentHosts(const QStringList &ids) {
    d.recent = ids.mid(0, RECENT_HOSTS);
}

void Model::setLaunchGroups(const QList<LaunchGroup> &groups, bool save) {
    d.launches = groups;
    if (!save) return;
    QJsonArray all;
    for (const LaunchGroup &g : groups) all.append(QJsonObject{{"name", g.name}, {"hosts", QJsonArray::fromStringList(g.hosts)}});
    request("prefs.put", {{"node", "ui"}, {"values", QJsonObject{{"launchGroups", QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact))}}}});
}

void Model::setTypesCommands(bool on) {
    typing = on;
    for (Session *s : d.sessions)
        if (s->view) s->view->setTypesCommands(on);
    emit typesCommandsChanged(on);
}

Model::~Model() {
    for (Session *s : d.sessions + leaving) dispose(s);
}
