#include "terminal/Ghost.h"

#include "app/Instance.h"
#include "app/Model.h"
#include "app/Texts.h"
#include "terminal/TerminalView.h"
#include "widgets/Chrome.h"
#include "widgets/Icons.h"
#include "widgets/Theme.h"
#include "window/Actions.h"
#include "window/HostTree.h"
#include "window/Views.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QSplitterHandle>
#include <QVBoxLayout>
#include <QWindow>

namespace {
constexpr int HOLD_MS = 450, EDGE = 6;
constexpr double MIN_OPACITY = 0.15, MAX_OPACITY = 0.95;
const QStringList SIDES{"left", "top", "bottom", "right"};

IconButton *control(const QString &icon, const QString &tip) {
    auto *b = new IconButton(icon, 13, QSize(22, 20), tip);
    b->colour = Theme::ghostControl;
    b->hoverColour = Qt::white;
    b->hoverBackground = QColor(255, 255, 255, 36);
    b->checkedBackground = QColor(56, 146, 212, 90);
    return b;
}

QWidget *separator() {
    auto *s = new QWidget;
    s->setFixedSize(1, 14);
    s->setStyleSheet(Theme::css("background: rgba(255,255,255,0.25);"));
    return s;
}
}  // namespace

GhostWindow::GhostWindow(Model *model, Actions *actions, ActivityLog *log, QWidget *statusBar)
    : model(model), actions(actions), log(log), status(statusBar) {
    setWindowTitle("Lemuri");
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(Theme::css(QString("QSplitter::handle { background: rgba(255,255,255,0.10); }"
                          "QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,0.25); border-radius: 2px; }"
                          "QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px; background: @text; }")));
    hold.setSingleShot(true);
    hold.setInterval(HOLD_MS);
    connect(&hold, &QTimer::timeout, this, [this] {
        // Held long enough: the whole window moves with the mouse.
        if (pressed && windowHandle()) windowHandle()->startSystemMove();
        pressed = false;
    });

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *middle = new QHBoxLayout;
    middle->setSpacing(0);

    // On the left: the app icon, the Hosts button (opens and closes the tree) and the ghost button, lit: it ends the mode.
    auto *column = new QWidget;
    column->setFixedWidth(44);
    auto *cc = new QVBoxLayout(column);
    cc->setContentsMargins(0, 6, 0, 4);
    cc->setSpacing(0);
    auto *logo = new QLabel;
    QPixmap pm(":/logo.png");
    qreal ratio = devicePixelRatioF();
    pm = pm.scaled(QSize(26, 26) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pm.setDevicePixelRatio(ratio);
    logo->setPixmap(pm);
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedHeight(34);
    cc->addWidget(logo);
    auto activity = [cc](const QString &icon, const QString &tip) {
        auto *b = new IconButton(icon, 22, QSize(44, 42), tip);
        b->activity = true;
        b->colour = Theme::activityIcon;
        b->hoverColour = Theme::activityActive;
        b->stroke = 1.6;
        b->setCheckable(true);
        cc->addWidget(b);
        return b;
    };
    hostsButton = activity("server", T("activity.hosts"));
    hostsButton->setChecked(true);
    connect(hostsButton, &QAbstractButton::clicked, this, [this](bool on) { showTree(on); });
    IconButton *ghost = activity("ghost", T("tray.ghostOff"));
    ghost->setChecked(true);
    ghost->colour = Theme::ghostAccent;
    ghost->hoverColour = Theme::ghostAccent;
    connect(ghost, &QAbstractButton::clicked, this, &GhostWindow::leave);
    cc->addStretch(1);
    middle->addWidget(column);

    // The tree (with its close icon above it) and, on its right, the terminal with the Terminal tab and the local shell.
    tree = new HostTree(model, actions);
    treeColumn = new QWidget;
    auto *tc = new QVBoxLayout(treeColumn);
    tc->setContentsMargins(0, 0, 0, 0);
    tc->setSpacing(0);
    auto *treeHead = new QHBoxLayout;
    treeHead->setContentsMargins(0, 2, 4, 0);
    treeHead->addStretch(1);
    IconButton *hide = control("sidebar-close", T("tree.hide"));
    connect(hide, &QAbstractButton::clicked, this, [this] {
        showTree(false);
        hostsButton->setChecked(false);
    });
    treeHead->addWidget(hide);
    tc->addLayout(treeHead);
    tc->addWidget(tree, 1);

    console = new ConsoleView(model, actions, controls());
    auto *upper = new QWidget;
    auto *uc = new QVBoxLayout(upper);
    uc->setContentsMargins(0, 0, 0, 0);
    uc->setSpacing(0);
    uc->addWidget(console, 1);
    auto *shellHead = new QHBoxLayout;
    shellHead->setContentsMargins(4, 0, 0, 0);
    shellTab = new IconButton("terminal", 13, QSize(26, 22), T("log.tab.terminal"));
    shellTab->setCheckable(true);
    shellTab->colour = Theme::ghostControl;
    shellTab->hoverColour = Qt::white;
    shellTab->hoverBackground = QColor(255, 255, 255, 36);
    shellTab->checkedBackground = QColor(255, 255, 255, 40);
    connect(shellTab, &QAbstractButton::clicked, this, [this](bool on) { showShell(on); });
    shellHead->addWidget(shellTab);
    shellHead->addStretch(1);
    uc->addLayout(shellHead);
    vertical = new QSplitter(Qt::Vertical);
    vertical->setChildrenCollapsible(false);
    vertical->setHandleWidth(2);
    vertical->addWidget(upper);
    split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(2);
    split->addWidget(treeColumn);
    split->addWidget(vertical);
    split->setStretchFactor(1, 1);
    split->setSizes({240, 900});
    middle->addWidget(split, 1);
    outer->addLayout(middle, 1);
    status->setParent(this);
    outer->addWidget(status);
    status->show();

    connect(log, &ActivityLog::shellGone, this, [this] {
        shellTab->setChecked(false);
        showShell(false);
    });
    // No sessions left: back to the normal window.
    connect(model, &Model::sessionsChanged, this, [this] {
        if (this->model->sessions().isEmpty()) emit leave();
    });
    qApp->installEventFilter(this);
}

GhostWindow::~GhostWindow() {
    qApp->removeEventFilter(this);
    showShell(false);
    // The status bar goes back to its window before this one is gone.
    if (status && status->parent() == this) {
        layout()->removeWidget(status);
        status->setParent(nullptr);
    }
}

QWidget *GhostWindow::controls() {
    auto *w = new QWidget;
    auto *r = new QHBoxLayout(w);
    r->setContentsMargins(0, 0, 0, 0);
    r->setSpacing(2);
    // A sun and a small slider: only the background changes, the text of the terminal stays readable.
    auto *sun = new QLabel;
    sun->setPixmap(Icons::pixmap("sun", 13, Theme::ghostControl, 2, devicePixelRatioF()));
    sun->setToolTip(T("ghost.opacity"));
    r->addWidget(sun);
    opacity = new QSlider(Qt::Horizontal);
    opacity->setRange(int(MIN_OPACITY * 100), int(MAX_OPACITY * 100));
    opacity->setFixedWidth(90);
    opacity->setFocusPolicy(Qt::NoFocus);
    opacity->setToolTip(T("ghost.opacity"));
    connect(opacity, &QSlider::valueChanged, this, [this](int v) {
        alpha = v / 100.0;
        update();
        model->request("prefs.put", {{"node", "ui"}, {"values", QJsonObject{{"ghostOpacity", QString::number(alpha, 'f', 2)}}}});
    });
    r->addWidget(opacity);
    r->addSpacing(6);
    r->addWidget(separator());
    r->addSpacing(6);
    for (const QString &side : SIDES) {
        IconButton *b = control("dock-" + side, T(("ghost.dock." + side).toLatin1().constData()));
        b->setCheckable(true);
        connect(b, &QAbstractButton::clicked, this, [this, side] { dock(side); });
        dockButtons << b;
        r->addWidget(b);
    }
    r->addSpacing(6);
    r->addWidget(separator());
    r->addSpacing(6);
    IconButton *minimize = control("minimize", T("window.minimize"));
    connect(minimize, &QAbstractButton::clicked, this, &QWidget::showMinimized);
    r->addWidget(minimize);
    maximizeButton = control("maximize", T("window.maximize"));
    connect(maximizeButton, &QAbstractButton::clicked, this, &GhostWindow::toggleMaximized);
    r->addWidget(maximizeButton);
    // As the X of the normal window: hidden to the tray, the sessions stay; F11 or the tray icon bring it back.
    IconButton *close = control("close", T("window.close"));
    connect(close, &QAbstractButton::clicked, this, &GhostWindow::toggle);
    r->addWidget(close);
    return w;
}

void GhostWindow::start(const QRect &from) {
    floating = from;
    model->request(this, "prefs.get", {{"node", "ui"}}, [this](const QJsonValue &v) {
        double a = v.toObject()["ghostOpacity"].toString("0.55").toDouble();
        QSignalBlocker b(opacity);
        alpha = std::clamp(a, MIN_OPACITY, MAX_OPACITY);
        opacity->setValue(int(alpha * 100));
        update();
    });
    setGeometry(from);
    show();
    raise();
    activateWindow();
}

void GhostWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, int(alpha * 255)));
    double r = docked.isEmpty() && !maximized ? 8 : 0;
    p.drawRoundedRect(QRectF(rect()), r, r);
}

