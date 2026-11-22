#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

class Events;
class Loop;
class Prompter;
class Sessions;
class Router;
class Reply;
class Sftp;

/**
 * File upload and download over SFTP on a session's connection: at most PER_HOST at
 * once per host, a second transfer to the same place refused until the first ends. The questions (what to upload,
 * where to save) are the frontend's; once confirmed, a transfer sends "transfer" events with its progress, at most
 * ten per second, and one at its end. Also the double click's check of a name in the terminal (fs.check).
 */
class Transfers {
public:
    static constexpr int PER_HOST = 4;
    enum class Direction { Up, Down };
    enum class Outcome { Done, Cancelled, Failed };
    Transfers(Events &events, Loop &loop, Prompter &prompter, Sessions &sessions);
    /** Registers its requests. */
    void ops(Router &router);

    struct Transfer;

private:
    using Work = std::function<QString(Transfer &, Sftp &)>;
    /** Runs on a worker thread with an SFTP channel of the session; the answer is the request's. */
    void sftp(const QJsonObject &m, const Reply &r, std::function<QJsonValue(Sftp &)> work);
    void existing(const QJsonObject &m, const Reply &r);
    void locate(const QJsonObject &m, const Reply &r);
    void check(const QJsonObject &m, const Reply &r);
    QJsonObject upload(const QJsonObject &m);
    QJsonObject download(const QJsonObject &m);
    void cancel(int id);
    QJsonObject start(const QString &sid, std::shared_ptr<Transfer> t, Work w);
    bool confirm(const QString &title, const QString &text, const QString &action);
    void slot(const QString &host, bool take, const std::function<bool()> &cancelled = {});

    Events &events;
    Loop &loop;
    Prompter &prompter;
    Sessions &sessions;
    std::mutex guard;
    std::condition_variable freed;
    QHash<QString, int> running;  // host -> transfers running
    QSet<QString> busy;           // where running transfers write: "host:path" or "local:path"
    QHash<int, std::shared_ptr<Transfer>> active;
    std::atomic<int> next{0};
};
