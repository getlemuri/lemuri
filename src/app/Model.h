#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <memory>

class Backend;
class TerminalView;

/** A saved host, as the backend sends it (see backend/PROTOCOL.md): the fields used here, and the whole object. */
struct Host {
    enum class Auth { Key, Password, Agent };
    /** Whether the last probe reached its SSH port. */
    enum class Reach { Online, Offline, Unknown };
    /** As hosts.json writes it. */
    static QString authName(Auth a);

    QJsonObject json;
    QString id, name, group, address, user, key, note, os, fromContact, jump;
    Auth auth = Auth::Key;
    Reach reach = Reach::Unknown;
    int port = 22;
    bool received = false;

    static Host from(const QJsonObject &o);
    QString destination() const { return user + "@" + address + ":" + QString::number(port); }
    bool local() const { return id == LOCAL; }
    static inline const QString LOCAL = QStringLiteral("local");
};

class Session;

/**
 * How a tab is divided: a single session, or two parts side by side (or one above the other), each divided again
 * the same way.
 */
class Pane {
public:
    explicit Pane(Session *s) : leaf(s) {}
    bool divided() const { return leaf == nullptr; }
    Session *session() const { return leaf; }
    bool sideBySide() const { return across; }
    Pane *first() const { return a.get(); }
    Pane *second() const { return b.get(); }
    double divider = 0.5;

    QList<Session *> sessions() const;
    bool split(Session *target, Session *added, bool sideBySide);
    bool replace(Session *old, Session *fresh);
    bool remove(Session *s);

private:
    void collect(QList<Session *> &all) const;
    Session *leaf;
    bool across = false;
    std::unique_ptr<Pane> a, b;
};

/** An open shell: its connection in the backend, its terminal, and where it sits among the tabs. */
class Session {
public:
    enum class State { Connecting, Connected, Closed, Error };
    /** From the protocol's "state" events. */
    static State parse(const QString &state);

    QString id;       // ours, stable
    QString backend;  // the backend's ("s3"), once it answered
    QString hostId;
    QString title;
    State state = State::Connecting;
    QString error;
    bool synced = false;
    qint64 started = 0;
    /** Null once gone: a view may delete it with the container it had put it in. */
    QPointer<TerminalView> view;
    /** Closed by the app (tab closed, disconnect): its end is not an "exit" typed in the shell. */
    bool stopped = false;

    Session *owner() const { return own; }
    Session *tab() { return own ? own : this; }
    Pane *layout() const { return pane.get(); }
    QList<Session *> panes() { return pane ? pane->sessions() : QList<Session *>{this}; }
    bool alive() const { return state == State::Connecting || state == State::Connected; }

private:
    friend class Model;
    Session *own = nullptr;
    std::unique_ptr<Pane> pane;
};

/** A line of the activity log, from the backend's "task" events. */
struct Task {
    enum class Outcome { Running, Ok, Error };
    int id = 0;
    qint64 start = 0, end = 0;
    QString target, text, detail;
    Outcome outcome = Outcome::Running;
};

/** What is in view: a node of the tree and, for a host, its summary or its console. */
struct Position {
    enum Kind { Datacenter, Folder, HostItem, Received, Team };
    Kind kind = Datacenter;
    QString key;   // folder path or host id
    QString item;  // "" (the node's page) or "console"
    bool operator==(const Position &o) const { return kind == o.kind && key == o.key && item == o.item; }
};

/** A transfer in progress or just ended, from the backend's "transfer" events. */
struct Transfer {
    /** Running until the last event, which says how it ended. */
    enum class Outcome { Running, Done, Cancelled, Failed };
    int id = 0;
    bool up = true;
    Outcome outcome = Outcome::Running;
    QString what, file, message;
    bool started = false, finished = false;
    qint64 done = 0, total = 1;
};

/**
 * The state of the interface and the actions that change it, through the backend: hosts, sessions with their tabs
 * and splits, the log, transfers, the team. Views read it and listen to its signals.
 */
class Model : public QObject {
    Q_OBJECT
public:
    explicit Model(Backend *backend, QObject *parent = nullptr);
    /** The sessions still open, and those on their way out, go with it. */
    ~Model() override;
    Backend *backend() const { return d.backend; }

    // ── Hosts ──
    const QList<Host> &hosts() const { return d.hosts; }
    /** All folders, sorted. */
    const QStringList &folders() const { return d.folders; }
    const QStringList &created() const { return d.created; }
    /** host id -> MetricsResult */
    const QHash<QString, QJsonObject> &metrics() const { return d.metrics; }
    const Host *host(const QString &id) const;
    /** The local shell's host: this computer. */
    const Host &localHost() const { return local; }
    bool folderExists(const QString &path) const;
    int connected(const QString &hostId) const;  // sessions CONNECTED; empty id: all