void GhostWindow::showTree(bool show) {
    treeColumn->setVisible(show);
}

/** The local shell of the bottom panel, under the terminal; given back to the panel when it goes. */
void GhostWindow::showShell(bool show) {
    if (show == bool(shellBox)) return;
    if (show) {
        shell = log->lendShell();
        shellBox = new QWidget;
        auto *l = new QVBoxLayout(shellBox);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(shell);
        shell->setGhost(true);
        shell->show();
        vertical->addWidget(shellBox);
        int h = vertical->height();
        vertical->setSizes({int(h * 0.7), h - int(h * 0.7)});
        shell->setFocus();
    } else {
        if (shell) {
            shell->setGhost(false);
            shell->setParent(nullptr);
        }
        delete shellBox;
        shellBox = nullptr;
        log->takeBackShell();
    }
}

void GhostWindow::refreshControls() {
    for (int i = 0; i < SIDES.size(); i++) dockButtons[i]->setChecked(SIDES[i] == docked);
    maximizeButton->setIconName(maximized ? "restore" : "maximize");
    maximizeButton->setToolTip(T(maximized ? "window.restore" : "window.maximize"));
    update();
}

/** Sticks to that side of the screen; on the same side again, it floats free where it was. */
void GhostWindow::dock(const QString &side) {
    if (docked.isEmpty() && !maximized) floating = geometry();
    docked = side == docked ? QString() : side;
    maximized = false;
    // Docked, it stays above the other windows, as a drop-down terminal; floating free it is an ordinary window again.
    bool top = !docked.isEmpty();
    if (bool(windowFlags() & Qt::WindowStaysOnTopHint) != top) {
        setWindowFlag(Qt::WindowStaysOnTopHint, top);
        show();
    }
    place();
    refreshControls();
}

