#pragma once

#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWidget>

class Actions;
class ActivityLog;
class ConsoleView;
class HostTree;
class IconButton;
class Model;
class QSlider;
class QSplitter;
class QVBoxLayout;
class TerminalView;

/**
 * Ghost mode: the main window goes away and a borderless window takes its place,
 * black and see-through like a desktop widget, with the session tabs, the host tree, the terminal and the status
 * bar, and the sharing page in place of the terminal. Press and hold anywhere to move it, or just drag its bare areas (the left column above all); it can float
 * free, fill the screen or stick to a side (then it stays on top and its inner edge can be dragged). F11 hides it; the tray icon brings it back or ends the mode, which also
 * ends by itself when the last session closes.
 */
class GhostWindow : public QWidget {
    Q_OBJECT
public:
    GhostWindow(Model *model, Actions *actions, ActivityLog *log, QWidget *statusBar);
    ~GhostWindow() override;
    /** Shows it where `from` was. */
    void start(const QRect &from);
    /** F11 or the tray: hides a visible ghost, brings back a hidden one. */
    void toggle();
    void restore();
    /** The status bar lent by the main window, to give it back. */
    QWidget *statusBar() const { return status; }

signals:
    /** Back to the normal window: the user asked, or there are no sessions left. */
    void leave();

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

private:
    QWidget *controls();
    void showTree(bool show);
    void showShell(bool show);
    /** The sharing page under the tabs in place of the terminal, or the terminal again. */
    void showTeam(bool show);
    void dock(const QString &side);
    void toggleMaximized();
    void place();
    void refreshControls();
    Qt::Edges innerEdge() const;

    Model *model;
    Actions *actions;
    ActivityLog *log;
    QWidget *status;
    HostTree *tree = nullptr;
    QWidget *treeColumn = nullptr;
    QSplitter *split = nullptr, *vertical = nullptr;
    ConsoleView *console = nullptr;
    QWidget *teamPage = nullptr;
    IconButton *teamButton = nullptr, *terminalButton = nullptr;
    IconButton *hostsButton = nullptr, *maximizeButton = nullptr, *shellTab = nullptr;
    QList<IconButton *> dockButtons;
    QSlider *opacity = nullptr;
    QPointer<TerminalView> shell;
    QWidget *shellBox = nullptr;
    double alpha = 0.55;
    QString docked;
    bool maximized = false;
    int dockWidth = 0, dockHeight = 0;
    QRect floating;
    QTimer hold;
    QPoint pressedAt;
    bool pressed = false;
    /** Pressed on a bare area while docked or maximized: the first drag sets it free. */
    bool unstickOnDrag = false;
    void unstick(const QPoint &global, const QPoint &at);
};
