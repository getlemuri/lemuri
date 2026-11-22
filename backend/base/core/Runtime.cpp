#include "base/core/Runtime.h"

#include "base/core/Error.h"
#include "base/core/Params.h"

#include <QDateTime>
#include <QJsonArray>
#include <QMetaObject>
#include <cstdio>

void reportErrors(const std::function<void()> &f) {
    try {
        f();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", qPrintable(errorText(e)));
    }
}

// ── Reply ──

struct Reply::State {
    Send send;
    QJsonValue id;
    QString op;
    std::atomic<bool> answered{false};

    /** Only the first answer goes out. */
    void give(const QJsonObject &m) {
        if (answered.exchange(true) || id.isUndefined() || id.isNull()) return;
        send(m);
    }

    ~State() {
        if (!answered && !id.isUndefined() && !id.isNull())
            send({{"id", id}, {"error", "No answer from the backend to " + op}});
    }
};

Reply::Reply(Send send, QJsonValue id, QString op) : state(std::make_shared<State>()) {
    state->send = std::move(send);
    state->id = std::move(id);
    state->op = std::move(op);
}

void Reply::ok(const QJsonValue &v) const {
    state->give({{"id", state->id}, {"ok", v}});
}

void Reply::fail(const QString &why) const {
    state->give({{"id", state->id}, {"error", why}});
}

// ── Router ──

void Router::op(const QString &name, Op f) {
    later(name, [f = std::move(f)](const QJsonObject &m, const Reply &r) { r.ok(f(m)); });
}

void Router::later(const QString &name, LaterOp f) {
    if (ops.contains(name)) qFatal("Op registered twice: %s", qPrintable(name));
    ops.insert(name, std::move(f));
}

void Router::handle(const QJsonObject &m) const {
    QString name = m["op"].toString();
    Reply r(send, m["id"], name);
    auto it = ops.constFind(name);
    if (it == ops.constEnd()) {
        r.fail("Unknown op: " + name);
        return;
    }
    try {
        (*it)(m, r);
    } catch (const Fail &f) {
        r.fail(f.why);
    } catch (const std::exception &e) {
        // Not a refusal meant for the user: a failure, which the log keeps too.
        std::fprintf(stderr, "%s: %s\n", qPrintable(name), e.what());
        r.fail(QString::fromUtf8(e.what()));
    }
}

// ── Prompter ──

struct Prompter::Question {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    QJsonValue value;

    void give(const QJsonValue &v) {
        {
            std::lock_guard<std::mutex> l(m);
            if (done) return;
            done = true;
            value = v;
        }
        cv.notify_all();
    }
};

QJsonValue Prompter::ask(const QString &kind, QJsonObject fields) {
    auto q = std::make_shared<Question>();
    int n;
    {
        std::lock_guard<std::mutex> l(m);
        if (closed) return QJsonValue::Null;
        n = ++next;
        waiting.insert(n, q);
    }
    fields["ask"] = n;
    fields["kind"] = kind;
    events.event("ask", fields);
    std::unique_lock<std::mutex> l(q->m);
    if (!q->cv.wait_for(l, std::chrono::minutes(TIMEOUT_MINUTES), [&] { return q->done; })) {
        l.unlock();
        std::lock_guard<std::mutex> g(m);
        waiting.remove(n);
        return QJsonValue::Null;
    }
    return q->value;
}

void Prompter::answer(const QJsonObject &a) {
    std::shared_ptr<Question> q;
    {
        std::lock_guard<std::mutex> l(m);
        q = waiting.take(a["ask"].toInt());
    }
    if (q) q->give(a.contains("value") ? a["value"] : QJsonValue::Null);
}

void Prompter::close() {
    QHash<int, std::shared_ptr<Question>> all;
    {
        std::lock_guard<std::mutex> l(m);
        closed = true;
        all.swap(waiting);
    }
    for (auto &q : all) q->give(QJsonValue::Null);
}

// ── Activity ──

namespace {
const char *name(Activity::Outcome o) {
    switch (o) {
    case Activity::Outcome::Running: return "RUNNING";
    case Activity::Outcome::Ok: return "OK";
    case Activity::Outcome::Error: return "ERROR";
    }
    return "ERROR";
}
}  // namespace

int Activity::task(const QString &target, const QString &text) {
    int id = ++next;
    events.event("task", {{"id", id}, {"target", target}, {"text", text}, {"outcome", name(Outcome::Running)},
                          {"when", QDateTime::currentMSecsSinceEpoch()}});
    return id;
}

