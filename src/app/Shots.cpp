#include "app/Shots.h"

#include "app/Backend.h"
#include "app/Model.h"
#include "dialogs/Settings.h"
#include "dialogs/Team.h"
#include "terminal/TerminalView.h"
#include "widgets/Chrome.h"
#include "app/Texts.h"
#include "window/Actions.h"
#include "window/MainWindow.h"
#include "window/Splash.h"

#include <QApplication>
#include <QScreen>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QTime>
#include <QTimer>

namespace {
/** A node of LEMURI_SHOT_NODE, or of the "show" action. */
void show(MainWindow *window, Model *model, const QString &node) {
    if (node == "dc") model->select(Position::Datacenter);
    else if (node == "team") model->select(Position::Team);
    else if (node.startsWith("folder:")) model->select(Position::Folder, node.mid(7));
    else if (node.startsWith("host:")) model->select(Position::HostItem, node.mid(5));
    else if (node.startsWith("console:")) model->showOrOpen(node.mid(8));
    else if (node == "log") window->openLog();
}

/** The splash screen of the "splash" action, grabbed instead of the window. */
QPointer<QWidget> splash;

/**
 * "h4>h3,h6,sync": a tab on h4 split right with h3 ("v" instead of ">": split down), a tab on h6, the first tab in
 * front, synced if "sync" is there.
 */
void sessions(Model *model, const QString &spec) {
    Session *first = nullptr;
    bool sync = false;
    for (const QString &part : spec.split(',', Qt::SkipEmptyParts)) {
        if (part == "sync") {
            sync = true;
            continue;
        }
        QStringList hosts = part.split(QRegularExpression("[>v]"));
        Session *tab = model->showOrOpen(hosts[0]);
        if (!tab) continue;
        if (!first) first = tab;
        Session *last = tab;
        qsizetype at = hosts[0].size();
        for (int i = 1; i < hosts.size(); i++) {
            bool across = part[at] == '>';
            last = model->split(last, across, hosts[i]);
            at += 1 + hosts[i].size();
        }
    }
    if (!first) return;
    model->setSynced(first, sync);
    model->showOrOpen(first->hostId);
}

/** The right-click menu of the terminal of `hostId`, on the last place where `text` is on screen. */
void menuOn(Model *model, const QString &hostId, const QString &text) {
    Session *s = model->showOrOpen(hostId);
    if (!s || !s->view) return;
    QPoint p = s->view->whereIs(text);
    if (p.x() < 0) return;
    QPoint global = s->view->mapToGlobal(p);
    QApplication::postEvent(s->view, new QMouseEvent(QEvent::MouseMove, p, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier));
    QApplication::postEvent(s->view, new QContextMenuEvent(QContextMenuEvent::Mouse, p, global));
}

/** One action of LEMURI_SHOT_DO. */
void act(MainWindow *window, Model *model, Backend *backend, const QString &action) {
    QString a = action.section(':', 0, 0), arg = action.section(':', 1);
    Actions *actions = window->commands();
    if (a == "edit") actions->editHost(arg);
    else if (a == "new") actions->newHost(arg);
    else if (a == "folder") actions->newFolder(arg);
    else if (a == "delete") actions->deleteFolder(arg);
    else if (a == "unlock") actions->unlockVault();
    else if (a == "import") actions->sshImport(arg);
    else if (a == "remove") actions->removeHost(arg);
    else if (a == "settings") SettingsDialog::open(model, actions, window);
    else if (a == "export") BackupDialogs::exportAll(model, actions, window);
    else if (a == "sessions") sessions(model, arg);
    else if (a == "show") show(window, model, arg);
    else if (a == "check" || a == "type") {
        // In the dialog in front: tick the check box whose text starts with `arg`, or type `arg` in its lines.
        QWidget *front = QApplication::activeModalWidget();
        if (!front) return;
        if (a == "check") {
            for (QCheckBox *c : front->findChildren<QCheckBox *>())
                if (c->text().startsWith(arg)) c->setChecked(true);
        } else {
            for (QLineEdit *e : front->findChildren<QLineEdit *>()) e->setText(arg);
        }
    }
    else if (a == "menu") menuOn(model, arg.section(':', 0, 0), arg.section(':', 1));
    else if (a == "send") TeamDialogs::send(model, actions, window, arg);
    else if (a == "waitlink") TeamDialogs::waitForLink(model, window);
    else if (a == "save") {
        // As the Save of a host's page: the host saved with another note.
        if (const Host *h = model->host(arg)) {
            QJsonObject c = h->json;
            c["note"] = "saved at " + QTime::currentTime().toString();
            model->request("host.save", {{"host", c}});
        }
    }
    else if (a == "accept") {
        // The first host waiting in the team's inbox.
        backend->request("team.state", {}, [=](const QJsonValue &v, const QString &) {
            QJsonArray d = v.toObject()["deliveries"].toArray();
            if (!d.isEmpty()) TeamDialogs::accept(model, actions, window, d[0].toObject(), false);
        });
    } else if (a == "splash") {
        // Its last full frame: 2.4 s after it starts, just before it fades out (at 2.5 s).
        splash = new Splash([] {});
        QTimer::singleShot(2400, splash, [] {
            splash->grab().save(Shots::file());
            QApplication::exit(0);
        });
    } else if (a == "ghost") {
        if (model->sessions().isEmpty()) model->openLocal();
        QTimer::singleShot(800, window, [window] {
            // The picture is of the ghost window: the main one stays in place for the grab.
            QRect where = window->geometry();
            window->enterGhost();
            if (QWidget *ghost = window->ghostWindow()) ghost->setGeometry(where);
        });
    } else if (a == "dock") {
        // The ghost's anchor button for that side, as if clicked.
        if (QWidget *ghost = window->ghostWindow())
            for (IconButton *b : ghost->findChildren<IconButton *>())
                if (b->toolTip() == T(("ghost.dock." + arg).toLatin1().constData())) b->click();
    } else if (a == "split" || a == "exit") {
        // A local shell split right, then its right part split down; "exit" ends the last part afterwards.
        Session *s = model->openLocal();
        Session *right = model->split(s, true, Host::LOCAL);
        Session *below = model->split(right, false, Host::LOCAL);
        model->setSynced(s, arg == "sync");
        if (a == "split" && arg == "heir") QTimer::singleShot(1200, window, [model, s] { model->closePane(s); });
        if (a == "exit")
            QTimer::singleShot(1200, window, [model, backend, below] {
                if (model->sessions().contains(below)) backend->request("session.input", {{"session", below->backend}, {"data", "exit\r"}});
            });
    }
}

/** Shows the node and, a moment later, does the actions (separated by ";", 800 ms apart). */
void stage(MainWindow *window, Model *model, Backend *backend, const QString &node, const QString &actions) {
    show(window, model, node);
    QStringList list = actions.split(';', Qt::SkipEmptyParts);
    for (int i = 0; i < list.size(); i++)
        QTimer::singleShot(300 + 800 * i, window, [=] { act(window, model, backend, list[i]); });
}

/** The splash, else the ghost window if it is in view, else the menu over the window, else the dialog in front, else the window. */
void save(MainWindow *window, const QString &file) {
    if (splash && splash->isVisible()) {
        splash->grab().save(file);
        return;
    }
    if (QWidget *ghost = window->ghostWindow(); ghost && ghost->isVisible()) {
        ghost->grab().save(file);
        return;
    }
    if (QWidget *popup = QApplication::activePopupWidget()) {
        // A menu is a window of its own: drawn onto the picture where it is over the window.
        QImage whole = window->grab().toImage();
        QPainter g(&whole);
        g.drawPixmap(window->mapFromGlobal(popup->mapToGlobal(QPoint(0, 0))), popup->grab());
        g.end();
        whole.save(file);
        return;
    }
    QWidget *modal = QApplication::activeModalWidget();
    QWidget *shown = modal ? modal : window;
    // LEMURI_SHOT_SCREEN: what the window system shows on a real screen, not what Qt paints (size, clipping).
    if (qEnvironmentVariableIsSet("LEMURI_SHOT_SCREEN") && shown->screen()) shown->screen()->grabWindow(shown->winId()).save(file);
    else shown->grab().save(file);
}
}  // namespace

QString Shots::file() {
    return qEnvironmentVariable("LEMURI_SHOT");
}

void Shots::take(MainWindow *window, Model *model, Backend *backend, const QString &file) {
    int ms = qEnvironmentVariableIntValue("LEMURI_SHOT_MS") ? qEnvironmentVariableIntValue("LEMURI_SHOT_MS") : 5000;
    // LEMURI_SHOT_SIZE=1830x1024: the inside of the window, without its shadow.
    QStringList size = qEnvironmentVariable("LEMURI_SHOT_SIZE").split('x');
    if (size.size() == 2)
        window->resize(window->size() - window->contentsRect().size() + QSize(size[0].toInt(), size[1].toInt()));
    QPointer<MainWindow> w = window;
    QTimer::singleShot(ms / 2, window, [w, model, backend] {
        QString node = qEnvironmentVariable("LEMURI_SHOT_NODE"), action = qEnvironmentVariable("LEMURI_SHOT_DO");
        if (w && (!node.isEmpty() || !action.isEmpty())) stage(w, model, backend, node, action);
    });
    QTimer::singleShot(ms, window, [w, file] {
        if (w) save(w, file);
        QApplication::exit(0);
    });
}
