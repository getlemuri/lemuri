#include "base/Files.h"

#include "base/Handles.h"

#include <QDir>
#include <QFileInfo>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

QString Files::localUser() {
    for (const char *v : {"USER", "LOGNAME"}) {
        QString u = qEnvironmentVariable(v);
        if (!u.isEmpty()) return u;
    }
    // Started by systemd, cron or a bare environment: neither is set, but the account is still there.
    long size = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    QByteArray buffer(size > 0 ? size : 16384, '\0');
    passwd entry{}, *found = nullptr;
    if (::getpwuid_r(::getuid(), &entry, buffer.data(), buffer.size(), &found) == 0 && found && found->pw_name)
        return QString::fromLocal8Bit(found->pw_name);
    return {};
}

QString Files::data() {
    return QDir::homePath() + "/.lemuri";
}

void Files::privateFolder(const QString &dir) {
    QDir().mkpath(dir);
    ::chmod(QFile::encodeName(dir).constData(), 0700);
}

bool Files::writePrivate(const QString &file, const QByteArray &bytes, QString *error) {
    privateFolder(QFileInfo(file).absolutePath());
    QByteArray tmp = QFile::encodeName(file + ".tmp"), to = QFile::encodeName(file);
    ::unlink(tmp.constData());
    Fd fd(::open(tmp.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    auto failed = [&](const char *what) {
        if (error) *error = QString("%1: %2").arg(what, QString::fromLocal8Bit(std::strerror(errno)));
        fd.reset();
        ::unlink(tmp.constData());
        return false;
    };
    if (!fd) return failed("create");
    for (qsizetype done = 0; done < bytes.size();) {
        ssize_t n = ::write(fd.get(), bytes.constData() + done, size_t(bytes.size() - done));
        if (n < 0) {
            if (errno == EINTR) continue;
            return failed("write");
        }
        done += n;
    }
    if (::fsync(fd.get()) != 0) return failed("sync");
    fd.reset();
    if (::rename(tmp.constData(), to.constData()) != 0) return failed("rename");
    return true;
}
