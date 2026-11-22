#include "window/MainWindow.h"

#include "app/Instance.h"

#include "app/Backend.h"
#include "app/Texts.h"
#include "dialogs/Settings.h"
#include "dialogs/Team.h"
#include "terminal/Ghost.h"
#include "terminal/TerminalView.h"
#include "widgets/Chrome.h"
#include "widgets/Icons.h"
#include "widgets/Theme.h"
#include "widgets/Ui.h"
#include "window/Actions.h"
#include "window/HostTree.h"
#include "window/Views.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QSystemTrayIcon>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace {
constexpr int EDGE = 6;

QLabel *iconLabel(const QString &icon, int side, const QColor &colour) {
    auto *l = new QLabel;
    l->setPixmap(Icons::pixmap(icon, side, colour, 2, qApp->devicePixelRatio()));
    l->setFixedSize(side, side);
    return l;
}

/** A status bar item: icon and text, light grey on the grey of the bar, a patch under the mouse. */
class StatusItem : public QWidget {
public:
    StatusItem(QLabel *&icon, QLabel *&text, const QString &iconName, const QColor &colour, std::function<void()> click = nullptr)
        : click(std::move(click)) {
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(8, 2, 8, 2);
        row->setSpacing(5);
        icon = iconLabel(iconName, 13, colour);
        text = new QLabel;
        text->setStyleSheet(Theme::css("color: @topFaint; font-size: 12px; background: transparent;"));
        row->addWidget(icon);
        row->addWidget(text);
        if (this->click) setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (!underMouse() || !click) return;
        QPainter p(this);
        p.fillRect(rect(), QColor(255, 255, 255, 36));
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
    void mousePressEvent(QMouseEvent *e) override {
        if (click && e->button() == Qt::LeftButton) click();
    }

private:
    std::function<void()> click;
};

QString measureBytes(qint64 b) {
    if (b < 1024) return QString("%1 B").arg(b);
    if (b < 1024 * 1024) return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    if (b < 1024LL * 1024 * 1024) return QString("%1 MB").arg(b / (1024.0 * 1024), 0, 'f', 1);
    return QString("%1 GB").arg(b / (1024.0 * 1024 * 1024), 0, 'f', 2);
}
}  // namespace

MainWindow::MainWindow(Backend *backend, Model *model) : backend(backend), model(model) {
    setWindowTitle("Lemuri");
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    qApp->installEventFilter(this);
    resize(1400 + 2 * frame, 860 + 2 * frame);
    setStyleSheet(Ui::styleSheet());
    actions = new Actions(model, this);
    layOut();
    trayIcon();
    follow();
    showNode();
    refreshStatus();
    refreshTransfers();
}

/** The top bar, the activity bar, the host column beside the central area above the log, the status bar. */
void MainWindow::layOut() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(frame, frame, frame, frame);
    outer->setSpacing(0);
    titleBar = new TitleBar(this);
    connect(titleBar, &TitleBar::updateClicked, this, [this] {
        QJsonObject r = this->model->updateAvailable();
        if (!r.isEmpty()) UpdateDialogs::offer(this->model, this, r);
    });
    outer->addWidget(titleBar);

    auto *middle = new QHBoxLayout;
    middle->setSpacing(0);
    middle->addWidget(activityBar());
    // The panels, on the grey of the gaps: 3 px to the right and below, 3 px between them.
    auto *panels = new QVBoxLayout;
    panels->setContentsMargins(0, 0, 3, 3);
    panels->setSpacing(0);
    vertical = new QSplitter(Qt::Vertical);
    vertical->setHandleWidth(3);
    vertical->setChildrenCollapsible(false);
    vertical->setStyleSheet(Theme::css("QSplitter::handle { background: transparent; }"));
    horizontal = new QSplitter(Qt::Horizontal);
    horizontal->setHandleWidth(3);
    horizontal->setChildrenCollapsible(false);
    horizontal->setStyleSheet(Theme::css("QSplitter::handle { background: transparent; }"));
    horizontal->addWidget(hostColumn());
    horizontal->addWidget(centralArea());
    horizontal->setStretchFactor(1, 1);
    horizontal->setSizes({370, 1030});
    vertical->addWidget(horizontal);
    auto *logPanel = new Panel(Theme::surface);
    auto *lp = new QVBoxLayout(logPanel);
    lp->setContentsMargins(0, 0, 0, 0);
    log = new ActivityLog(model);
    lp->addWidget(log);
    vertical->addWidget(logPanel);
    vertical->setStretchFactor(0, 1);
    connect(log, &ActivityLog::openedChanged, this, [this, logPanel](bool open) {
        logPanel->setMaximumHeight(open ? QWIDGETSIZE_MAX : 28);
        logPanel->setMinimumHeight(open ? 120 : 28);
        int total = vertical->height();
        vertical->setSizes(open ? QList<int>{int(total * 0.74), total - int(total * 0.74)} : QList<int>{total - 28, 28});
        refreshActivity();
    });
    logPanel->setMaximumHeight(28);
    panels->addWidget(vertical, 1);
    middle->addLayout(panels, 1);
    outer->addLayout(middle, 1);
    statusWidget = statusBar();
    outer->addWidget(statusWidget);

}

