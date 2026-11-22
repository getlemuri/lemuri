#include "sessions/terminals/Sessions.h"

#include "base/core/Error.h"
#include "base/config/I18n.h"
#include "base/core/Params.h"
#include "base/core/Runtime.h"
#include "hosts/list/Hosts.h"
#include "hosts/routes/Jumps.h"
#include "sessions/terminals/LocalShell.h"
#include "ssh/Ssh.h"
#include "vault/Vault.h"
#include "vault/VaultAccess.h"

#include <QFileInfo>
#include <QJsonObject>
#include <mutex>

namespace {
QString text(const QJsonValue &a) {
    return a.isObject() && a.toObject().contains("text") ? a["text"].toString() : QString();
}
}  // namespace

SshPrompts Sessions::prompts(const QString &sid, const Host &host) {
    struct State {
        std::mutex m;
        bool triedSaved = false, wasSaved = false;
        QString toRemember;
    };
    auto st = std::make_shared<State>();
    QString id = host.id(), name = host.name(), destination = host.destination();
    SshPrompts p;
    Prompter &prompter = this->prompter;
    VaultAccess &vaultAccess = this->vaultAccess;
    Activity &activity = this->activity;
    Loop &loop = this->loop;
    p.confirm = [=, &prompter](const QString &message) {
        QJsonValue a = prompter.ask("confirm", {{"session", sid}, {"title", I18n::t("ssh.unknownHost", name)},
                                               {"message", message}, {"action", I18n::t("ssh.acceptConnect")}});
        return a.toBool();
    };
    p.password = [=, &prompter, &vaultAccess](const QString &message) {
        QString saved;
        bool first;
        {
            std::lock_guard<std::mutex> l(st->m);
            first = !st->triedSaved;
            st->triedSaved = true;
        }
        if (first && !id.isEmpty() && vaultAccess.vault().exists() && vaultAccess.unlock(I18n::t("ssh.passwordFor", destination)))
            saved = vaultAccess.vault().password(id);
        if (!saved.isNull()) {
            std::lock_guard<std::mutex> l(st->m);
            st->wasSaved = true;
            return saved;
        }
        bool rejected;
        {
            std::lock_guard<std::mutex> l(st->m);
            rejected = st->wasSaved;
        }
        QString question = rejected ? I18n::t("ssh.savedRejected") + "\n" + message : message;
        QJsonValue a = prompter.ask("password", {{"session", sid}, {"title", I18n::t("ssh.passwordFor", destination)}, {"message", question},
                                                {"note", I18n::t("ssh.passwordNote")}, {"remember", I18n::t("ssh.remember")}});
        QString typed = text(a);
        std::lock_guard<std::mutex> l(st->m);
        st->toRemember = !typed.isNull() && a["remember"].toBool() ? typed : QString();
        return typed;
    };
    p.passphrase = [=, &prompter](const QString &message) {
        return text(prompter.ask("secret", {{"session", sid}, {"title", I18n::t("ssh.keyFor", destination)}, {"message", message},
                                           {"note", I18n::t("ssh.passphraseNotSaved")}}));
    };
    p.reply = [=, &prompter](const QString &question) {
        return text(prompter.ask("secret", {{"session", sid}, {"title", I18n::t("ssh.signIn", destination)}, {"message", question},
                                           {"note", I18n::t("ssh.answerNotSaved")}}));
    };
    p.authenticated = [=, &vaultAccess, &activity, &loop] {
        QString secret;
        {
            std::lock_guard<std::mutex> l(st->m);
            secret = st->toRemember;
            st->toRemember.clear();
        }
        if (secret.isEmpty() || id.isEmpty()) return;
        // On a thread of its own: the connection must not wait for the master password.
        loop.work([&vaultAccess, &activity, id, name, secret] {
            if (!vaultAccess.unlock(I18n::t("ssh.remember"))) return;
            try {
                vaultAccess.vault().remember(id, secret);
            } catch (const Fail &f) {
                activity.done(name, I18n::t("vault.cannotSave", f.why), Activity::Outcome::Error, f.why);
            }
        });
    };
    return p;
}

Sessions::Sessions(Events &events, Loop &loop, Activity &activity, Prompter &prompter, VaultAccess &vaultAccess, Hosts &hosts)
    : events(events), loop(loop), activity(activity), prompter(prompter), vaultAccess(vaultAccess), hosts(hosts) {
    loop.onStop([this] {
        for (const QString &sid : live.keys()) close(sid);
        threads.join();
    });
}

