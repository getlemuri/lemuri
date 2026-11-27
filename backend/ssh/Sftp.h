#pragma once

#include <QList>
#include <QString>
#include <functional>
#include <memory>

class RemoteFile;
class SshConnection;
typedef struct sftp_session_struct *sftp_session;

/**
 * SFTP on an SSH connection (a channel of its own, no second login). Every call takes the connection's lock, so the
 * terminal of the same connection goes on between them. Paths are taken as they are (no wildcards). Throws SshError.
 */
class Sftp {
public:
    struct Attrs {
        bool exists = false, dir = false, link = false, regular = false;
        qint64 size = 0;
        /** Last change, in seconds since 1970 (0 when the server does not say). */
        qint64 mtime = 0;
    };
    struct Entry {
        QString name;
        Attrs attrs;
    };
    /** Bytes moved since the last call; false to stop (cancelled). */
    using Progress = std::function<bool(qint64 bytes)>;

    explicit Sftp(std::shared_ptr<SshConnection> c);
    ~Sftp();
    QString home();
    /** The absolute path the server gives for `path` ("." is the home folder). */
    QString canonical(const QString &path);
    Attrs stat(const QString &path);
    Attrs lstat(const QString &path);
    QList<Entry> list(const QString &path);
    void mkdir(const QString &path);
    void rename(const QString &from, const QString &to);
    void remove(const QString &path);
    /** An empty folder. */
    void rmdir(const QString &path);
    /** The first `n` bytes of a file. */
    QByteArray head(const QString &path, int n);
    void upload(const QString &local, const QString &remote, const Progress &progress);
    void download(const QString &remote, const QString &local, const Progress &progress);

    static QString join(const QString &folder, const QString &name);
    /** The last part of a server path: the name a download takes here. */
    static QString lastName(const QString &path);
    /** A name from the server that can become a file here: not empty, not . or .., no / or \ or control characters. */
    static bool safeLocalName(const QString &n);

private:
    [[noreturn]] void fail(const QString &what);
    /** Opens a remote file, closed when the pointer goes. */
    std::unique_ptr<RemoteFile> openFile(const QString &path, int flags, int mode);

    std::shared_ptr<SshConnection> conn;
    sftp_session sftp = nullptr;
};
