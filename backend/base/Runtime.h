#pragma once

// The pieces every module of the backend is built on: where events go, how requests reach their handler, questions
// to the user, the activity log and the threads. Each module is given, in its constructor, only those it uses.

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

/** Where events go: {"event": name, ...fields}. From any thread. */
class Events {
public:
    virtual ~Events() = default;
    virtual void event(const QString &name, QJsonObject fields = {}) = 0;
};

/**
 * The answer to one request, to give now or later, from any thread. Copies share it; the first ok() or fail() is
 * the answer and the others are ignored. If the last copy goes without either, the request fails: a request is
 * never left without an answer.
 */
class Reply {
public:
    using Send = std::function<void(const QJsonObject &)>;
    Reply(Send send, QJsonValue id, QString op);
    void ok(const QJsonValue &v = QJsonValue::Null) const;
    void fail(const QString &why) const;

private:
    struct State;
    std::shared_ptr<State> state;
};

/** The requests' handlers, by name. Used on the main thread. */
class Router {
public:
    /** A request answered at once: returns the answer, or throws. */
    using Op = std::function<QJsonValue(const QJsonObject &m)>;
    /** A request answered later: keeps the Reply and answers when done. May still throw at once. */
    using LaterOp = std::function<void(const QJsonObject &m, const Reply &r)>;

    explicit Router(Reply::Send send) : send(std::move(send)) {}
    void op(const QString &name, Op f);
    void later(const QString &name, LaterOp f);
    /** Runs the request's handler; an unknown op or a handler that throws is an error reply. */
    void handle(const QJsonObject &m) const;
    QStringList names() const { return ops.keys(); }

private:
    Reply::Send send;
    QHash<QString, LaterOp> ops;
};

/**
 * Questions to the user: the frontend shows a dialog and sends back {"op": "answer", "ask": n, "value": ...}. Asked
 * only from worker threads: the main thread keeps serving the requests the frontend sends meanwhile.
 */
class Prompter {
public:
    explicit Prompter(Events &events) : events(events) {}
    /** No question waits longer: then the answer is null, as if the dialog had been closed. */
    static constexpr int TIMEOUT_MINUTES = 30;
    /** Waits for the answer; null if the user closed the dialog, after TIMEOUT_MINUTES, or once the frontend has gone. */
    QJsonValue ask(const QString &kind, QJsonObject fields = {});
    void answer(const QJsonObject &m);
    /** The frontend has gone: every waiting question gets null, and new ones get it at once. */
    void close();

private:
    struct Question;
    Events &events;
    std::mutex m;
    QHash<int, std::shared_ptr<Question>> waiting;
    int next = 0;
    bool closed = false;
};

/** The activity log of the frontend: lines with a target, a text and an outcome. */
class Activity {
public:
    enum class Outcome { Running, Ok, Error };

    explicit Activity(Events &events) : events(events) {}
    /** A line still running; ended by finish(). */
    int task(const QString &target, const QString &text);
    void finish(int task, Outcome outcome, const QString &detail = {});
    /** A line already ended. */
    void done(const QString &target, const QString &text, Outcome outcome = Outcome::Ok, const QString &detail = {});

private:
    Events &events;
    std::atomic<int> next{0};
};

/**
 * Threads that belong to an object: started with start(), all waited for by join() (and by the destructor). Those
 * already finished are joined as new ones start, so a long-lived group does not pile them up. Whoever owns the group
 * must make its threads end (close their sockets, wake their waits) before joining.
 */
class ThreadGroup {
public:
    ThreadGroup() = default;
    ~ThreadGroup();
    ThreadGroup(const ThreadGroup &) = delete;
    ThreadGroup &operator=(const ThreadGroup &) = delete;
    void start(std::function<void()> f);
    /** A thread made elsewhere, joined with the others. */
    void adopt(std::thread t);
    /** Waits for every thread but the calling one (a thread of the group may stop it). */
    void join();

private:
    struct Item {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    void reap();
    std::mutex m;
    std::vector<Item> items;
};

/**
 * Threads that run work which waits (a connection, a question to the user), reused once idle, at most MAX_THREADS:
 * past that, work waits in line for a free one. They belong to the pool: stop() waits for each to finish what it is
 * doing; work not started by then is dropped.
 */
class WorkerPool {
public:
    static constexpr int MAX_THREADS = 64;
    WorkerPool() = default;
    ~WorkerPool();
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool &operator=(const WorkerPool &) = delete;
    void run(std::function<void()> f);
    void stop();

private:
    void serve();
    std::mutex m;
    std::condition_variable wake;
    std::deque<std::function<void()>> queue;
    std::vector<std::thread> threads;
    int idle = 0;
    bool stopping = false;
};

/**
 * The main thread, where requests and the modules' state live, one thing at a time: post() brings work back to it
 * from any thread. work() runs on the pool; onStop() is called when the frontend has gone, before the threads end.
 */
class Loop : public QObject {
    Q_OBJECT
public:
    void post(std::function<void()> f);
    void work(std::function<void()> f);
    void onStop(std::function<void()> f);
    /** The stop hooks in reverse order of registration, then the pool. */
    void stop();

private:
    WorkerPool pool;
    std::vector<std::function<void()>> stopping;
};

/** Writes `f`'s exception, if any, to standard error: for work that has no one to answer to. */
void reportErrors(const std::function<void()> &f);