    // ── Vault, team, updates ──
    enum class TeamState { VaultLocked, Ready };
    enum class LanState { Off, VaultLocked, Active, Error };
    bool vaultExists() const { return d.vaultExists; }
    bool vaultOpen() const { return d.vaultOpen; }
    /** What the backend answered about the vault: vaultChanged follows. */
    void setVault(bool exists, bool open);
    TeamState teamState() const { return d.teamState; }
    const QString &teamMessage() const { return d.teamMessage; }
    int deliveries() const { return d.deliveries; }
    /** See backend/PROTOCOL.md, "Team and local network". */
    const QJsonArray &deliveryList() const { return d.deliveryList; }
    const QJsonArray &contacts() const { return d.contacts; }
    const QJsonArray &peers() const { return d.peers; }
    LanState lanState() const { return d.lanState; }
    const QString &lanMessage() const { return d.lanMessage; }
    bool lanOn() const { return d.lanOn; }
    bool lanCarrierGrade() const { return d.lanCarrierGrade; }
    const QString &firewall() const { return d.firewall; }
    const QString &fingerprint() const { return d.fingerprint; }
    const QString &keyCard() const { return d.keyCard; }
    /** Asks the backend for the whole team state (the page opens with it). */
    void refreshTeam();
    const QJsonObject &updateOffered() const { return d.updateOffered; }
    const QJsonObject &updateAvailable() const { return d.updateAvailable; }
    /** A check asked by hand found this (or nothing): updateChanged follows. */
    void setUpdateAvailable(const QJsonObject &release);
    const QString &version() const { return d.version; }
    /** What "hello" said: user, data folder... */
    const QJsonObject &info() const { return d.info; }
    /** "hello" answered: the version, the vault. */
    void setHello(const QJsonObject &hello);

    // ── Log, transfers ──
    const QList<Task> &tasks() const { return d.tasks; }
    /** Running ones, then those ended since the last summary was closed. */
    const QList<Transfer> &transfers() const { return d.transfers; }
    /** The summary closed: transfersChanged follows. */
    void clearTransfers();
    /** The log keeps only what is still running: tasksChanged follows. */
    void clearEndedTasks();

    // ── Navigation ──
    const Position &position() const { return d.position; }
    void select(Position::Kind kind, const QString &key = {}, const QString &item = {});

    // ── Sessions ──
    /** Tabs and their parts, in the row's order. */
    const QList<Session *> &sessions() const { return d.sessions; }
    /** host id -> the session shown in its console. */
    const QHash<QString, Session *> &active() const { return d.active; }
    Session *openLocal();
    Session *openSession(const QString &hostId);
    Session *showOrOpen(const QString &hostId);
    void showSession(Session *part);
    Session *split(Session *target, bool sideBySide, const QString &hostId = {});
    void closePane(Session *s);
    void closeSession(Session *part);
    Session *retry(Session *failed);
    void disconnectHost(const QString &hostId);
    void setSynced(Session *tab, bool on);
    QList<Session *> tabs() const;

    /** The "terminalCommands" setting, applied to every terminal (see TerminalView::setTypesCommands). */
    bool typesCommands() const { return typing; }
    void setTypesCommands(bool on);

    /** A request to the backend; errors without a reply go to a warning. */
    void request(const QString &op, const QJsonObject &params = {}, std::function<void(const QJsonValue &)> ok = nullptr,
                 std::function<void(const QString &)> failed = nullptr);
    /**
     * The same for a widget that may be gone when the answer comes (a page left before a server answered): the
     * answer is then dropped instead of reaching a deleted object.
     */
    void request(const QObject *owner, const QString &op, const QJsonObject &params, std::function<void(const QJsonValue &)> ok,
                 std::function<void(const QString &)> failed = nullptr);

signals:
    void hostsChanged();
    void metricsChanged(const QString &hostId);
    void vaultChanged();
    void teamChanged();
    /** A host sent by a contact has just arrived: the interface offers it at once. */
    void arrival(const QJsonObject &delivery);
    void updateChanged();
    void tasksChanged();
    void transfersChanged();
    void positionChanged();
    void sessionsChanged();
    void sessionChanged(Session *s);
    void typesCommandsChanged(bool on);
    /** An error the user should see, from a request nobody else answers. */
    void warning(const QString &title, const QString &text);

private:
    void onEvent(const QString &name, const QJsonObject &m);
    void showHosts(const QJsonObject &m);
    Session *create(const QString &hostId, Session *owner, std::function<void(Session *)> place, int at);
    void start(Session *s);
    void onState(Session *s, Session::State state, const QString &message);
    void replaceWithLocal(Session *ended);
    void stop(Session *s);
    void drop(Session *s);
    void dispose(Session *s);
    bool consoleShows(Session *tab) const;
    Session *byBackend(const QString &id) const;

    /** The state the views read; changed only here, each change followed by its signal. */
    struct State {
        Backend *backend = nullptr;
        QList<Host> hosts;
        QStringList folders, created;
        QHash<QString, QJsonObject> metrics;
        bool vaultExists = false, vaultOpen = false;
        TeamState teamState = TeamState::VaultLocked;
        QString teamMessage;
        int deliveries = 0;
        QJsonArray deliveryList, contacts, peers;
        LanState lanState = LanState::Off;
        QString lanMessage, firewall, fingerprint, keyCard;
        bool lanOn = false, lanCarrierGrade = false;
        QJsonObject updateOffered, updateAvailable;
        QString version = "v0.1";
        QJsonObject info;
        QList<Task> tasks;
        QList<Transfer> transfers;
        Position position;
        QList<Session *> sessions;
        QHash<QString, Session *> active;
    } d;
    Host local;
    int counter = 0;
    bool typing = false;
    /** Out of the list, deleted on the next turn (see drop); here until then, so that none is left if the model goes first. */
    QList<Session *> leaving;
};