/** The tray icon: with it the X only hides the window, and ghost mode has its way back. */
void MainWindow::trayIcon() {
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray = new QSystemTrayIcon(QIcon(":/logo.png"), this);
        tray->setToolTip("Lemuri");
        auto *menu = new QMenu(this);
        menu->addAction(T("tray.show"), this, [this] { fromOutside(false); });
        ghostOff = menu->addAction(T("tray.ghostOff"), this, &MainWindow::leaveGhost);
        ghostOff->setEnabled(false);
        menu->addSeparator();
        menu->addAction(T("tray.quit"), qApp, &QApplication::quit);
        tray->setContextMenu(menu);
        connect(tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger) fromOutside(true);
        });
        tray->show();
    }

}

/** What the window follows: questions and failures of the backend, the model's changes, hosts arriving. */
void MainWindow::follow() {
    connect(backend, &Backend::ask, this, &MainWindow::ask);
    connect(backend, &Backend::failed, this, [this](const QString &why) {
        // Without a backend there may be no texts yet (the very first start): then in English.
        Ui::warning(this, "Lemuri", Texts::has("backend.failed") ? T("backend.failed", why)
                                                                  : "Lemuri's backend is not running (" + why + "). Restart Lemuri.");
    });
    connect(model, &Model::warning, this, [this](const QString &t, const QString &text) { Ui::warning(this, t, text); });
    connect(model, &Model::positionChanged, this, &MainWindow::showNode);
    connect(model, &Model::hostsChanged, this, [this] {
        // A host's page follows its data in place; the console and the lists follow by themselves.
        if (!refreshHostPage()) showNode();
    });
    connect(model, &Model::sessionsChanged, this, [this] {
        refreshStatus();
        refreshActivity();
        refreshHostPage();
    });
    connect(model, &Model::sessionChanged, this, &MainWindow::refreshStatus);
    connect(model, &Model::vaultChanged, this, &MainWindow::refreshStatus);
    connect(model, &Model::teamChanged, this, &MainWindow::refreshStatus);
    connect(model, &Model::teamChanged, this, &MainWindow::refreshActivity);
    connect(model, &Model::updateChanged, this, &MainWindow::refreshStatus);
    connect(model, &Model::transfersChanged, this, &MainWindow::refreshTransfers);
    // A host sent by a contact is offered at once, one window at a time: Accept, Reject or Later.
    connect(model, &Model::arrival, this, [this](const QJsonObject &d) {
        arrivals.append(d);
        if (proposing) return;
        proposing = true;
        QTimer::singleShot(0, this, [this] {
            while (!arrivals.isEmpty()) {
                QJsonObject x = arrivals.takeFirst();
                // Already handled (from the Team page) while it was waiting.
                bool still = std::any_of(this->model->deliveryList().begin(), this->model->deliveryList().end(),
                                         [&](const QJsonValue &v) { return v.toObject()["id"] == x["id"]; });
                if (still || this->model->deliveryList().isEmpty()) TeamDialogs::accept(this->model, actions, this, x, true);
            }
            proposing = false;
        });
    });
}


void MainWindow::prefs(const QJsonObject &ui) {
    column->setVisible(ui["hostColumn"].toString("true") != "false");
    model->setTypesCommands(ui["terminalCommands"].toString() == "true");
    refreshActivity();
}

void MainWindow::hello(const QJsonObject &o) {
    model->setHello(o);
    titleBar->setVersion(model->version());
    versionLabel->setText("Lemuri " + model->version());
    refreshStatus();
    if (o["loadingNotice"].isString()) Ui::warning(this, T("hosts.loadWarning.title"), o["loadingNotice"].toString());
    // A backend from another version of the package (a copy left behind, LEMURI_CORE): said once, at the start.
    int protocol = o["protocol"].toInt(0);
    if (protocol != Backend::PROTOCOL) Ui::warning(this, "Lemuri", T("backend.protocol", protocol, Backend::PROTOCOL));
}

// ── The parts of the window ──

