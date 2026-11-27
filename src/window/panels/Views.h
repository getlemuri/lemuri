#pragma once

#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>

class Actions;
class Gauge;
class Model;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;
class QVBoxLayout;
class Session;
class TabStrip;

/**
 * The hosts of Hosts (all but the received ones), of a folder (subfolders included) or the received ones: a toolbar,
 * then a table with state, address, system, the fullest disk and the open sessions. A double click shows the host.
 */
class HostListView : public QWidget {
    Q_OBJECT
public:
    enum Scope { All, InFolder, Received };
    HostListView(Model *model, Actions *actions, Scope scope, const QString &folder = {}, QWidget *parent = nullptr);

private:
    void fill();
    void refreshDisks();
    QStringList shown() const;
    QString selected() const;

    Model *model;
    Actions *actions;
    Scope scope;
    QString folder;
    QLineEdit *search;
    QTableWidget *table;
    QList<QPushButton *> needSelection;
};

/** The CPU columns of the summary: newest on the right. */
class CpuColumns : public QWidget {
public:
    using QWidget::QWidget;
    QList<double> values;
    QSize sizeHint() const override { return {400, 120}; }

protected:
    void paintEvent(QPaintEvent *) override;
};

/**
 * The page of a host: details with updates and reboot, CPU/RAM/disk/security rings, the SSH security checks,
 * disks, CPU history and notes. Read again every 4 seconds while in view.
 */
class HostSummaryView : public QWidget {
    Q_OBJECT
public:
    HostSummaryView(Model *model, Actions *actions, const QString &hostId, QWidget *parent = nullptr);
    QString hostId() const { return id; }
    /**
     * The host or its sessions changed (edited, saved, probed, a shell opened): the page follows in place, keeping
     * the readings it has, instead of being made again from empty.
     */
    void refresh();

private:
    void show();
    void showDetails();
    void refreshNote();
    void askUpdates(bool force);
    void showUpdates(const QJsonObject &u);
    void showSecurity();
    QWidget *securityCard(const QString &prefix, const QString &icon, const QString &title, const QList<QJsonObject> &checks);

    Model *model;
    Actions *actions;
    QString id;
    Gauge *cpu, *ram, *disk, *security;
    QLabel *note, *upFor, *noteUnsaved;
    QWidget *updatesBox, *rebootBox;
    QVBoxLayout *disks;
    class QGridLayout *grid;
    class QPlainTextEdit *noteText;
    class QPushButton *noteSave;
    QString savedNote;
    QWidget *securityBody;
    CpuColumns *columns;
    QTimer *round;
    QJsonObject updates, securityReport;
    bool haveUpdates = false, haveSecurity = false;
    int ticks = 0;
};

/**
 * The console: the row of all the session tabs, of every host, and under it the tab in view, its parts side by
 * side and one above the other. Each part has its host's commands against its top right corner, and while it
 * connects (or after it failed) a cover saying so.
 */
class ConsoleView : public QWidget {
    Q_OBJECT
public:
    ConsoleView(Model *model, Actions *actions, const QString &hostId, QWidget *parent = nullptr);
    /**
     * Ghost mode (`hostId` empty): see-through, and it follows the tab of the host chosen in the tree, or the one
     * already shown, or the last one opened. `extra` goes on the right of the tabs.
     */
    ConsoleView(Model *model, Actions *actions, QWidget *extra, QWidget *parent = nullptr);
    ~ConsoleView() override;
    /** The tab in view, in ghost mode. */
    Session *shownTab() const { return shown; }
    /** Ghost mode: a page in place of the terminals, under the tabs (null: the terminals again); it becomes a child of the console. */
    void setPage(QWidget *page);

signals:
    /** A tab clicked or a new one asked for: the user wants the terminals. */
    void terminalsWanted();

private:
    void rebuild();
    void refreshStrip();
    QWidget *draw(class Pane *p, Session *tab);
    QWidget *slot(Session *s, bool synced);
    void release();

    Model *model;
    Actions *actions;
    QString hostId;
    TabStrip *strip;
    QWidget *area;
    QVBoxLayout *areaLayout;
    QPointer<QWidget> page;
    QList<Session *> shownTabs;
    QList<QPointer<QWidget>> views;
    bool ghost = false;
    Session *shown = nullptr;
    void build(QWidget *extra);
    Session *chooseTab();
};

/** The bottom panel: a shell on this computer, the activity log and the open sessions. Collapsed to its head. */
class ActivityLog : public QWidget {
    Q_OBJECT
public:
    ActivityLog(Model *model, QWidget *parent = nullptr);
    /** The local shell, lent to the ghost window (started if needed); taken back with takeBackShell. */
    class TerminalView *lendShell();
    void takeBackShell();
    bool opened() const { return isOpen; }
    void setOpened(bool open);

signals:
    void openedChanged(bool open);
    /** The local shell has exited. */
    void shellGone();

private:
    void choose(int tab);
    void fillTasks();
    void fillSessions();
    void startShell();

    Model *model;
    bool isOpen = false;
    int current = 1;
    QList<QPushButton *> tabs;
    QWidget *body;
    QVBoxLayout *bodyLayout;
    QTableWidget *tasks, *sessions;
    class TerminalView *shell = nullptr;
    QString shellId;
    bool lent = false;
    QWidget *clear;
    class IconButton *toggle;
};
