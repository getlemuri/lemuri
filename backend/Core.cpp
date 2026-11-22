#include "Core.h"

#include "backup/Backup.h"
#include "base/core/Error.h"
#include "base/system/Files.h"
#include "base/config/I18n.h"
#include "base/core/Params.h"
#include "base/config/Prefs.h"
#include "hosts/list/Hosts.h"
#include "monitoring/live/Monitor.h"
#include "sessions/terminals/Sessions.h"
#include "sessions/transfers/Transfers.h"
#include "sharing/team/Team.h"
#include "updates/Updates.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cstdio>

Core::Core() : router([this](const QJsonObject &m) { send(m); }) {
    hosts = std::make_unique<Hosts>(*this, loop, activity, vault);
    sessions = std::make_unique<Sessions>(*this, loop, activity, prompter, vaultAccess, *hosts);
    transfers = std::make_unique<Transfers>(*this, loop, prompter, *sessions);
    monitor = std::make_unique<Monitor>(*this, loop, *hosts, vault);
    team = std::make_unique<Team>(*this, loop, prompter, activity, vaultAccess, *hosts);
    backups = std::make_unique<Backups>(activity, vaultAccess, *hosts, *team);
    updates = std::make_unique<Updates>(*this, loop);
    ops();
    vaultAccess.ops(router);
    hosts->ops(router);
    sessions->ops(router);
    transfers->ops(router);
    monitor->ops(router);
    team->ops(router);
    backups->ops(router);
    updates->ops(router);
}

// The modules go in reverse order of their making: each one before those it uses.
Core::~Core() {
    loop.stop();
    if (input.joinable()) input.join();
    updates.reset();
    backups.reset();
    team.reset();
    monitor.reset();
    transfers.reset();
    sessions.reset();
    hosts.reset();
}

int Core::run() {
    // Standard input on a thread of its own: the main thread serves the requests and the modules.
    input = std::thread([this] { read(); });
    int code = QCoreApplication::exec();
    // The frontend has gone: questions get no answer, the modules stop and their threads end.
    prompter.close();
    loop.stop();
    input.join();
    return code;
}

void Core::read() {
    // No request is anywhere near this: a longer line is dropped whole, not kept growing.
    constexpr size_t MAX_LINE = 32 * 1024 * 1024;
    std::string line;
    bool dropping = false;
    char buffer[65536];
    while (std::fgets(buffer, sizeof buffer, stdin)) {
        line += buffer;
        bool whole = !line.empty() && line.back() == '\n';
        if (line.size() > MAX_LINE || (dropping && !whole)) {
            if (!dropping) std::fprintf(stderr, "A request longer than %zu bytes, dropped\n", MAX_LINE);
            dropping = !whole;
            line.clear();
            continue;
        }
        if (dropping) {
            dropping = false;
            line.clear();
            continue;
        }
        if (!whole) continue;
        QJsonParseError error;
        QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(line), &error);
        line.clear();
        if (!d.isObject()) {
            if (error.error != QJsonParseError::NoError) std::fprintf(stderr, "Not a JSON object: %s\n", qPrintable(error.errorString()));
            continue;
        }
        QJsonObject m = d.object();
        // Answers go straight to who waits for them: the main thread may be busy.
        if (m["op"].toString() == "answer") prompter.answer(m);
        else loop.post([this, m] { router.handle(m); });
    }
    // Questions are released at once: a worker waiting for one would hold up the end.
    prompter.close();
    loop.post([] { QCoreApplication::exit(0); });
}

void Core::event(const QString &name, QJsonObject fields) {
    fields["event"] = name;
    send(fields);
}

void Core::send(const QJsonObject &m) {
    QByteArray line = QJsonDocument(m).toJson(QJsonDocument::Compact);
    line += '\n';
    QMutexLocker l(&out);
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
}

void Core::ops() {
    router.op("hello", [this](const QJsonObject &) {
        return QJsonObject{{"version", "v" + QStringLiteral(LEMURI_VERSION)},
                           {"protocol", PROTOCOL},
                           {"language", I18n::language()},
                           {"user", Files::localUser()},
                           {"vaultExists", vault.exists()},
                           {"vaultOpen", vault.isOpen()},
                           {"loadingNotice", hosts->loadingNotice.isNull() ? QJsonValue() : QJsonValue(hosts->loadingNotice)},
                           {"data", Vault::dir()},
                           {"languageChoice", Prefs("lemuri/ui").get("language", "auto")}};
    });
    // Every request this backend knows: the tests check them against PROTOCOL.md.
    router.op("protocol.ops", [this](const QJsonObject &) {
        QStringList l = router.names();
        l.sort();
        return QJsonArray::fromStringList(l);
    });
    // Every text of the interface, in the chosen language: the frontend formats {0}, {1}... itself.
    router.op("texts", [](const QJsonObject &) { return I18n::all(); });
    // The settings: the lemuri/ preference nodes, in the files shared with the first Lemuri.
    auto node = [](const QJsonObject &m) {
        QString n = Params::str(m, "node");
        static const QRegularExpression plain("^[a-z]+$");
        if (!plain.match(n).hasMatch()) throw Fail("Bad node: " + n);
        return Prefs("lemuri/" + n);
    };
    router.op("prefs.get", [node](const QJsonObject &m) {
        QJsonObject r;
        const auto all = node(m).all();
        for (auto it = all.begin(); it != all.end(); ++it) r[it.key()] = it.value();
        return r;
    });
    router.op("prefs.put", [node](const QJsonObject &m) {
        Prefs p = node(m);
        QHash<QString, QString> values;
        QStringList removed;
        const QJsonObject v = m["values"].toObject();
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (it.value().isNull()) removed << it.key();
            else values.insert(it.key(), it.value().isString() ? it.value().toString() : it.value().toVariant().toString());
        }
        p.change(values, removed);
        return QJsonValue();
    });
}
