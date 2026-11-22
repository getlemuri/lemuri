#include "app/Backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>

Backend::Backend(QObject *parent) : QObject(parent) {
    connect(&process, &QProcess::readyReadStandardOutput, this, &Backend::read);
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        dropReplies(process.errorString());
        emit failed(process.errorString());
    });
    connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        QString why = status == QProcess::CrashExit ? QString("crashed") : QString("exit code %1").arg(code);
        dropReplies(why);
        if (status == QProcess::CrashExit || code != 0) emit failed(why);
    });
    // What the backend logs goes to our standard error, as is.
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
}

Backend::~Backend() {
    // The app is quitting: whoever waited for an answer is going too, so nobody is called back.
    replies.clear();
    disconnect(&process, nullptr, this, nullptr);
    // Closing its input tells the backend to close every connection and quit.
    process.closeWriteChannel();
    if (!process.waitForFinished(2000)) process.kill();
}

void Backend::start() {
    // LEMURI_CORE, or lemuri-core beside the app (the build folder, bin/ of the .deb in /opt, MacOS/ of the Mac app),
    // or in libexec/lemuri as a distribution installs it (/usr/bin/lemuri, /usr/libexec/lemuri/lemuri-core).
    QDir here(QCoreApplication::applicationDirPath());
    QString core = here.filePath("lemuri-core");
#ifdef LEMURI_CORE_FROM_BIN
    if (!QFileInfo::exists(core)) core = QDir::cleanPath(here.filePath(LEMURI_CORE_FROM_BIN "/lemuri-core"));
#endif
    process.start(qEnvironmentVariable("LEMURI_CORE", core), {});
}

void Backend::dropReplies(const QString &why) {
    QHash<int, Reply> all;
    all.swap(replies);
    for (const Reply &r : all) r(QJsonValue(), why);
}

void Backend::request(const QString &op, QJsonObject params, Reply reply) {
    if (process.state() == QProcess::NotRunning && reply) {
        // Not after it has gone: the answer would never come.
        QJsonValue none;
        QMetaObject::invokeMethod(this, [reply, none] { reply(none, "The backend is not running"); }, Qt::QueuedConnection);
        return;
    }
    params["op"] = op;
    if (reply) {
        int id = ++next;
        params["id"] = id;
        replies.insert(id, std::move(reply));
    }
    process.write(QJsonDocument(params).toJson(QJsonDocument::Compact) + '\n');
}

void Backend::answer(int ask, const QJsonValue &value) {
    request("answer", {{"ask", ask}, {"value", value}});
}

void Backend::read() {
    pending += process.readAllStandardOutput();
    for (qsizetype nl; (nl = pending.indexOf('\n')) >= 0; ) {
        QByteArray line = pending.left(nl);
        pending.remove(0, nl + 1);
        QJsonObject m = QJsonDocument::fromJson(line).object();
        // A reply has an id and no "event": the events of the activity log carry an id of their own (the line's).
        if (m.contains("id") && !m.contains("event")) {
            Reply r = replies.take(m["id"].toInt());
            if (r) r(m["ok"], m["error"].toString());
            continue;
        }
        QString event = m["event"].toString();
        QString session = m["session"].toString();
        if (event == "data") emit data(session, m["data"].toString());
        else if (event == "state") emit state(session, m["state"].toString(), m["message"].toString());
        else if (event == "ask") emit ask(m["ask"].toInt(), m["kind"].toString(), m);
        else emit this->event(event, m);
    }
}