void GhostWindow::toggleMaximized() {
    if (docked.isEmpty() && !maximized) floating = geometry();
    maximized = !maximized;
    place();
    refreshControls();
}

/** The whole usable area if maximized, a band along the docked side, otherwise where it floated. */
void GhostWindow::place() {
    QScreen *s = QGuiApplication::screenAt(floating.center());
    if (!s) s = screen();
    QRect v = Instance::workArea(s);
    QRect r;
    if (maximized) r = v;
    else if (docked.isEmpty()) r = floating;
    else {
        int w = dockWidth > 0 ? std::clamp(dockWidth, 320, int(v.width() * 0.9)) : std::max(320, std::min(floating.width(), v.width() / 2));
        int h = dockHeight > 0 ? std::clamp(dockHeight, 240, int(v.height() * 0.9)) : std::max(240, std::min(floating.height(), v.height() / 2));
        if (docked == "left") r = QRect(v.left(), v.top(), w, v.height());
        else if (docked == "right") r = QRect(v.right() + 1 - w, v.top(), w, v.height());
        else if (docked == "top") r = QRect(v.left(), v.top(), v.width(), h);
        else r = QRect(v.left(), v.bottom() + 1 - h, v.width(), h);
    }
    setGeometry(r);
}

void GhostWindow::resizeEvent(QResizeEvent *e) {
    // The inner edge dragged: the thickness stays for the next docks.
    if (!maximized && (docked == "left" || docked == "right")) dockWidth = width();
    else if (!maximized && !docked.isEmpty()) dockHeight = height();
    QWidget::resizeEvent(e);
}