void Activity::finish(int task, Outcome outcome, const QString &detail) {
    events.event("task", {{"id", task}, {"outcome", name(outcome)}, {"detail", detail.isNull() ? QJsonValue() : QJsonValue(detail)},
                          {"ended", QDateTime::currentMSecsSinceEpoch()}});
}

void Activity::done(const QString &target, const QString &text, Outcome outcome, const QString &detail) {
    finish(task(target, text), outcome, detail);
}

// ── Threads ──

ThreadGroup::~ThreadGroup() {
    join();
    // Left only if the group goes away on one of its own threads, which then ends right after: a joinable
    // std::thread may not be destroyed, and it cannot join itself.
    std::lock_guard<std::mutex> l(m);
    for (Item &i : items)
        if (i.thread.joinable()) i.thread.detach();
}

void ThreadGroup::start(std::function<void()> f) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread t([f = std::move(f), done] {
        reportErrors(f);
        *done = true;
    });
    std::lock_guard<std::mutex> l(m);
    reap();
    items.push_back({std::move(t), done});
}

void ThreadGroup::adopt(std::thread t) {
    std::lock_guard<std::mutex> l(m);
    items.push_back({std::move(t), nullptr});
}

/** With the lock held: the threads already finished are joined (at once) and dropped. */
void ThreadGroup::reap() {
    for (auto it = items.begin(); it != items.end();) {
        if (it->done && *it->done && it->thread.joinable()) {
            it->thread.join();
            it = items.erase(it);
        } else {
            ++it;
        }
    }
}

void ThreadGroup::join() {
    // Threads may start others while being joined: until none is left but, maybe, the calling one.
    for (;;) {
        std::vector<Item> all;
        {
            std::lock_guard<std::mutex> l(m);
            for (auto it = items.begin(); it != items.end();) {
                if (it->thread.get_id() == std::this_thread::get_id()) {
                    ++it;
                } else {
                    all.push_back(std::move(*it));
                    it = items.erase(it);
                }
            }
        }
        if (all.empty()) return;
        for (Item &i : all)
            if (i.thread.joinable()) i.thread.join();
    }
}

WorkerPool::~WorkerPool() {
    stop();
}

void WorkerPool::run(std::function<void()> f) {
    std::lock_guard<std::mutex> l(m);
    if (stopping) return;
    queue.push_back(std::move(f));
    // A new thread only when none is free: work often waits for long (a connection, the user), never queued behind.
    if (qsizetype(queue.size()) <= idle || threads.size() >= size_t(MAX_THREADS)) wake.notify_one();
    else threads.emplace_back([this] { serve(); });
}

void WorkerPool::serve() {
    std::unique_lock<std::mutex> l(m);
    for (;;) {
        idle++;
        wake.wait(l, [this] { return stopping || !queue.empty(); });
        idle--;
        if (stopping) return;
        std::function<void()> f = std::move(queue.front());
        queue.pop_front();
        l.unlock();
        reportErrors(f);
        l.lock();
    }
}

void WorkerPool::stop() {
    std::vector<std::thread> all;
    {
        std::lock_guard<std::mutex> l(m);
        stopping = true;
        queue.clear();
        all.swap(threads);
    }
    wake.notify_all();
    for (std::thread &t : all)
        if (t.joinable()) t.join();
}

void Loop::post(std::function<void()> f) {
    QMetaObject::invokeMethod(this, [f = std::move(f)] { reportErrors(f); }, Qt::QueuedConnection);
}

void Loop::work(std::function<void()> f) {
    pool.run(std::move(f));
}

void Loop::onStop(std::function<void()> f) {
    stopping.push_back(std::move(f));
}

void Loop::stop() {
    for (auto it = stopping.rbegin(); it != stopping.rend(); ++it) reportErrors(*it);
    stopping.clear();
    pool.stop();
}

// ── Params ──

QString Params::str(const QJsonObject &m, const QString &k) {
    QJsonValue v = m[k];
    if (v.isUndefined() || v.isNull()) return QString();
    return v.isString() ? v.toString() : v.toVariant().toString();
}

QString Params::need(const QJsonObject &m, const QString &k) {
    QString s = str(m, k);
    if (s.isNull()) throw Fail("Missing " + k);
    return s;
}

bool Params::flag(const QJsonObject &m, const QString &k) {
    return m[k].toBool();
}

int Params::integer(const QJsonObject &m, const QString &k, int otherwise) {
    QJsonValue v = m[k];
    return v.isDouble() ? v.toInt() : v.isString() ? v.toString().toInt() : otherwise;
}

QStringList Params::strings(const QJsonObject &m, const QString &k) {
    QStringList l;
    for (const QJsonValue &v : m[k].toArray()) l << v.toString();
    return l;
}