QWidget *MainWindow::activityBar() {
    auto *bar = new QWidget;
    bar->setFixedWidth(48);
    auto *col = new QVBoxLayout(bar);
    col->setContentsMargins(0, 4, 0, 4);
    col->setSpacing(0);
    auto activity = [col](const QString &icon, const QString &tip) {
        auto *b = new IconButton(icon, 22, QSize(48, 46), tip);
        b->activity = true;
        b->colour = Theme::activityIcon;
        b->hoverColour = Theme::activityActive;
        b->stroke = 1.6;
        b->setCheckable(true);
        col->addWidget(b);
        return b;
    };
    hostsButton = activity("server", T("activity.hosts"));
    connect(hostsButton, &QAbstractButton::clicked, this, [this] {
        column->setVisible(column->isHidden());
        model->request("prefs.put", {{"node", "ui"}, {"values", QJsonObject{{"hostColumn", !column->isHidden() ? "true" : "false"}}}});
        refreshActivity();
    });
    consoleButton = activity("terminal", T("hosts.console"));
    connect(consoleButton, &QAbstractButton::clicked, this, [this] {
        // Without sessions it opens a local shell; otherwise the last tab, or the one of the host in view.
        if (model->sessions().isEmpty()) {
            model->openLocal();
            return;
        }
        Session *s = model->sessions().last();
        if (model->position().kind == Position::HostItem && model->active().value(model->position().key)) s = model->active().value(model->position().key);
        model->showSession(s);
    });
    teamButton = activity("share", T("team.title"));
    connect(teamButton, &QAbstractButton::clicked, this, [this] { model->select(Position::Team); });
    ghostButton = activity("ghost", T("activity.ghost"));
    connect(ghostButton, &QAbstractButton::clicked, this, [this] {
        ghostButton->setChecked(false);
        enterGhost();
    });
    col->addStretch(1);
    logButton = activity("list", T("activity.log"));
    connect(logButton, &QAbstractButton::clicked, this, [this] { log->setOpened(!log->opened()); });
    IconButton *settings = activity("gear", T("activity.settings"));
    connect(settings, &QAbstractButton::clicked, this, [this, settings] {
        settings->setChecked(false);
        SettingsDialog::open(model, actions, this);
    });
    return bar;
}

QWidget *MainWindow::hostColumn() {
    auto *panel = new Panel(Theme::side);
    column = panel;
    panel->setMinimumWidth(220);
    auto *col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // The search box, then the icon commands; a thin line above the tree.
    auto *head = new QWidget;
    head->setObjectName("hostHead");
    head->setStyleSheet(Theme::css("#hostHead { border-bottom: 1px solid @border; }"
                        "#hostHead QLineEdit { background: transparent; border: none; padding: 0; min-height: 20px; }"));
    auto *row = new QHBoxLayout(head);
    row->setContentsMargins(10, 6, 6, 6);
    row->setSpacing(2);
    auto *search = new QWidget;
    search->setObjectName("hostSearch");
    search->setAttribute(Qt::WA_StyledBackground);
    search->setStyleSheet(Theme::css("#hostSearch { border: 1px solid @border; border-radius: 3px; background: @bg; }"));
    auto *sr = new QHBoxLayout(search);
    sr->setContentsMargins(8, 2, 8, 2);
    sr->setSpacing(6);
    sr->addWidget(iconLabel("search", 14, Theme::faint));
    auto *field = new QLineEdit;
    field->setPlaceholderText(T("tree.search"));
    field->setMaxLength(100);
    sr->addWidget(field, 1);
    row->addWidget(search, 1);
    row->addSpacing(4);
    auto action = [row](const QString &icon, const QString &tip, std::function<void()> act) {
        auto *b = new IconButton(icon, 13, QSize(22, 22), tip);
        b->colour = Theme::faint;
        b->hoverColour = Theme::text;
        b->hoverBackground = Theme::on;
        QObject::connect(b, &QAbstractButton::clicked, b, [act] { act(); });
        row->addWidget(b);
        return b;
    };
    action("plus", T("hosts.add"), [this] { actions->newHost(currentFolder()); });
    action("folder", T("folders.new"), [this] { actions->newFolder(currentFolder()); });
    action("expand", T("tree.expandAll"), [this] { tree->expandFolders(true); });
    action("collapse", T("tree.collapseAll"), [this] { tree->expandFolders(false); });
    action("sidebar-close", T("tree.hide"), [this] {
        column->hide();
        model->request("prefs.put", {{"node", "ui"}, {"values", QJsonObject{{"hostColumn", "false"}}}});
        refreshActivity();
    });
    col->addWidget(head);

    tree = new HostTree(model, actions);
    connect(field, &QLineEdit::textChanged, tree, &HostTree::setFilter);
    // Esc clears the filter.
    auto *esc = new QAction(field);
    esc->setShortcut(Qt::Key_Escape);
    esc->setShortcutContext(Qt::WidgetShortcut);
    connect(esc, &QAction::triggered, field, &QLineEdit::clear);
    field->addAction(esc);
    col->addWidget(tree, 1);
    return panel;
}