Qt::Edges GhostWindow::innerEdge() const {
    if (docked == "bottom") return Qt::TopEdge;
    if (docked == "top") return Qt::BottomEdge;
    if (docked == "left") return Qt::RightEdge;
    if (docked == "right") return Qt::LeftEdge;
    return {};
}

bool GhostWindow::eventFilter(QObject *o, QEvent *e) {
    auto *w = qobject_cast<QWidget *>(o);
    if (!w || w->window() != this) return false;
    auto t = e->type();
    if (t != QEvent::MouseButtonPress && t != QEvent::MouseMove && t != QEvent::MouseButtonRelease) return false;
    auto *m = static_cast<QMouseEvent *>(e);
    QPoint at = mapFromGlobal(m->globalPosition().toPoint());
    // Docked: the edge facing the screen resizes the band.
    Qt::Edges edge = maximized ? Qt::Edges() : innerEdge();
    bool onEdge = edge && ((edge == Qt::TopEdge && at.y() <= EDGE) || (edge == Qt::BottomEdge && at.y() >= height() - EDGE)
                           || (edge == Qt::LeftEdge && at.x() <= EDGE) || (edge == Qt::RightEdge && at.x() >= width() - EDGE));
    if (t == QEvent::MouseMove && !(m->buttons() & Qt::LeftButton)) {
        if (onEdge) setCursor(edge & (Qt::LeftEdge | Qt::RightEdge) ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        else unsetCursor();
        return false;
    }
    if (t == QEvent::MouseButtonPress && m->button() == Qt::LeftButton) {
        if (onEdge && windowHandle()) {
            windowHandle()->startSystemResize(edge);
            return true;
        }
        // Docked or maximized it stays where it is; the slider, the separators and scroll bars keep their press.
        if (!docked.isEmpty() || maximized || qobject_cast<QSlider *>(w) || qobject_cast<QSplitterHandle *>(w) || qobject_cast<QScrollBar *>(w))
            return false;
        pressed = true;
        pressedAt = m->globalPosition().toPoint();
        hold.start();
    } else if (t == QEvent::MouseMove && pressed) {
        // It moved before the hold ended: an ordinary drag (a selection, a host dragged in the tree).
        if ((m->globalPosition().toPoint() - pressedAt).manhattanLength() > 5) {
            pressed = false;
            hold.stop();
        }
    } else if (t == QEvent::MouseButtonRelease) {
        pressed = false;
        hold.stop();
    }
    return false;
}

void GhostWindow::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_F11) {
        toggle();
        return;
    }
    QWidget::keyPressEvent(e);
}

/** Closing it from the keyboard (Alt+F4) brings back the normal window instead of losing the sessions. */
void GhostWindow::closeEvent(QCloseEvent *e) {
    e->ignore();
    emit leave();
}

void GhostWindow::toggle() {
    if (isVisible() && !isMinimized()) {
        if (docked.isEmpty() && !maximized) floating = geometry();
        hide();
    } else {
        restore();
    }
}

void GhostWindow::restore() {
    if (!isVisible()) {
        show();
        place();
    }
    if (isMinimized()) showNormal();
    raise();
    activateWindow();
}
