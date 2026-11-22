#pragma once

#include <QString>
#include <functional>
#include <memory>

class ThreadGroup;

/**
 * A terminal's connection: an SSH shell or the local one. It runs on threads of the group given to launch(), which
 * its owner joins after closing it; what the shell prints comes out through onData, the state (an Error with why)
 * through onState, both from those threads. send, resize and close may be called
 * from any thread. Always held by a shared_ptr: the threads keep it alive until they end.
 */
class Transport : public std::enable_shared_from_this<Transport> {
public:
    enum class State { Connecting, Connected, Closed, Error };
    /** As the protocol's "state" events say it. */
    static QString name(State s);

    virtual ~Transport() = default;
    std::function<void(const QString &)> onData = [](const QString &) {};
    std::function<void(State state, const QString &message)> onState = [](State, const QString &) {};
    virtual void launch(ThreadGroup &threads, int columns, int lines) = 0;
    virtual void send(const QByteArray &bytes) = 0;
    virtual void resize(int columns, int lines) = 0;
    virtual void close() = 0;
    virtual bool isLocal() const { return false; }
};

inline QString Transport::name(State s) {
    switch (s) {
    case State::Connecting: return "CONNECTING";
    case State::Connected: return "CONNECTED";
    case State::Closed: return "CLOSED";
    case State::Error: return "ERROR";
    }
    return "ERROR";
}
