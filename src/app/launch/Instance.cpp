#include "app/launch/Instance.h"

#include <QDir>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRect>
#include <QScreen>
#include <QStandardPaths>
#include <unistd.h>
#include <QWidget>

#if defined(LEMURI_XCB)
#include <xcb/xcb.h>
#endif

namespace {
QString socketPath() {
    // $XDG_RUNTIME_DIR, or Qt's own /tmp/runtime-<user> (0700). Only if both fail the shared /tmp: then with the
    // user's id in the name, so that another user cannot take or answer this user's socket by chance.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) return QDir::tempPath() + QString("/lemuri-%1.sock").arg(::getuid());
    return dir + "/lemuri.sock";
}
}  // namespace

bool Instance::handOver(const QString &command) {
    QLocalSocket s;
    s.connectToServer(socketPath());
    if (!s.waitForConnected(500)) return false;
    s.write(command.toUtf8() + '\n');
    s.waitForBytesWritten(500);
    // Read until it closes: the running copy has taken it.
    s.waitForReadyRead(1000);
    return true;
}

void Instance::listen(QObject *parent, std::function<void(const QString &)> on) {
    auto *server = new QLocalServer(parent);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    // A socket left by a copy that crashed: nobody answered on it (see handOver), so it can go.
    if (!server->listen(socketPath())) {
        QLocalServer::removeServer(socketPath());
        server->listen(socketPath());
    }
    QObject::connect(server, &QLocalServer::newConnection, server, [server, on] {
        while (QLocalSocket *c = server->nextPendingConnection()) {
            QObject::connect(c, &QLocalSocket::readyRead, c, [c, on] {
                if (!c->canReadLine()) return;
                QString command = QString::fromUtf8(c->readLine()).trimmed();
                c->disconnectFromServer();
                on(command);
            });
            QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
        }
    });
}

void Instance::activate(QWidget *w) {
    w->raise();
    w->activateWindow();
#if defined(LEMURI_XCB)
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    xcb_connection_t *c = x11 ? x11->connection() : nullptr;
    if (!c) return;
    static const char name[] = "_NET_ACTIVE_WINDOW";
    xcb_intern_atom_reply_t *atom = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, sizeof name - 1, name), nullptr);
    if (!atom) return;
    xcb_client_message_event_t e{};
    e.response_type = XCB_CLIENT_MESSAGE;
    e.format = 32;
    e.window = xcb_window_t(w->winId());
    e.type = atom->atom;
    e.data.data32[0] = 2;  // from a pager: the user asked for it
    e.data.data32[1] = XCB_CURRENT_TIME;
    free(atom);
    xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
    xcb_send_event(c, 0, root, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&e));
    xcb_flush(c);
#endif
}

#if defined(LEMURI_XCB)
namespace {
/** A property of the root window made of 32-bit numbers; empty if it is not there. */
QList<quint32> rootNumbers(xcb_connection_t *c, const QByteArray &name) {
    QList<quint32> out;
    xcb_intern_atom_reply_t *atom = xcb_intern_atom_reply(c, xcb_intern_atom(c, 1, name.size(), name.constData()), nullptr);
    if (!atom) return out;
    xcb_atom_t a = atom->atom;
    free(atom);
    if (a == XCB_ATOM_NONE) return out;
    xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
    xcb_get_property_reply_t *r = xcb_get_property_reply(c, xcb_get_property(c, 0, root, a, XCB_ATOM_CARDINAL, 0, 1024), nullptr);
    if (!r) return out;
    if (r->format == 32) {
        auto *v = static_cast<const quint32 *>(xcb_get_property_value(r));
        out = QList<quint32>(v, v + xcb_get_property_value_length(r) / 4);
    }
    free(r);
    return out;
}
}  // namespace
#endif

QRect Instance::workArea(QScreen *s) {
    QRect area = s->availableGeometry();
#if defined(LEMURI_XCB)
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    xcb_connection_t *c = x11 ? x11->connection() : nullptr;
    if (!c || area != s->geometry()) return area;
    quint32 desktop = rootNumbers(c, "_NET_CURRENT_DESKTOP").value(0);
    QList<quint32> rects = rootNumbers(c, "_GTK_WORKAREAS_D" + QByteArray::number(desktop));
    if (rects.isEmpty()) rects = rootNumbers(c, "_NET_WORKAREA").mid(desktop * 4, 4);
    // X11 counts device pixels; Qt scales them around the screen's corner.
    double dpr = s->devicePixelRatio();
    QPoint origin = s->geometry().topLeft();
    QRect native(origin, s->geometry().size() * dpr);
    QRect best;
    for (int i = 0; i + 3 < rects.size(); i += 4) {
        QRect r = QRect(int(rects[i]), int(rects[i + 1]), int(rects[i + 2]), int(rects[i + 3])).intersected(native);
        if (qint64(r.width()) * r.height() > qint64(best.width()) * best.height()) best = r;
    }
    // Less than half the screen: not believable, the whole screen is better.
    if (qint64(best.width()) * best.height() * 2 < qint64(native.width()) * native.height()) return area;
    return QRect(origin + (best.topLeft() - origin) / dpr, best.size() / dpr);
#else
    return area;
#endif
}
