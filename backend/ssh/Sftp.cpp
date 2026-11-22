#include "ssh/Sftp.h"

#include "base/Handles.h"
#include "base/I18n.h"
#include "ssh/Ssh.h"

#include <QFile>
#include <fcntl.h>
#include <libssh/libssh.h>
#include <libssh/sftp.h>

namespace {
constexpr int CHUNK = 32 * 1024;

using Attributes = Owned<sftp_attributes_struct, sftp_attributes_free>;
using Text = Owned<char, ssh_string_free_char>;


Sftp::Attrs attrs(sftp_attributes a) {
    Sftp::Attrs r;
    if (!a) return r;
    r.exists = true;
    r.dir = a->type == SSH_FILEXFER_TYPE_DIRECTORY;
    r.link = a->type == SSH_FILEXFER_TYPE_SYMLINK;
    r.regular = a->type == SSH_FILEXFER_TYPE_REGULAR;
    r.size = qint64(a->size);
    return r;
}

QString sftpError(int code) {
    switch (code) {
    case SSH_FX_NO_SUCH_FILE: return "No such file";
    case SSH_FX_PERMISSION_DENIED: return "Permission denied";
    case SSH_FX_FAILURE: return "Failure";
    case SSH_FX_NO_SUCH_PATH: return "No such path";
    case SSH_FX_FILE_ALREADY_EXISTS: return "File already exists";
    case SSH_FX_WRITE_PROTECT: return "Write protected";
    case SSH_FX_NO_MEDIA: return "No media";
    case SSH_FX_CONNECTION_LOST: return "Connection lost";
    default: return QString("SFTP error %1").arg(code);
    }
}

}  // namespace

/**
 * An open remote file, closed on every way out: a request to the server, so under the connection's lock, and only
 * while the session is there (a disconnect frees it with everything it had open).
 */
class RemoteFile {
public:
    RemoteFile(sftp_file f, SshConnection &conn) : f(f), conn(conn) {}
    ~RemoteFile() {
        conn.ifConnected([this](ssh_session) { sftp_close(f); });
    }
    RemoteFile(const RemoteFile &) = delete;
    RemoteFile &operator=(const RemoteFile &) = delete;
    sftp_file get() const { return f; }

private:
    sftp_file f;
    SshConnection &conn;
};

Sftp::Sftp(std::shared_ptr<SshConnection> c) : conn(std::move(c)) {
    conn->users++;
    try {
        conn->withSession([this](ssh_session session) {
            sftp = sftp_new(session);
            if (!sftp || sftp_init(sftp) != SSH_OK) {
                QString why = QString::fromUtf8(ssh_get_error(session));
                if (sftp) sftp_free(sftp);
                sftp = nullptr;
                throw SshError{Ssh::explain(why), why};
            }
        });
    } catch (...) {
        conn->users--;
        throw;
    }
}

Sftp::~Sftp() {
    // A disconnect has freed it with its session already.
    if (sftp) conn->ifConnected([this](ssh_session) { sftp_free(sftp); });
    conn->users--;
}

void Sftp::fail(const QString &what) {
    QString why = sftpError(sftp_get_error(sftp));
    QString detail = what.isEmpty() ? why : what + ": " + why;
    throw SshError{Ssh::explain(detail), detail};
}

QString Sftp::join(const QString &folder, const QString &name) {
    return folder.endsWith('/') ? folder + name : folder + "/" + name;
}

QString Sftp::lastName(const QString &path) {
    QString p = path.endsWith('/') && path.size() > 1 ? path.chopped(1) : path;
    return p.mid(p.lastIndexOf('/') + 1);
}

bool Sftp::safeLocalName(const QString &n) {
    if (n.isEmpty() || n == "." || n == ".." || n.contains('/') || n.contains('\\')) return false;
    for (QChar c : n)
        if (c.category() == QChar::Other_Control) return false;
    return true;
}

QString Sftp::home() {
    return conn->withSession([&](ssh_session) {
        Text p(sftp_canonicalize_path(sftp, "."));
        if (!p) fail(".");
        return QString::fromUtf8(p.get());
    });
}

