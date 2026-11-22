#pragma once

#include "app/Model.h"

#include <QJsonObject>
#include <QPointer>
#include <QWidget>

class Actions;
class ActivityLog;
class Backend;
class HostTree;
class IconButton;
class QLabel;
class QHBoxLayout;
class QSplitter;
class QVBoxLayout;

/**
 * The main window: its own top bar, the activity bar on the left and the
 * status bar at the bottom forming one grey frame; inside it the host column and the central area, rounded
 * panels with a thin gap between them, and the bottom panel (local shell, log, sessions) under both.
 * The central area shows the node chosen in the tree: the hosts of Hosts or of a folder, a host's page or its console.
 */
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(Backend *backend, Model *model);
    /** After "hello": the version, the vault, the warnings about the saved hosts. */
    void hello(const QJsonObject &o);
    /** The settings of the "ui" node: whether the host column is open. */
    void prefs(const QJsonObject &ui);
    Actions *commands() const { return actions; }
    void openLog() const;
    /** The ghost window while in ghost mode, otherwise null. */
    QWidget *ghostWindow() const;

    /** Ghost mode: the console of the sessions becomes a see-through borderless window (see GhostWindow). */
    void enterGhost();
    void leaveGhost();
    /** From the tray or another start of the app: back in view; or `toggle`, as F11 (in view: hidden). */
    void fromOutside(bool toggle);

protected:
    void closeEvent(QCloseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void changeEvent(QEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

private:
    void layOut();
    void trayIcon();
    void follow();
    QWidget *activityBar();
    QWidget *hostColumn();
    QWidget *centralArea();
    QWidget *statusBar();

    void showNode();
    void refreshActivity();
    /** The host's page in view follows a change of the host or its sessions in place; false if it is not in view. */
    bool refreshHostPage();
    void hostButtons(const QString &id);
    void refreshStatus();
    void refreshTransfers();
    void ask(int ask, const QString &kind, const QJsonObject &m);
    QString currentFolder() const;
    int edgesAt(const QPoint &p) const;

    Backend *backend;
    Model *model;
    Actions *actions;
    HostTree *tree = nullptr;
    QWidget *column = nullptr;
    QSplitter *horizontal = nullptr, *vertical = nullptr;
    ActivityLog *log = nullptr;
    IconButton *hostsButton = nullptr, *consoleButton = nullptr, *teamButton = nullptr, *logButton = nullptr;
    QWidget *areaHead = nullptr;
    QLabel *title = nullptr;
    QHBoxLayout *areaActions = nullptr;
    QVBoxLayout *content = nullptr;
    QPointer<QWidget> view;
    Position shown;
    bool anyShown = false;
    QLabel *sessionsLabel = nullptr, *sessionsIcon = nullptr, *teamLabel = nullptr, *teamIcon = nullptr;
    QLabel *vaultLabel = nullptr, *vaultIcon = nullptr, *updateLabel = nullptr, *versionLabel = nullptr;
    QWidget *updateItem = nullptr, *transferItem = nullptr;
    QLabel *transferText = nullptr;
    class QProgressBar *transferBar = nullptr;
    IconButton *transferClose = nullptr;
    class TitleBar *titleBar = nullptr;
    QList<QJsonObject> arrivals;
    class QSystemTrayIcon *tray = nullptr;
    class QAction *ghostOff = nullptr;
    class GhostWindow *ghost = nullptr;
    QWidget *statusWidget = nullptr;
    IconButton *ghostButton = nullptr;
    void bringToFront();
    bool proposing = false;
    int frame = 6;  // the shadow around the window; none when maximized
};
