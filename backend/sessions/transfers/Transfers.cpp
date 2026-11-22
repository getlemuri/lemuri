#include "sessions/transfers/Transfers.h"

#include "base/core/Error.h"
#include "base/config/I18n.h"
#include "base/core/Params.h"
#include "base/core/Runtime.h"
#include "sessions/terminals/LocalShell.h"
#include "sessions/terminals/Sessions.h"
#include "ssh/Sftp.h"
#include "ssh/Ssh.h"
#include "vault/Vault.h"
#include "vault/VaultAccess.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <QStorageInfo>
#include <chrono>

namespace {
QString directionName(Transfers::Direction d) {
    return d == Transfers::Direction::Up ? "up" : "down";
}

QString outcomeName(Transfers::Outcome o) {
    switch (o) {
    case Transfers::Outcome::Done: return "DONE";
    case Transfers::Outcome::Cancelled: return "CANCELLED";
    case Transfers::Outcome::Failed: return "FAILED";
    }
    return "FAILED";
}
}  // namespace

// ── A transfer ──

struct Transfers::Transfer {
    Events *events = nullptr;
    int id = 0;
    Direction direction = Direction::Up;
    QString what, host;
    QStringList targets;
    std::atomic<bool> cancelled{false}, started{false};
    std::atomic<int> skipped{0};
    std::atomic<qint64> done{0}, total{1};
    /** Past this many bytes the server is sending more than it said: stopped (0: no limit). */
    qint64 limit = 0;
    std::atomic<bool> overflowed{false};
    QString name;
    std::mutex guard;
    qint64 last = 0;

    void setTotal(qint64 bytes) { total = std::max<qint64>(bytes, 1); }
    QString skippedReported() const {
        int n = skipped;
        return n == 0 ? QString() : " " + I18n::t("transfer.skipped", n, I18n::t("transfer.skipped.links"));
    }
    void file(const QString &n) {
        {
            std::lock_guard<std::mutex> l(guard);
            name = n;
        }
        update(true);
    }
    bool count(qint64 n) {
        done += n;
        if (limit > 0 && done > limit) {
            overflowed = true;
            cancelled = true;
        }
        update(false);
        return !cancelled;
    }
    void update(bool now) {
        std::lock_guard<std::mutex> l(guard);
        qint64 t = QDateTime::currentMSecsSinceEpoch();
        if (!now && t - last < 100) return;
        last = t;
        events->event("transfer", {{"transfer", id}, {"direction", directionName(direction)}, {"what", what}, {"file", name}, {"started", bool(started)},
                                   {"done", double(done)}, {"total", double(total)}, {"finished", false}});
    }
};

// ── Uploads and downloads of trees ──

