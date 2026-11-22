#pragma once

#include <QList>
#include <QObject>
#include <QString>

class Model;
class QMenu;
class QWidget;
class Session;

/**
 * Operations on hosts, folders, sessions and the vault with their dialogs and confirmations. Used by the tree, the
 * lists, the buttons and the terminals.
 */
class Actions : public QObject {
    Q_OBJECT
public:
    Actions(Model *model, QWidget *window);

    // ── Hosts ──
    void newHost(const QString &folder);
    void editHost(const QString &id);
    void duplicateHost(const QString &id);
    void moveHost(const QString &id);
    void removeHost(const QString &id);
    void sendHost(const QString &id);
    void sshImport(const QString &folder);
    /** Reads the disks of these hosts again, unlocking the vault first if some sign in with a password. */
    void checkDisks(const QStringList &ids);

    // ── Folders ──
    void newFolder(const QString &parent);
    void renameFolder(const QString &path);
    void moveFolder(const QString &path);
    void moveFolderTo(const QString &path, const QString &dest);
    void deleteFolder(const QString &path);

    // ── Vault ──
    /** Asks the master password (or creates the vault); `then` runs once it is open. */
    void unlockVault(std::function<void()> then = nullptr);
    void toggleVault();
    void changeVaultPassword();

    // ── Menus and buttons ──
    QMenu *hostMenu(const QString &id);
    QMenu *folderMenu(const QString &path);
    QMenu *datacenterMenu();
    /** Right click on a terminal: copy, paste, split, upload, close the pane. */
    QMenu *terminalMenu(Session *s);
    /** Disconnect, Edit, Move, Send, Remove: the head of the host page. */
    QList<QWidget *> hostButtons(const QString &id);
    /** The icons against the top right corner of a terminal. */
    QList<QWidget *> terminalButtons(Session *s);

    /** Uploads into the folder the shell of `s` is in; `files` empty: chosen from the computer. */
    void upload(Session *s, const QStringList &files);
    /** Downloads a name listed by `lsCommand` in the shell of `s`, into a folder chosen here. */
    void download(Session *s, const QString &name, const QString &lsCommand);
    /** Double click on a name listed by ls: a folder is entered and listed, a text file opened with nano. */
    void openName(Session *s, const QString &name, const QString &lsCommand, const QPoint &at);

private:
    QString folderNameError(const QString &parent, const QString &name, const QString &excluded) const;
    void hostDialog(const QString &existingId, QJsonObject host, bool duplicate);
    void sendUpload(Session *s, const QStringList &files, const QString &folder);

    Model *model;
    QWidget *window;
};