QWidget *MainWindow::centralArea() {
    auto *panel = new Panel(Theme::surface);
    auto *col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    areaHead = new QWidget;
    areaHead->setObjectName("areaHead");
    areaHead->setAttribute(Qt::WA_StyledBackground);
    areaHead->setStyleSheet(Theme::css("#areaHead { background: @headPanel; border-bottom: 1px solid @border; }"));
    areaHead->setMinimumHeight(38);
    auto *row = new QHBoxLayout(areaHead);
    row->setContentsMargins(14, 4, 12, 4);
    row->setSpacing(8);
    title = new QLabel;
    title->setStyleSheet(Theme::css("font-size: 13px; font-weight: bold; background: transparent;"));
    row->addWidget(title, 1);
    areaActions = new QHBoxLayout;
    areaActions->setSpacing(4);
    row->addLayout(areaActions);
    col->addWidget(areaHead);
    auto *holder = new QWidget;
    content = new QVBoxLayout(holder);
    content->setContentsMargins(0, 0, 0, 0);
    col->addWidget(holder, 1);
    return panel;
}

QWidget *MainWindow::statusBar() {
    auto *bar = new QWidget;
    bar->setFixedHeight(22);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(6, 0, 6, 0);
    row->setSpacing(0);
    row->addWidget(new StatusItem(sessionsIcon, sessionsLabel, "terminal", Theme::sessionsIcon));
    row->addWidget(new StatusItem(teamIcon, teamLabel, "group", Theme::teamIcon, [this] { model->select(Position::Team); }));
    row->addStretch(1);

    // The transfer in progress, or their total; at the end a summary.
    transferItem = new QWidget;
    auto *tr = new QHBoxLayout(transferItem);
    tr->setContentsMargins(8, 0, 4, 0);
    tr->setSpacing(6);
    transferText = new QLabel;
    transferText->setStyleSheet(Theme::css("color: @topFaint; font-size: 12px;"));
    transferBar = new QProgressBar;
    transferBar->setFixedSize(120, 8);
    transferBar->setTextVisible(false);
    transferBar->setRange(0, 1000);
    transferBar->setStyleSheet(Theme::css("QProgressBar { background: rgba(255,255,255,0.15); border: none; border-radius: 3px; }"
                               "QProgressBar::chunk { background: @accent; border-radius: 3px; }"));
    transferClose = new IconButton("close", 11, QSize(18, 18), T("transfer.cancel"));
    transferClose->colour = Theme::topFaint;
    transferClose->hoverColour = Qt::white;
    transferClose->hoverBackground = QColor(255, 255, 255, 36);
    connect(transferClose, &QAbstractButton::clicked, this, [this] {
        QList<Transfer> running;
        for (const Transfer &t : model->transfers())
            if (!t.finished) running << t;
        if (running.size() == 1) {
            model->request("transfer.cancel", {{"transfer", running[0].id}});
        } else if (!running.isEmpty()) {
            QMenu m(this);
            for (const Transfer &t : running) {
                int id = t.id;
                m.addAction(Icons::icon("close", 13, Theme::text), QString("%1 · %2%").arg(t.what).arg(qRound(100.0 * t.done / t.total)),
                            this, [this, id] { model->request("transfer.cancel", {{"transfer", id}}); });
            }
            m.addSeparator();
            m.addAction(T("transfer.cancelAll"), this, [this, running] {
                for (const Transfer &t : running) model->request("transfer.cancel", {{"transfer", t.id}});
            });
            m.exec(transferClose->mapToGlobal(QPoint(0, -int(m.sizeHint().height()))));
        } else {
            model->clearTransfers();
        }
    });
    tr->addWidget(transferText);
    tr->addWidget(transferBar);
    tr->addWidget(transferClose);
    row->addWidget(transferItem);

    QLabel *updateIcon;
    updateItem = new StatusItem(updateIcon, updateLabel, "download", Theme::updateIcon, [this] {
        QJsonObject r = model->updateOffered();
        if (!r.isEmpty()) UpdateDialogs::offer(model, this, r);
    });
    updateItem->setToolTip(T("status.update.tip"));
    row->addWidget(updateItem);
    auto *vault = new StatusItem(vaultIcon, vaultLabel, "lock", Theme::vaultIcon, [this] { actions->toggleVault(); });
    row->addWidget(vault);
    QLabel *versionIcon;
    row->addWidget(new StatusItem(versionIcon, versionLabel, "terminal", Qt::transparent));
    QPixmap logo(":/logo.png");
    qreal ratio = devicePixelRatioF();
    logo = logo.scaled(QSize(13, 13) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    logo.setDevicePixelRatio(ratio);
    versionIcon->setPixmap(logo);
    versionLabel->setText("Lemuri " + model->version());
    return bar;
}

// ── What is in view ──

QString MainWindow::currentFolder() const {
    const Position &p = model->position();
    if (p.kind == Position::Folder) return p.key;
    if (p.kind == Position::HostItem)
        if (const Host *h = model->host(p.key)) return h->local() ? QString() : h->group;
    return QString();
}

/** Builds title and content for the node and item in view. */
void MainWindow::showNode() {
    const Position &p = model->position();
    refreshActivity();
    // In ghost mode the terminals belong to the ghost window: the hidden console must not take them back.
    if (ghost) {
        if (view) {
            content->removeWidget(view);
            delete view;
        }
        anyShown = false;
        return;
    }
    // The console stays as it is when only the tab in view changes: it follows the sessions by itself.
    bool same = anyShown && p == shown;
    bool consoleToConsole = anyShown && p.item == "console" && shown.item == "console" && p.key == shown.key;
    if (same || consoleToConsole) {
        shown = p;
        return;
    }
    shown = p;
    anyShown = true;
    if (view) {
        content->removeWidget(view);
        view->deleteLater();
    }
    while (QLayoutItem *i = areaActions->takeAt(0)) {
        if (i->widget()) i->widget()->deleteLater();
        delete i;
    }
    const Host *h = p.kind == Position::HostItem ? model->host(p.key) : nullptr;
    QWidget *w;
    if (p.kind == Position::Datacenter) {
        int n = int(std::count_if(model->hosts().begin(), model->hosts().end(), [](const Host &x) { return !x.received; }));
        title->setText(T("title.datacenter", n));
        w = new HostListView(model, actions, HostListView::All);
    } else if (p.kind == Position::Folder) {
        title->setText(T("title.folder", p.key));
        w = new HostListView(model, actions, HostListView::InFolder, p.key);
        QString path = p.key;
        areaActions->addWidget(Ui::primary(T("hosts.add"), "plus", [this, path] { actions->newHost(path); }));
        areaActions->addWidget(Ui::button(T("folders.newSub"), "folder", [this, path] { actions->newFolder(path); }));
        areaActions->addWidget(Ui::button(T("folders.rename"), "edit", [this, path] { actions->renameFolder(path); }));
        areaActions->addWidget(Ui::button(T("common.move"), "right", [this, path] { actions->moveFolder(path); }));
        areaActions->addWidget(Ui::danger(T("common.delete"), "trash", [this, path] { actions->deleteFolder(path); }));
    } else if (p.kind == Position::Received) {
        title->setText(T("tree.received"));
        w = new HostListView(model, actions, HostListView::Received);
    } else if (p.kind == Position::Team) {
        title->setText(T("team.title"));
        w = new TeamView(model, actions);
    } else if (h && p.item == "console") {
        w = new ConsoleView(model, actions, p.key);
    } else if (h) {
        title->setText(T("title.host", h->name, h->user + "@" + h->address, h->group.isEmpty() ? T("tree.datacenter") : h->group));
        hostButtons(h->id);
        w = new HostSummaryView(model, actions, h->id);
    } else {
        model->select(Position::Datacenter);
        return;
    }
    // The title row exists only when it carries buttons: elsewhere it is space for the content.
    areaHead->setVisible(areaActions->count() > 0);
    content->addWidget(w);
    view = w;
}

bool MainWindow::refreshHostPage() {
    const Position &p = model->position();
    auto *page = qobject_cast<HostSummaryView *>(view.data());
    if (ghost || !anyShown || p.kind != Position::HostItem || p.item == "console" || !page || page->hostId() != p.key) return false;
    const Host *h = model->host(p.key);
    if (!h) return false;
    title->setText(T("title.host", h->name, h->user + "@" + h->address, h->group.isEmpty() ? T("tree.datacenter") : h->group));
    // The buttons depend on the sessions (Disconnect, Remove): made again, they are only a row.
    while (QLayoutItem *i = areaActions->takeAt(0)) {
        if (i->widget()) i->widget()->deleteLater();
        delete i;
    }
    hostButtons(h->id);
    page->refresh();
    return true;
}

void MainWindow::hostButtons(const QString &id) {
    areaActions->addWidget(Ui::primary(T("hosts.console"), "terminal", [this, id] { model->showOrOpen(id); }));
    for (QWidget *b : actions->hostButtons(id)) areaActions->addWidget(b);
}

void MainWindow::refreshActivity() {
    hostsButton->setChecked(!column->isHidden());
    consoleButton->setChecked(model->position().item == "console");
    teamButton->setChecked(model->position().kind == Position::Team);
    teamButton->badge = model->deliveries();
    teamButton->update();
    logButton->setChecked(log && log->opened());
    // Ghost mode needs a terminal to show.
    if (ghostButton) ghostButton->setEnabled(!model->sessions().isEmpty());
}

void MainWindow::refreshStatus() {
    int n = model->connected(QString());
    sessionsLabel->setText(n == 1 ? T("status.sessionConnected", n) : T("status.sessionsConnected", n));
    sessionsIcon->setPixmap(Icons::pixmap("terminal", 13, n ? Theme::sessionsIcon : QColor(79, 193, 255, 150), 2, devicePixelRatioF()));
    bool ready = model->teamState() == Model::TeamState::Ready;
    teamLabel->setText(T("status.team", T(ready ? "sharing.state.ready" : "sharing.state.vaultLocked")));
    teamLabel->parentWidget()->setToolTip(model->teamMessage());
    teamIcon->setPixmap(Icons::pixmap("group", 13, ready ? Theme::updateIcon : Theme::teamIcon, 2, devicePixelRatioF()));
    vaultLabel->setText(model->vaultOpen() ? T("status.vault.open") : T("status.vault.locked"));
    vaultLabel->parentWidget()->setToolTip(model->vaultOpen() ? T("status.vault.lockTip") : T("status.vault.unlockTip"));
    vaultIcon->setPixmap(Icons::pixmap(model->vaultOpen() ? "lock-open" : "lock", 13, model->vaultOpen() ? Theme::updateIcon : Theme::vaultIcon, 2, devicePixelRatioF()));
    updateItem->setVisible(!model->updateOffered().isEmpty());
    if (!model->updateOffered().isEmpty()) updateLabel->setText(T("status.update", model->updateOffered()["tag"].toString()));
    titleBar->setUpdate(model->updateAvailable().isEmpty() ? QString() : T("update.title", model->updateAvailable()["tag"].toString()));
}

void MainWindow::refreshTransfers() {
    const QList<Transfer> &all = model->transfers();
    QList<Transfer> running, ended;
    for (const Transfer &t : all) (t.finished ? ended : running) << t;
    transferItem->setVisible(!all.isEmpty());
    if (all.isEmpty()) return;
    transferText->setStyleSheet(Theme::css("color: @topFaint; font-size: 12px;"));
    if (running.size() == 1 && ended.isEmpty()) {
        const Transfer &t = running[0];
        QString arrow = t.up ? "↑ " : "";
        if (!t.started) {
            transferText->setText(arrow + t.what + " · " + T("transfer.waiting"));
            transferBar->setValue(0);
        } else {
            double q = std::min(1.0, double(t.done) / t.total);
            transferText->setText(QString("%1%2 · %3% · %4 / %5").arg(arrow, t.file.isEmpty() ? t.what : t.file).arg(qRound(q * 100))
                                          .arg(measureBytes(t.done), measureBytes(t.total)));
            transferBar->setValue(int(q * 1000));
        }
        transferBar->show();
        transferClose->setToolTip(T("transfer.cancel"));
        return;
    }
    if (!running.isEmpty()) {
        qint64 done = 0, total = 0;
        for (const Transfer &t : all) {
            done += t.finished ? t.total : t.done;
            total += t.total;
        }
        double q = total ? double(done) / total : 0;
        transferText->setText(T("transfer.many", int(running.size())) + QString(" · %1%").arg(qRound(q * 100)));
        transferBar->setValue(int(q * 1000));
        transferBar->show();
        transferClose->setToolTip(T("transfer.cancelAll"));
        return;
    }
    // All ended: one outcome as it is, several as counts with what failed.
    transferBar->hide();
    bool failed = std::any_of(ended.begin(), ended.end(), [](const Transfer &t) { return t.outcome == Transfer::Outcome::Failed; });
    QString text;
    if (ended.size() == 1) {
        text = ended[0].message;
    } else {
        int ok = 0, bad = 0, cancelled = 0;
        QStringList why;
        for (const Transfer &t : ended) {
            if (t.outcome == Transfer::Outcome::Done) ok++;
            else if (t.outcome == Transfer::Outcome::Failed) {
                bad++;
                why << t.what + ": " + t.message;
            } else cancelled++;
        }
        QStringList parts;
        if (ok) parts << T("transfer.summary.done", ok);
        if (bad) parts << T("transfer.summary.failed", bad);
        if (cancelled) parts << T("transfer.summary.cancelled", cancelled);
        text = parts.join(" · ") + (why.isEmpty() ? "" : " · " + why.join("; "));
    }
    transferText->setText(text);
    transferText->setStyleSheet(Theme::css(QString("color: %1; font-size: 12px;").arg(failed ? Theme::failed.name() : Theme::topFaint.name())));
    transferClose->setToolTip(T("window.close"));
    // A summary without errors goes by itself; one with errors stays until closed.
    if (!failed) {
        int last = ended.last().id;
        QTimer::singleShot(6000, this, [this, last] {
            bool busy = std::any_of(model->transfers().begin(), model->transfers().end(), [](const Transfer &t) { return !t.finished; });
            if (!busy && !model->transfers().isEmpty() && model->transfers().last().id == last) {
                model->clearTransfers();
            }
        });
    }
}

// ── Ghost mode, tray ──

void MainWindow::openLog() const {
    log->setOpened(true);
}

QWidget *MainWindow::ghostWindow() const {
    return ghost;
}

void MainWindow::enterGhost() {
    if (ghost || model->sessions().isEmpty()) return;
    // The pictures without a screen have no tray: there the way back is not needed.
    if (!tray && qEnvironmentVariableIsEmpty("LEMURI_SHOT")) {
        Ui::warning(this, T("ghost.title"), T("ghost.noTray"));
        return;
    }
    QRect where = geometry().adjusted(frame, frame, -frame, -frame);
    layout()->removeWidget(statusWidget);
    ghost = new GhostWindow(model, actions, log, statusWidget);
    // The console lets go of the terminals: from now on they live in the ghost.
    showNode();
    connect(ghost, &GhostWindow::leave, this, &MainWindow::leaveGhost, Qt::QueuedConnection);
    ghost->start(where);
    if (ghostOff) ghostOff->setEnabled(true);
    hide();
}

void MainWindow::leaveGhost() {
    if (!ghost) return;
    GhostWindow *g = ghost;
    ghost = nullptr;
    if (ghostOff) ghostOff->setEnabled(false);
    g->hide();
    // The status bar comes back below the panels, the terminals into the console.
    statusWidget->setParent(this);
    static_cast<QVBoxLayout *>(layout())->addWidget(statusWidget);
    statusWidget->show();
    delete g;
    anyShown = false;
    showNode();
    bringToFront();
}

void MainWindow::bringToFront() {
    if (isMinimized()) showNormal();
    show();
    Instance::activate(this);
}

void MainWindow::fromOutside(bool toggle) {
    if (ghost) {
        if (toggle) ghost->toggle();
        else ghost->restore();
        if (ghost->isVisible()) Instance::activate(ghost);
    } else if (toggle && isVisible() && !isMinimized()) {
        hide();
    } else {
        bringToFront();
    }
}

void MainWindow::closeEvent(QCloseEvent *e) {
    // With the tray icon the X only hides the window: the sessions stay open, quit from the icon's menu.
    if (tray && tray->isVisible()) {
        e->ignore();
        hide();
        return;
    }
    QWidget::closeEvent(e);
    qApp->quit();
}

void MainWindow::keyPressEvent(QKeyEvent *e) {
    // F11 hides the window to the tray.
    if (e->key() == Qt::Key_F11 && tray) {
        hide();
        return;
    }
    QWidget::keyPressEvent(e);
}

// ── Questions from the backend ──

void MainWindow::ask(int ask, const QString &kind, const QJsonObject &m) {
    QString title = m["title"].toString(), message = m["message"].toString();
    if (kind == "confirm") {
        backend->answer(ask, Ui::confirm(this, title, message, m["action"].toString("OK")));
        return;
    }
    if (kind == "pair") {
        QString name = TeamDialogs::pairingRequest(model, this, m["peer"].toObject(), m["code"].toString());
        backend->answer(ask, name.isEmpty() ? QJsonValue() : QJsonValue(name));
        return;
    }
    bool vault = kind == "vault" || kind == "vaultCreate";
    bool create = kind == "vaultCreate";
    FramedDialog d(this, vault ? T(create ? "vault.create.title" : "vault.unlock.title") : title);
    if (vault) {
        if (!message.isEmpty()) d.body()->addWidget(Ui::faint(message));
        if (create) {
            auto *l = new QLabel(T("vault.create.choose"));
            l->setWordWrap(true);
            d.body()->addWidget(l);
        }
        d.body()->addWidget(new QLabel(T("vault.master")));
    } else {
        auto *l = new QLabel(message);
        l->setWordWrap(true);
        d.body()->addWidget(l);
    }
    auto *field = new QLineEdit;
    field->setEchoMode(QLineEdit::Password);
    field->setMinimumWidth(400);
    d.body()->addWidget(field);
    QLineEdit *again = nullptr;
    if (create) {
        d.body()->addWidget(new QLabel(T("vault.repeat")));
        again = new QLineEdit;
        again->setEchoMode(QLineEdit::Password);
        d.body()->addWidget(again);
    }
    QCheckBox *remember = nullptr;
    if (kind == "password") {
        remember = new QCheckBox(m["remember"].toString());
        remember->setChecked(true);
        d.body()->addWidget(remember);
    }
    QString noteText = vault ? T(create ? "vault.create.note" : "vault.unlock.note") : m["note"].toString();
    if (!noteText.isEmpty()) {
        QLabel *note = Ui::faint(noteText);
        note->setWordWrap(true);
        d.body()->addWidget(note);
    }
    auto *error = new QLabel(m["error"].toString());
    error->setStyleSheet(Theme::css("color: @warn;"));
    error->setVisible(!error->text().isEmpty());
    d.body()->addWidget(error);
    QPushButton *ok = d.buttons(vault ? T(create ? "vault.create.button" : "vault.unlock.button") : T("ssh.connect"));
    // The dialog accepts only with the fields right.
    ok->disconnect();
    connect(ok, &QPushButton::clicked, &d, [&] {
        if (vault && field->text().isEmpty()) {
            error->setText(T("vault.enterPassword"));
            error->show();
            return;
        }
        if (again && again->text() != field->text()) {
            error->setText(T("vault.mismatch"));
            error->show();
            return;
        }
        d.accept();
    });
    field->setFocus();
    if (d.exec() != QDialog::Accepted) {
        backend->answer(ask, QJsonValue::Null);
        return;
    }
    QJsonObject a{{"text", field->text()}};
    if (remember) a["remember"] = remember->isChecked();
    backend->answer(ask, a);
}

// ── The frame: rounded grey window with a soft shadow, resized from its edges ──

void MainWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF inner = QRectF(rect()).adjusted(frame, frame, -frame, -frame);
    for (int i = frame; i > 0; i--) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 10 + 4 * (frame - i)));
        p.drawRoundedRect(inner.adjusted(-i, -i + 2, i, i + 2), 10 + i, 10 + i);
    }
    p.setBrush(Theme::activityBg);
    double r = frame ? 10 : 0;
    p.drawRoundedRect(inner, r, r);
}