namespace {
qint64 localSize(const QString &path) {
    QFileInfo f(path);
    if (f.isSymLink()) return 0;
    if (!f.isDir()) return f.size();
    qint64 t = 0;
    for (const QFileInfo &g : QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        t += localSize(g.absoluteFilePath());
    return t;
}

/**
 * A file or a folder into `folder`. Symbolic links are not followed: a loop would run forever, and a link would
 * bring to the server files outside the chosen folder. Every file goes first to a temporary name and takes the real
 * one only when complete; if anything fails, the temporary file is removed and the one that was there stays.
 */
void upload(Sftp &c, const QString &path, const QString &folder, Transfers::Transfer &w) {
    QFileInfo f(path);
    QString dest = Sftp::join(folder, f.fileName());
    if (f.isSymLink()) {
        w.skipped++;
        return;
    }
    if (f.isDir()) {
        try {
            c.mkdir(dest);
        } catch (const SshError &) {
            if (!c.stat(dest).dir) throw SshError{I18n::t("transfer.notAFolder", dest), dest};
        }
        for (const QFileInfo &g : QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
            upload(c, g.absoluteFilePath(), dest, w);
            if (w.cancelled) return;
        }
        return;
    }
    w.file(f.fileName());
    QString partial = Sftp::join(folder, "." + f.fileName() + ".lemuri-part");
    try {
        c.upload(path, partial, [&](qint64 n) { return w.count(n); });
        if (w.cancelled) throw SshError{"cancelled", "cancelled"};
        // SFTP v3 does not replace with rename: the old file is removed first, if there is one.
        Sftp::Attrs there = c.lstat(dest);
        if (there.exists && !there.dir) c.remove(dest);
        c.rename(partial, dest);
    } catch (const SshError &) {
        try {
            c.remove(partial);
        } catch (const SshError &) {
        }
        if (w.cancelled) return;
        throw;
    }
}

/** What a download brings: counted before it starts, to ask about it and to check the space. */
struct Survey {
    qint64 files = 0, bytes = 0;
    /** Files here that the download would replace (inside a folder that is already here). */
    qint64 replaced = 0;
};

/** Asked before downloading more than this: a click on the wrong folder should not fill the disk. */
constexpr qint64 ASK_FILES = 10'000;
constexpr qint64 ASK_BYTES = 5LL * 1024 * 1024 * 1024;
/** Left free on the disk, and allowed beyond what the server said (files that grew meanwhile). */
constexpr qint64 SPARE_BYTES = 64LL * 1024 * 1024;

QString sizeText(qint64 bytes) {
    return QLocale(I18n::language()).formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

/** Folders deeper than this are not followed: nobody nests real files so deep, a server that lies does. */
constexpr int MAX_DEPTH = 64;
/** More names than this in one download: refused, it would take hours just to list them. */
constexpr qint64 MAX_ENTRIES = 200'000;

void survey(Sftp &c, const QString &path, const QString &dest, int depth, Survey &s) {
    Sftp::Attrs a = c.lstat(path);
    if (a.link) return;
    if (!a.dir) {
        s.files++;
        s.bytes += std::max<qint64>(a.size, 0);
        if (QFileInfo(dest).isFile()) s.replaced++;
        return;
    }
    if (depth >= MAX_DEPTH) throw Fail{I18n::t("transfer.download.tooDeep", MAX_DEPTH)};
    for (const Sftp::Entry &e : c.list(path)) {
        if (++s.files > MAX_ENTRIES) throw Fail{I18n::t("transfer.download.tooMany", MAX_ENTRIES)};
        s.files--;
        if (!Sftp::safeLocalName(e.name)) continue;
        QString inside = Sftp::join(path, e.name), there = dest + "/" + e.name;
        if (e.attrs.dir) survey(c, inside, there, depth + 1, s);
        else if (!e.attrs.link) {
            s.files++;
            s.bytes += std::max<qint64>(e.attrs.size, 0);
            if (QFileInfo(there).isFile()) s.replaced++;
        }
    }
}

/** As upload, the other way: links on the server not followed, names unsafe here skipped, temporary names. */
void download(Sftp &c, const QString &path, const QString &folder, Transfers::Transfer &w, int depth = 0) {
    QString name = Sftp::lastName(path);
    if (!Sftp::safeLocalName(name)) {
        w.skipped++;
        return;
    }
    Sftp::Attrs a = c.lstat(path);
    if (!a.exists) throw SshError{I18n::t("transfer.download.missing", name), path};
    if (a.link) {
        w.skipped++;
        return;
    }
    QString dest = folder + "/" + name;
    if (a.dir) {
        QFileInfo d(dest);
        if (d.isSymLink() || (d.exists() && !d.isDir())) throw SshError{I18n::t("transfer.notAFolder", dest), dest};
        QDir().mkpath(dest);
        for (const Sftp::Entry &e : c.list(path)) {
            if (!Sftp::safeLocalName(e.name)) {
                w.skipped++;
                continue;
            }
            if (depth + 1 > MAX_DEPTH) {
                w.skipped++;
                continue;
            }
            download(c, Sftp::join(path, e.name), dest, w, depth + 1);
            if (w.cancelled) return;
        }
        return;
    }
    w.file(name);
    QString partial = folder + "/." + name + ".lemuri-part";
    try {
        c.download(path, partial, [&](qint64 n) { return w.count(n); });
        if (w.cancelled) throw SshError{"cancelled", "cancelled"};
        if (std::rename(QFile::encodeName(partial).constData(), QFile::encodeName(dest).constData()) != 0)
            throw SshError{I18n::t("transfer.download.failed", dest), dest};
    } catch (const SshError &) {
        QFile::remove(partial);
        if (w.cancelled) return;
        throw;
    }
}

// ── The double click on a name ──

/** Larger than this, a file is not opened with nano; how much of its start tells text from binary, as git does. */
constexpr qint64 MAX_TEXT = 1024 * 1024;
constexpr int HEAD = 8192;

/** What a name double clicked in the terminal is: it decides what is typed (cd, nano, nothing). */
enum class Kind { Folder, Text, Binary, Large, Other, Missing };

QString kindName(Kind k) {
    switch (k) {
    case Kind::Folder: return "FOLDER";
    case Kind::Text: return "TEXT";
    case Kind::Binary: return "BINARY";
    case Kind::Large: return "LARGE";
    case Kind::Other: return "OTHER";
    case Kind::Missing: return "MISSING";
    }
    return "OTHER";
}

Kind judge(const QByteArray &head) {
    return head.contains('\0') ? Kind::Binary : Kind::Text;
}

Kind remoteKind(Sftp &c, const QString &path) {
    Sftp::Attrs a = c.stat(path);
    if (!a.exists) return Kind::Missing;
    if (a.dir) return Kind::Folder;
    if (!a.regular) return Kind::Other;
    if (a.size > MAX_TEXT) return Kind::Large;
    try {
        return judge(c.head(path, HEAD));
    } catch (const SshError &) {
        return Kind::Text;  // not readable: nano says so better than a guess
    }
}

Kind localKind(const QString &path) {
    QFileInfo f(path);
    if (f.isDir()) return Kind::Folder;
    if (!f.exists()) return Kind::Missing;
    if (!f.isFile()) return Kind::Other;
    if (f.size() > MAX_TEXT) return Kind::Large;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return Kind::Text;
    return judge(in.read(HEAD));
}

/**
 * Names with "\", "!" or control characters are never typed: inside quotes fish reads \' as a quote, and csh/tcsh
 * expand "!" from the history even between single quotes.
 */
bool typable(const QString &s) {
    if (s.contains('\\') || s.contains('!')) return false;
    for (QChar c : s)
        if (c.category() == QChar::Other_Control) return false;
    return true;
}

/** As the user would type it: as it is if made only of safe characters, otherwise in single quotes. */
QString quote(const QString &s) {
    static const QRegularExpression plain("^[\\p{L}\\p{N}._/][\\p{L}\\p{N}._/-]*$");
    if (plain.match(s).hasMatch()) return s;
    return "'" + QString(s).replace("'", "'\\''") + "'";
}

/** The first place where the name is: in the folder the ls listed, then in the shell's folder. */
QJsonObject find(const QString &shell, const QString &listed, const QString &name, const std::function<QString()> &home,
                 const std::function<Kind(const QString &)> &check) {
    QList<QPair<QString, QString>> places;
    if (!listed.isEmpty()) {
        QString base = listed == "~" ? home() : listed.startsWith("~/") ? Sftp::join(home(), listed.mid(2))
                                                                         : listed.startsWith('/') ? listed : Sftp::join(shell, listed);
        QString p = Sftp::join(base, name);
        places << qMakePair(p, p);
    }
    // A name starting with "-" gets "./": nano and cd would read it as an option ("cd -" goes back).
    places << qMakePair(Sftp::join(shell, name), name.startsWith('-') ? "./" + name : name);
    QJsonObject last;
    for (const auto &[path, typed] : places) {
        if (!typable(path)) continue;
        Kind k = check(path);
        last = {{"path", path}, {"typed", quote(typed)}, {"kind", kindName(k)}};
        if (k != Kind::Missing) return last;
    }
    if (!last.isEmpty()) return last;
    // Not one place that can be typed safely: the user types it.
    return {{"path", name}, {"typed", QString()}, {"kind", "UNTYPABLE"}};
}

QString remotePath(Sftp &c, const QString &shell, const QString &listed, const QString &name) {
    if (!listed.isEmpty()) {
        QString base = listed == "~" ? c.home() : listed.startsWith("~/") ? Sftp::join(c.home(), listed.mid(2))
                                                                           : listed.startsWith('/') ? listed : Sftp::join(shell, listed);
        QString candidate = Sftp::join(base, name);
        if (c.lstat(candidate).exists) return candidate;
        // ls was given a file, or a pattern: the name is in the shell's folder
    }
    QString path = Sftp::join(shell, name);
    if (!c.lstat(path).exists) throw Fail{I18n::t("transfer.download.missing", name)};
    return path;
}

QString what(const QStringList &files) {
    if (files.size() != 1) return I18n::t("files.items", files.size());
    QFileInfo f(files[0]);
    return "\"" + f.fileName() + "\"" + (f.isDir() ? " " + I18n::t("transfer.folderMark") : QString());
}
}  // namespace

// ── Requests ──

Transfers::Transfers(Events &events, Loop &loop, Prompter &prompter, Sessions &sessions)
    : events(events), loop(loop), prompter(prompter), sessions(sessions) {
    loop.onStop([this] {
        std::lock_guard<std::mutex> l(guard);
        for (auto &t : active) t->cancelled = true;
        freed.notify_all();
    });
}

void Transfers::ops(Router &router) {
    using namespace std::placeholders;
    router.later("sftp.home", [this](const QJsonObject &m, const Reply &r) { sftp(m, r, [](Sftp &c) { return c.home(); }); });
    router.later("sftp.existing", [this](const QJsonObject &m, const Reply &r) { existing(m, r); });
    router.later("sftp.locate", [this](const QJsonObject &m, const Reply &r) { locate(m, r); });
    router.later("fs.check", [this](const QJsonObject &m, const Reply &r) { check(m, r); });
    router.op("sftp.upload", [this](const QJsonObject &m) { return upload(m); });
    router.op("sftp.download", [this](const QJsonObject &m) { return download(m); });
    router.op("transfer.cancel", [this](const QJsonObject &m) {
        cancel(Params::integer(m, "transfer", -1));
        return QJsonValue();
    });
}

void Transfers::sftp(const QJsonObject &m, const Reply &r, std::function<QJsonValue(Sftp &)> work) {
    auto c = sessions.ssh(Params::need(m, "session"));
    loop.work([c, r, work] {
        try {
            Sftp s(c);
            r.ok(work(s));
        } catch (const SshError &e) {
            r.fail(e.message);
        } catch (const Fail &f) {
            r.fail(f.why);
        }
    });
}

/** The names among "files" (local paths) that already exist in the server folder: an upload would replace them. */
void Transfers::existing(const QJsonObject &m, const Reply &r) {
    QStringList files = Params::strings(m, "files");
    QString folder = Params::need(m, "folder");
    sftp(m, r, [files, folder](Sftp &c) {
        QJsonArray l;
        for (const QString &f : files) {
            QString n = QFileInfo(f).fileName();
            if (c.lstat(Sftp::join(folder, n)).exists) l << n;
        }
        return l;
    });
}

/** Where a name right-clicked in the output of an ls is on the server. */
void Transfers::locate(const QJsonObject &m, const Reply &r) {
    QString shell = Params::need(m, "shell"), listed = Params::str(m, "listed"), name = Params::need(m, "name");
    sftp(m, r, [=](Sftp &c) {
        QString path = remotePath(c, shell, listed, name);
        QString w = Sftp::lastName(path);
        if (!Sftp::safeLocalName(w)) throw Fail(I18n::t("transfer.download.badName", w));
        return QJsonObject{{"path", path}, {"name", w}};
    });
}

/** The double click on a name listed by ls: what it is and how to type it; over SFTP, or here for a local shell. */
void Transfers::check(const QJsonObject &m, const Reply &r) {
    QString shell = Params::need(m, "shell"), listed = Params::str(m, "listed"), name = Params::need(m, "name");
    auto t = sessions.transport(Params::need(m, "session"));
    if (!t->isLocal()) {
        sftp(m, r, [=](Sftp &c) {
            return find(shell, listed, name, [&] {
                try {
                    return c.home();
                } catch (const SshError &) {
                    return QString("/");
                }
            }, [&](const QString &p) { return remoteKind(c, p); });
        });
        return;
    }
    loop.work([=] { r.ok(find(shell, listed, name, [] { return QDir::homePath(); }, localKind)); });
}

QJsonObject Transfers::upload(const QJsonObject &m) {
    QString sid = Params::need(m, "session");
    QStringList files = Params::strings(m, "files");
    if (files.isEmpty()) throw Fail("No files");
    QString folder = Params::need(m, "folder");
    QStringList known = Params::strings(m, "known");
    auto t = std::make_shared<Transfer>();
    t->direction = Direction::Up;
    t->what = what(files);
    t->host = sessions.host(sid);
    for (const QString &f : files) t->targets << t->host + ":" + Sftp::join(folder, QFileInfo(f).fileName());
    return start(sid, t, [this, files, folder, known](Transfer &k, Sftp &c) {
        // Confirmed knowing that "known" were there: if others have appeared meanwhile, the user is asked again.
        QStringList now;
        for (const QString &f : files) {
            QString n = QFileInfo(f).fileName();
            if (c.lstat(Sftp::join(folder, n)).exists && !known.contains(n)) now << n;
        }
        if (!now.isEmpty() && !confirm(I18n::t("transfer.upload"), I18n::t("transfer.upload.nowThere", folder, now.join(", ")),
                                       I18n::t("transfer.overwrite")))
            k.cancelled = true;
        if (k.cancelled) return I18n::t("transfer.upload.cancelled");
        qint64 total = 0;
        for (const QString &f : files) total += localSize(f);
        k.setTotal(total);
        for (const QString &f : files) {
            ::upload(c, f, folder, k);
            if (k.cancelled) break;
        }
        return k.cancelled ? I18n::t("transfer.upload.cancelled") : I18n::t("transfer.uploaded", k.what, folder) + k.skippedReported();
    });
}

QJsonObject Transfers::download(const QJsonObject &m) {
    QString sid = Params::need(m, "session"), path = Params::need(m, "path");
    QString local = Params::need(m, "into"), name = Sftp::lastName(path);
    QString target = QFileInfo(local + "/" + name).absoluteFilePath();
    bool replace = Params::flag(m, "replace");
    auto t = std::make_shared<Transfer>();
    t->direction = Direction::Down;
    t->what = "\"" + name + "\"";
    t->host = sessions.host(sid);
    t->targets << "local:" + target;
    return start(sid, t, [this, path, local, name, target, replace](Transfer &k, Sftp &c) {
        QString title = I18n::t("transfer.download");
        Survey s;
        survey(c, path, target, 0, s);
        QFileInfo here(target);
        // A file: replaced only if the user said so, or asked now (it was not there when they chose).
        if (here.exists() && !here.isDir() && !replace && !confirm(title, I18n::t("transfer.download.replace", target), I18n::t("transfer.overwrite")))
            k.cancelled = true;
        // A folder already here: the two are merged, and the files with the same name replaced. Said once, with how many.
        if (!k.cancelled && here.isDir() && s.replaced > 0
            && !confirm(title, I18n::t("transfer.download.merge", target, s.replaced), I18n::t("transfer.download.mergeAction")))
            k.cancelled = true;
        if (!k.cancelled && (s.files > ASK_FILES || s.bytes > ASK_BYTES)
            && !confirm(title, I18n::t("transfer.download.large", name, s.files, sizeText(s.bytes)), title))
            k.cancelled = true;
        if (k.cancelled) return I18n::t("transfer.download.cancelled");
        QStorageInfo disk(local);
        qint64 free = disk.isValid() ? disk.bytesAvailable() : -1;
        if (free >= 0 && s.bytes + SPARE_BYTES > free)
            throw Fail{I18n::t("transfer.download.noSpace", sizeText(s.bytes), sizeText(free))};
        k.setTotal(s.bytes);
        // The sizes come from the server: if it sends much more than it said, the download stops.
        k.limit = s.bytes + s.bytes / 10 + SPARE_BYTES;
        ::download(c, path, local, k);
        if (k.overflowed) throw Fail{I18n::t("transfer.download.overflow", sizeText(s.bytes))};
        return k.cancelled ? I18n::t("transfer.download.cancelled") : I18n::t("transfer.downloaded", name, local) + k.skippedReported();
    });
}

void Transfers::cancel(int id) {
    std::lock_guard<std::mutex> l(guard);
    auto t = active.value(id);
    if (t) t->cancelled = true;
    freed.notify_all();
}

bool Transfers::confirm(const QString &title, const QString &text, const QString &action) {
    return prompter.ask("confirm", {{"title", title}, {"message", text}, {"action", action}}).toBool();
}

QJsonObject Transfers::start(const QString &sid, std::shared_ptr<Transfer> t, Work w) {
    auto c = sessions.ssh(sid);
    t->events = &events;
    t->id = ++next;
    {
        std::lock_guard<std::mutex> l(guard);
        for (const QString &x : t->targets)
            if (busy.contains(x)) throw Fail{I18n::t("transfer.busy", t->what)};
        for (const QString &x : t->targets) busy.insert(x);
        active.insert(t->id, t);
    }
    t->update(true);
    loop.work([this, c, t, w] {
        Outcome outcome = Outcome::Failed;
        QString message;
        bool slot = false;
        {
            // At most PER_HOST at once per host: the others wait for a place, or for their cancel.
            std::unique_lock<std::mutex> l(guard);
            freed.wait(l, [&] { return t->cancelled || running.value(t->host) < PER_HOST; });
            if (!t->cancelled) {
                running[t->host]++;
                slot = true;
            }
        }
        if (!slot) {
            outcome = Outcome::Cancelled;
            message = I18n::t("transfer.cancelled");
        } else {
            t->started = true;
            t->update(true);
            try {
                Sftp s(c);
                message = w(*t, s);
                outcome = t->cancelled ? Outcome::Cancelled : Outcome::Done;
            } catch (const SshError &e) {
                outcome = Outcome::Failed;
                message = I18n::t("transfer.failed", e.message);
            } catch (const Fail &f) {
                outcome = Outcome::Failed;
                message = I18n::t("transfer.failed", f.why);
            }
        }
        {
            std::lock_guard<std::mutex> l(guard);
            if (slot) running[t->host]--;
            for (const QString &x : t->targets) busy.remove(x);
            active.remove(t->id);
            freed.notify_all();
        }
        events.event("transfer", {{"transfer", t->id}, {"direction", directionName(t->direction)}, {"what", t->what}, {"finished", true},
                                   {"outcome", outcomeName(outcome)}, {"message", message}, {"done", double(t->done)}, {"total", double(t->total)}});
    });
    return {{"transfer", t->id}};
}
