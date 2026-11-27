#pragma once

#include <QSet>
#include <QStringList>
#include <QWidget>

class Model;
class Session;
class FilesSide;

/**
 * The files of a terminal's server next to those of this computer, in place of the terminal: this computer on the
 * left, the server on the right, each with its folder, its list and its commands. A double click on a folder opens
 * it; on a file, or dragging files from one side to the other, copies them across (over SFTP, as the transfers of
 * the terminal). The server side starts in the folder the shell is in.
 */
class FilesView : public QWidget {
    Q_OBJECT
public:
    FilesView(Model *model, Session *session, QWidget *parent = nullptr);

    /** Copies these files of this computer into the server folder on the right. */
    void upload(const QStringList &paths);
    /** Copies these server paths into the folder on the left. */
    void download(const QStringList &paths);
    /** Both lists read again. */
    void reload();
    /** The server side has its folder, and the session is connected. */
    bool remoteReady() const;
    /** Ghost mode: no background of its own, the see-through window behind gives it; light veils on the parts. */
    void setGhost(bool on);

    Model *model;
    Session *session;
    bool showHidden = false;

private:
    FilesSide *local, *remote;
    /** The transfers already seen finished: a new one reads the lists again. */
    QSet<int> finished;
    bool pending = false;
    bool ghost = true;
};
