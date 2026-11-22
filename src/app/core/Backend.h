#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <functional>

/**
 * The backend (lemuri-core, beside the app) as a child process: requests go to its standard input, one JSON object per
 * line, and its answers and events come back the same way (see backend/PROTOCOL.md).
 */
class Backend : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonValue &ok, const QString &error)>;
    /** The version of the protocol this app speaks: the backend says its own in "hello". */
    static constexpr int PROTOCOL = 1;

    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    void start();
    /** A request; `reply`, if given, is called with the answer on the GUI thread. */
    void request(const QString &op, QJsonObject params = {}, Reply reply = nullptr);
    void answer(int ask, const QJsonValue &value);

signals:
    void data(const QString &session, const QString &text);
    void state(const QString &session, const QString &state, const QString &message);
    /** A question for the user (see backend/PROTOCOL.md, "Questions"): answer it with answer. */
    void ask(int ask, const QString &kind, const QJsonObject &m);
    /** Every other event, with all its fields (hosts, vault, metrics, task, transfer...). */
    void event(const QString &name, const QJsonObject &m);
    /** The backend could not start, or has stopped: `why` in English, from the system. */
    void failed(const QString &why);

private:
    void read();
    /** The backend is gone: every request still waiting gets an error, none is left hanging. */
    void dropReplies(const QString &why);

    QProcess process;
    QByteArray pending;
    QHash<int, Reply> replies;
    int next = 0;
};
