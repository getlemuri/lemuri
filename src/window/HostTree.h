#pragma once

#include <QPersistentModelIndex>
#include <QSet>
#include <QTreeWidget>

class Actions;
class Model;

/**
 * The host tree: "Hosts" on top with the nested folders and their hosts, then the hosts
 * received from contacts grouped by sender. Rows 28 px high drawn as rounded pills, thin chevrons, host icons
 * coloured by reachability, a soft pill with the open sessions and a small bar with the fullest disk.
 * A click shows the node (or brings back a host's shell), a double click opens a console, right click the
 * operations; hosts and folders can be dragged onto a folder or onto "Hosts".
 */
class HostTree : public QTreeWidget {
    Q_OBJECT
public:
    static constexpr int KEY = Qt::UserRole;       // "dc", "folder:<path>", "host:<id>", "received", "received:<from>"
    static constexpr int TALLY = Qt::UserRole + 1;  // sessions of a host

    HostTree(Model *model, Actions *actions, QWidget *parent = nullptr);
    void setFilter(const QString &text);
    void expandFolders(bool open);

protected:
    void drawRow(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void drawBranches(QPainter *, const QRect &, const QModelIndex &) const override {}
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void startDrag(Qt::DropActions) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;
    bool viewportEvent(QEvent *) override;

private:
    void rebuild();
    void syncSelection();
    void choose(QTreeWidgetItem *item);
    QTreeWidgetItem *add(QTreeWidgetItem *parent, const QString &key, const QString &text);
    /** Where something can be dropped: "" is Hosts, a path a folder; null for the other rows. */
    QString destination(QTreeWidgetItem *item) const;
    bool accept(const QString &dragged, const QString &dest) const;
    int depthOf(const QModelIndex &i) const;

    Model *model;
    Actions *actions;
    QString filter;
    QSet<QString> closed;
    QPersistentModelIndex hovered, target;
    QString dragged;
    bool syncing = false;
};