Sftp::Attrs Sftp::stat(const QString &path) {
    return conn->withSession([&](ssh_session) {
        Attributes a(sftp_stat(sftp, path.toUtf8().constData()));
        return attrs(a.get());
    });
}

Sftp::Attrs Sftp::lstat(const QString &path) {
    return conn->withSession([&](ssh_session) {
        Attributes a(sftp_lstat(sftp, path.toUtf8().constData()));
        return attrs(a.get());
    });
}

QList<Sftp::Entry> Sftp::list(const QString &path) {
    return conn->withSession([&](ssh_session) {
        Owned<sftp_dir_struct, sftp_closedir> d(sftp_opendir(sftp, path.toUtf8().constData()));
        if (!d) fail(path);
        QList<Entry> r;
        while (Attributes a{sftp_readdir(sftp, d.get())}) {
            QString name = QString::fromUtf8(a->name);
            if (name != "." && name != "..") r << Entry{name, attrs(a.get())};
        }
        return r;
    });
}

void Sftp::mkdir(const QString &path) {
    conn->withSession([&](ssh_session) {
        if (sftp_mkdir(sftp, path.toUtf8().constData(), 0755) != SSH_OK) fail(path);
    });
}

void Sftp::rename(const QString &from, const QString &to) {
    conn->withSession([&](ssh_session) {
        if (sftp_rename(sftp, from.toUtf8().constData(), to.toUtf8().constData()) != SSH_OK) fail(to);
    });
}

void Sftp::remove(const QString &path) {
    conn->withSession([&](ssh_session) {
        if (sftp_unlink(sftp, path.toUtf8().constData()) != SSH_OK) fail(path);
    });
}

std::unique_ptr<RemoteFile> Sftp::openFile(const QString &path, int flags, int mode) {
    return conn->withSession([&](ssh_session) {
        sftp_file opened = sftp_open(sftp, path.toUtf8().constData(), flags, mode_t(mode));
        if (!opened) fail(path);
        return std::make_unique<RemoteFile>(opened, *conn);
    });
}

QByteArray Sftp::head(const QString &path, int n) {
    std::unique_ptr<RemoteFile> f = openFile(path, O_RDONLY, 0);
    return conn->withSession([&](ssh_session) {
        QByteArray out(n, '\0');
        ssize_t got = sftp_read(f->get(), out.data(), size_t(n));
        if (got < 0) fail(path);
        out.truncate(got);
        return out;
    });
}

void Sftp::upload(const QString &local, const QString &remote, const Progress &progress) {
    QFile in(local);
    if (!in.open(QIODevice::ReadOnly)) throw SshError{in.errorString(), local + ": " + in.errorString()};
    std::unique_ptr<RemoteFile> f = openFile(remote, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    QByteArray buffer(CHUNK, '\0');
    for (;;) {
        qint64 n = in.read(buffer.data(), CHUNK);
        if (n < 0) throw SshError{in.errorString(), local + ": " + in.errorString()};
        if (n == 0) break;
        conn->withSession([&](ssh_session) {
            if (sftp_write(f->get(), buffer.constData(), size_t(n)) != n) fail(remote);
        });
        if (!progress(n)) break;
    }
}

void Sftp::download(const QString &remote, const QString &local, const Progress &progress) {
    QFile out(local);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) throw SshError{out.errorString(), local + ": " + out.errorString()};
    std::unique_ptr<RemoteFile> f = openFile(remote, O_RDONLY, 0);
    QByteArray buffer(CHUNK, '\0');
    for (;;) {
        ssize_t n = conn->withSession([&](ssh_session) {
            ssize_t got = sftp_read(f->get(), buffer.data(), CHUNK);
            if (got < 0) fail(remote);
            return got;
        });
        if (n == 0) break;
        if (out.write(buffer.constData(), n) != n) throw SshError{out.errorString(), local + ": " + out.errorString()};
        if (!progress(n)) break;
    }
}