void Sessions::ops(Router &router) {
    router.op("session.open", [this](const QJsonObject &m) { return open(m); });
    router.op("session.input", [this](const QJsonObject &m) {
        transport(Params::need(m, "session"))->send(Params::need(m, "data").toUtf8());
        return QJsonValue();
    });
    router.op("session.resize", [this](const QJsonObject &m) {
        transport(Params::need(m, "session"))->resize(Params::integer(m, "cols", 80), Params::integer(m, "rows", 24));
        return QJsonValue();
    });
    router.op("session.close", [this](const QJsonObject &m) {
        close(Params::need(m, "session"));
        return QJsonValue();
    });
    router.op("session.disconnectHost", [this](const QJsonObject &m) {
        QString h = Params::need(m, "host");
        for (const QString &sid : hostOf.keys())
            if (hostOf.value(sid) == h) close(sid);
        return QJsonValue();
    });
}

std::shared_ptr<Transport> Sessions::transport(const QString &sid) const {
    auto t = live.value(sid);
    if (!t) throw Fail{"No session " + sid};
    return t;
}

std::shared_ptr<SshConnection> Sessions::ssh(const QString &sid) const {
    auto shell = std::dynamic_pointer_cast<SshShell>(transport(sid));
    if (!shell) throw Fail{I18n::t("transfer.upload.onlySsh")};
    auto c = shell->connection();
    if (!c) throw Fail{I18n::t("ssh.notReady")};
    return c;
}

QString Sessions::host(const QString &sid) const {
    return hostOf.value(sid, "local");
}

void Sessions::close(const QString &sid) {
    auto t = live.take(sid);
    hostOf.remove(sid);
    if (t) t->close();
}

QJsonObject Sessions::open(const QJsonObject &m) {
    QString sid = "s" + QString::number(++next);
    std::shared_ptr<Transport> t;
    QString title, hostId;
    int task = 0;
    if (Params::flag(m, "local")) {
        t = std::make_shared<LocalShell>();
        QString shell = qEnvironmentVariable("SHELL");
        title = shell.trimmed().isEmpty() ? "bash" : QFileInfo(shell).fileName();
        hostId = "local";
    } else {
        Host h = hosts.host(Params::need(m, "host"));
        Jumps::Route route = Jumps::resolve(h, hosts.all());
        auto shell = std::make_shared<SshShell>(h, route.hops, route.error, [this, sid](const Host &x) { return prompts(sid, x); });
        QString id = h.id();
        shell->onOs = [this, id](const QString &os) { loop.post([this, id, os] { hosts.refreshOs(id, os); }); };
        t = shell;
        title = h.name();
        hostId = h.id();
        task = activity.task(h.name(), I18n::t("task.sshSession", h.destination()));
    }
    live.insert(sid, t);
    hostOf.insert(sid, hostId);
    t->onData = [this, sid](const QString &d) { events.event("data", {{"session", sid}, {"data", d}}); };
    auto log = std::make_shared<std::atomic<int>>(task);
    std::weak_ptr<Transport> weak = t;
    using State = Transport::State;
    t->onState = [this, sid, hostId, log, weak](State state, const QString &message) {
        events.event("state", {{"session", sid}, {"state", Transport::name(state)}, {"message", message.isNull() ? QJsonValue() : QJsonValue(message)}});
        if (state == State::Connected && hostId != "local") loop.post([this, hostId] { hosts.refreshStatuses({{hostId, Host::Reach::Online}}); });
        // Only the first outcome goes in the log: connected, or why it never was.
        if (state != State::Connecting) {
            int line = log->exchange(0);
            if (line) {
                if (state == State::Connected) activity.finish(line, Activity::Outcome::Ok);
                else activity.finish(line, Activity::Outcome::Error, message.isEmpty() ? I18n::t("task.connectionClosed") : message);
            }
        }
        if (state == State::Closed || state == State::Error)
            loop.post([this, sid, weak] {
                // Only if it is still this one (an id is never reused, but be sure).
                if (live.value(sid) == weak.lock()) {
                    live.remove(sid);
                    hostOf.remove(sid);
                }
            });
    };
    t->launch(threads, Params::integer(m, "cols", 80), Params::integer(m, "rows", 24));
    return {{"session", sid}, {"title", title}, {"host", hostId}};
}