void MainWindow::changeEvent(QEvent *e) {
    if (e->type() == QEvent::WindowStateChange) {
        frame = isMaximized() || isFullScreen() ? 0 : 6;
        layout()->setContentsMargins(frame, frame, frame, frame);
        update();
    }
    QWidget::changeEvent(e);
}

int MainWindow::edgesAt(const QPoint &p) const {
    if (frame == 0) return 0;
    int edges = 0;
    if (p.x() < EDGE) edges |= Qt::LeftEdge;
    if (p.x() >= width() - EDGE) edges |= Qt::RightEdge;
    if (p.y() < EDGE) edges |= Qt::TopEdge;
    if (p.y() >= height() - EDGE) edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::mouseMoveEvent(QMouseEvent *e) {
    int edges = edgesAt(e->pos());
    bool h = edges & (Qt::LeftEdge | Qt::RightEdge), v = edges & (Qt::TopEdge | Qt::BottomEdge);
    bool diagonalDown = (edges & Qt::LeftEdge && edges & Qt::TopEdge) || (edges & Qt::RightEdge && edges & Qt::BottomEdge);
    if (h || v) setCursor(h && v ? (diagonalDown ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor) : h ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    else unsetCursor();
}

/**
 * The cursor of the edges is the window's, and everything inside inherits it: once the mouse goes from an edge into
 * a part of the window, the window hears no more moves, so it gives the cursor back there.
 */
bool MainWindow::eventFilter(QObject *o, QEvent *e) {
    if (e->type() == QEvent::Enter && o != this && o->isWidgetType() && static_cast<QWidget *>(o)->window() == this
            && testAttribute(Qt::WA_SetCursor))
        unsetCursor();
    return false;
}

void MainWindow::mousePressEvent(QMouseEvent *e) {
    int edges = edgesAt(e->pos());
    if (edges && e->button() == Qt::LeftButton) windowHandle()->startSystemResize(Qt::Edges(edges));
}
