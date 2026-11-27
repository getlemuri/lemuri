#include "app/tools/Shots.h"

#include "app/core/Backend.h"
#include "app/core/Model.h"
#include "dialogs/Settings.h"
#include "dialogs/Team.h"
#include "terminal/TerminalView.h"
#include "widgets/parts/Chrome.h"
#include "widgets/look/Backdrop.h"
#include "widgets/look/Theme.h"
#include "app/tools/Texts.h"
#include "window/main/Actions.h"
#include "window/main/MainWindow.h"
#include "window/main/Splash.h"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QRandomGenerator>
#include <cstdio>
#include <QScreen>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QMenu>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
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

}  // namespace

/** One action of LEMURI_SHOT_DO. */
void Shots::act(MainWindow *window, Model *model, Backend *backend, const QString &action) {
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
    else if (a == "group") model->openSplit(arg.split('+'));
    else if (a == "groups") {
        // Two made-up launch groups, for the menu of the recent servers.
        model->setLaunchGroups({{"Deploy web", {"h4", "h5", "h3"}}, {"Fleet EU", {"h3", "h4", "h5", "h6"}}}, false);
    }
    else if (a == "launch") {
        // The dialog of a new launch group, with two servers ticked as if chosen.
        model->setLaunchGroups({{"Deploy", {"h4", "h5", "h3"}}}, false);
        QTimer::singleShot(0, window, [actions] { actions->editLaunch(0); });
    }
    else if (a == "recent") {
        // The menu of Ctrl+Shift+O, open over the window.
        QMenu *menu = actions->recentMenu();
        menu->adjustSize();
        menu->popup(window->mapToGlobal(QPoint((window->width() - menu->width()) / 2, window->height() / 4)));
    }
    else if (a == "files") {
        // The file view in place of the first server terminal.
        for (Session *s : model->sessions())
            if (s->hostId != Host::LOCAL) {
                model->setShowFiles(s, true);
                break;
            }
    }
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
    } else if (a == "tree") {
        // The ghost's Hosts button, as if clicked: the tree starts closed.
        if (QWidget *ghost = window->ghostWindow())
            for (IconButton *b : ghost->findChildren<IconButton *>())
                if (b->toolTip() == T("activity.hosts")) b->click();
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

namespace {
/** Shows the node and, a moment later, does the actions (separated by ";", 800 ms apart). */
void stage(MainWindow *window, Model *model, Backend *backend, const QString &node, const QString &actions) {
    show(window, model, node);
    QStringList list = actions.split(';', Qt::SkipEmptyParts);
    for (int i = 0; i < list.size(); i++)
        QTimer::singleShot(300 + 800 * i, window, [=] { Shots::act(window, model, backend, list[i]); });
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

/**
 * A film of the app, for the website (LEMURI_FILM=script): the script's actions at their times, and a frame every
 * 40 ms written raw (BGRA, LEMURI_FILM_SIZE) to the standard output, for ffmpeg. Frames are counted from the clock:
 * one that comes late is repeated, so the film keeps the time of the script.
 */
class Film : public QObject {
public:
    Film(MainWindow *window, Model *model, Backend *backend) : window(window), model(model), backend(backend) {
        QStringList size = qEnvironmentVariable("LEMURI_FILM_SIZE", "1920x1080").split('x');
        canvas = QSize(size[0].toInt(), size[1].toInt());
        desktop = QImage(qEnvironmentVariable("LEMURI_FILM_DESKTOP")).scaled(canvas, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QFile f(qEnvironmentVariable("LEMURI_FILM"));
        if (f.open(QIODevice::ReadOnly))
            for (const QString &line : QString::fromUtf8(f.readAll()).split('\n')) {
                QString l = line.trimmed();
                if (l.isEmpty() || l.startsWith('#')) continue;
                double at = l.section(' ', 0, 0).toDouble();
                QString action = l.section(' ', 1).trimmed();
                if (action == "end") end = qint64(at * 1000);
                else QTimer::singleShot(int(at * 1000), this, [this, action] { act(action); });
            }
        frames.setTimerType(Qt::PreciseTimer);
        connect(&frames, &QTimer::timeout, this, &Film::frame);
        frames.start(FRAME_MS / 2);
        clock.start();
    }

private:
    static constexpr int FRAME_MS = 40;

    void act(const QString &action) {
        QString a = action.section(':', 0, 0), arg = action.section(':', 1);
        if (a == "caption") {
            previous = caption;
            previousAt = clock.elapsed();
            caption = arg;
            captionAt = clock.elapsed();
        } else if (a == "fade") {
            fadeOut = arg == "out";
            fadeAt = clock.elapsed();
        } else if (a == "key") {
            type(arg);
        } else if (a == "close") {
            if (QWidget *popup = QApplication::activePopupWidget()) popup->close();
            else if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget())) d->reject();
        } else if (a == "transfer") {
            backend->request("demo.transfer");
        } else if (a == "splash") {
            splash = new Splash([] {});
        } else if (a == "sync") {
            if (Session *s = model->showOrOpen(arg)) model->setSynced(s->tab(), true);
        } else if (a == "title") {
            title = arg;
            titleAt = clock.elapsed();
        } else if (a == "unghost") {
            window->leaveGhost();
        } else {
            Shots::act(window, model, backend, action);
        }
    }

    /** Keys typed into the terminal in front, a person's pace; "\n" is Enter. */
    void type(const QString &text) {
        TerminalView *view = qobject_cast<TerminalView *>(QApplication::focusWidget());
        for (QWidget *top : QApplication::topLevelWidgets()) {
            if (view || !top->isVisible()) continue;
            for (TerminalView *t : top->findChildren<TerminalView *>())
                if (t->isVisible()) {
                    view = t;
                    break;
                }
        }
        if (!view) return;
        QPointer<TerminalView> to = view;
        QString keys = QString(text).replace("\\n", "\n");
        int at = 0;
        for (QChar c : keys) {
            at += c == '\n' ? 260 : 55 + int(QRandomGenerator::global()->bounded(45));
            QTimer::singleShot(at, this, [to, c] {
                if (!to) return;
                int key = c == '\n' ? int(Qt::Key_Return) : int(c.toUpper().unicode());
                QString t = c == '\n' ? QString("\r") : QString(c);
                QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, t);
                QApplication::sendEvent(to, &press);
                QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, t);
                QApplication::sendEvent(to, &release);
            });
        }
    }

    void frame() {
        qint64 now = clock.elapsed();
        if (end && now >= end) {
            frames.stop();
            std::fflush(stdout);
            QApplication::exit(0);
            return;
        }
        qint64 due = now / FRAME_MS + 1;
        if (due <= written) return;
        QImage img = compose(now);
        while (written < due) {
            std::fwrite(img.constBits(), 1, size_t(img.sizeInBytes()), stdout);
            written++;
        }
    }

    /** What a screen would show: the window with its dialog or menu, or the desktop with the splash or the ghost. */
    QImage compose(qint64 now) {
        QImage img(canvas, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);
        QWidget *ghost = window->ghostWindow();
        double scale = window->devicePixelRatioF();
        p.scale(scale, scale);
        if ((splash && splash->isVisible()) || (ghost && ghost->isVisible())) {
            p.save();
            p.resetTransform();
            if (desktop.isNull()) p.fillRect(img.rect(), Qt::black);
            else p.drawImage(0, 0, desktop);
            p.restore();
            QWidget *top = splash && splash->isVisible() ? splash.data() : ghost;
            p.drawPixmap(top->geometry().topLeft(), top->grab());
        } else {
            QPoint origin = window->mapToGlobal(window->contentsRect().topLeft());
            p.fillRect(QRectF(QPointF(0, 0), QSizeF(canvas) / scale), Qt::black);
            p.drawPixmap(-window->contentsRect().topLeft(), window->grab());
            if (QWidget *modal = QApplication::activeModalWidget(); modal && modal != window)
                p.drawPixmap(modal->mapToGlobal(QPoint(0, 0)) - origin, modal->grab());
            if (QWidget *popup = QApplication::activePopupWidget()) p.drawPixmap(popup->mapToGlobal(QPoint(0, 0)) - origin, popup->grab());
        }
        p.resetTransform();
        drawCaption(p, previous, 1.0 - std::min(1.0, (now - previousAt) / 300.0));
        drawCaption(p, caption, std::min(1.0, (now - captionAt) / 300.0));
        double dark = std::clamp((now - fadeAt) / 600.0, 0.0, 1.0);
        if (!fadeOut) dark = 1 - dark;
        if (dark > 0) p.fillRect(img.rect(), QColor(0, 0, 0, int(255 * dark)));
        if (!title.isEmpty()) drawTitle(p, std::min(1.0, (now - titleAt) / 600.0));
        return img;
    }

    /** The last card: the backdrop, the logo and "first line|second line", over everything. */
    void drawTitle(QPainter &p, double alpha) {
        QRectF area(QPointF(0, 0), QSizeF(canvas));
        p.setOpacity(alpha);
        QPainterPath all;
        all.addRect(area);
        Backdrop::paint(p, all, area);
        double h = canvas.height();
        QImage logo(":/logo.png");
        QRectF at(0, 0, h * 0.30, h * 0.30);
        at.moveCenter(QPointF(canvas.width() / 2.0, h * 0.36));
        p.drawImage(at, logo);
        QFont big("Inter");
        big.setPixelSize(int(h / 13));
        big.setWeight(QFont::Bold);
        p.setFont(big);
        p.setPen(QColor("#e8f2ee"));
        p.drawText(QRectF(0, h * 0.54, canvas.width(), h * 0.11), Qt::AlignCenter, title.section('|', 0, 0));
        QFont small("Inter");
        small.setPixelSize(int(h / 30));
        p.setFont(small);
        p.setPen(Theme::backdropCrest);
        p.drawText(QRectF(0, h * 0.66, canvas.width(), h * 0.06), Qt::AlignCenter, title.section('|', 1, 1));
        p.setPen(QColor("#9fb3ab"));
        p.drawText(QRectF(0, h * 0.72, canvas.width(), h * 0.06), Qt::AlignCenter, title.section('|', 2, 2));
        p.setOpacity(1);
    }

    /** A caption in a dark pill at the foot of the picture, fading in and out. */
    void drawCaption(QPainter &p, const QString &text, double alpha) {
        if (text.isEmpty() || alpha <= 0) return;
        QFont f("Inter");
        f.setPixelSize(canvas.height() / 36);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        QRect r = p.fontMetrics().boundingRect(text);
        int padX = canvas.height() / 34, padY = canvas.height() / 70;
        QRectF pill(0, 0, r.width() + 2 * padX, r.height() + 2 * padY);
        pill.moveCenter(QPointF(canvas.width() / 2.0, canvas.height() - canvas.height() / 9.0));
        p.setOpacity(alpha);
        p.setPen(QPen(Theme::backdropCrest, 2));
        p.setBrush(QColor(8, 12, 11, 225));
        p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        p.setPen(QColor("#e8f2ee"));
        p.drawText(pill, Qt::AlignCenter, text);
        p.setOpacity(1);
    }

    MainWindow *window;
    Model *model;
    Backend *backend;
    QSize canvas;
    QImage desktop;
    QTimer frames;
    QElapsedTimer clock;
    qint64 written = 0, end = 0;
    QString caption, previous, title;
    qint64 titleAt = 0;
    qint64 captionAt = 0, previousAt = 0, fadeAt = -100000;
    bool fadeOut = false;
    QPointer<Splash> splash;
};
}  // namespace

QString Shots::file() {
    return qEnvironmentVariable("LEMURI_SHOT");
}

void Shots::take(MainWindow *window, Model *model, Backend *backend, const QString &file) {
    if (qEnvironmentVariableIsSet("LEMURI_FILM")) {
        QStringList size = qEnvironmentVariable("LEMURI_FILM_SIZE", "1920x1080").split('x');
        QSize inside = QSize(size[0].toInt(), size[1].toInt()) / window->devicePixelRatioF();
        window->resize(window->size() - window->contentsRect().size() + inside);
        window->move(-window->contentsRect().topLeft());
        new Film(window, model, backend);
        return;
    }
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
