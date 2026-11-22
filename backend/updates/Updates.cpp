#include "updates/Updates.h"

#include "base/Error.h"
#include "base/I18n.h"
#include "base/Params.h"
#include "base/Prefs.h"
#include "base/Runtime.h"
#include "vault/Vault.h"
#include "vault/VaultAccess.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace {
const QString LATEST = QStringLiteral("https://api.github.com/repos/getlemuri/lemuri/releases/latest");
const QString HOME = QStringLiteral("/opt/lemuri");
constexpr qint64 MAX_PACKAGE = 300LL << 20, MAX_ANSWER = 4LL << 20;
constexpr qint64 DAY_MS = 24LL * 3600 * 1000;
const QString NODE = QStringLiteral("lemuri/updates");

/** Exit code (-1: the program is missing or did not end in time) and the end of the output. */
struct Run {
    int code;
    QString out;
};

Run run(int seconds, const QStringList &command, const QString &input = QString()) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    if (input.isNull()) p.setStandardInputFile(QProcess::nullDevice());
    else p.setStandardInputFile(input);
    p.start(command.first(), command.mid(1));
    if (!p.waitForStarted(10'000)) return {-1, ""};
    if (!p.waitForFinished(seconds * 1000)) {
        p.kill();
        p.waitForFinished(2000);
        return {-1, ""};
    }
    QString s = QString::fromUtf8(p.readAll());
    if (s.size() > 3000) s = s.right(3000);
    return {p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1, s};
}

QString arch() {
    static const QString a = [] {
        Run r = run(10, {"dpkg", "--print-architecture"});
        QString s = r.out.trimmed();
        return r.code == 0 && QRegularExpression("^[a-z0-9]+$").match(s).hasMatch() ? s : QString("amd64");
    }();
    return a;
}

QString packageName(const QString &version) {
    return "lemuri_" + version + "_" + arch() + ".deb";
}

QString text(const QJsonValue &v) {
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble());
    if (v.isBool()) return v.toBool() ? "true" : "false";
    return "";
}

/**
 * One GET on a thread of its own event loop: the body (at most `max` bytes) into memory, or into `to`.
 * Redirects are followed, never from HTTPS to HTTP. Throws Fail.
 */
QByteArray get(const QString &url, const QList<QPair<QByteArray, QByteArray>> &headers, qint64 max, int timeoutMs, int *status,
               QFile *to = nullptr) {
    QNetworkAccessManager net;
    QNetworkRequest q{QUrl(url)};
    q.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    q.setTransferTimeout(timeoutMs);
    for (const auto &[k, v] : headers) q.setRawHeader(k, v);
    QNetworkReply *a = net.get(q);
    QByteArray body;
    qint64 got = 0;
    bool tooLarge = false;
    QString writeError;
    QEventLoop loop;
    QObject::connect(a, &QNetworkReply::readyRead, &loop, [&] {
        QByteArray chunk = a->readAll();
        got += chunk.size();
        if (got > max) {
            tooLarge = true;
            a->abort();
            return;
        }
        if (to) {
            if (to->write(chunk) != chunk.size()) {
                writeError = to->errorString();
                a->abort();
            }
        } else {
            body += chunk;
        }
    });
    QObject::connect(a, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    *status = a->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError error = a->error();
    QString why = a->errorString();
    delete a;
    if (tooLarge) throw Fail{"Download too large"};
    if (!writeError.isNull()) throw Fail{writeError};
    // An HTTP error status still has a body: the caller looks at the status.
    if (error != QNetworkReply::NoError && *status == 0) throw Fail{why};
    return body;
}
}  // namespace

// ── Versions ──

bool Versions::valid(const QString &v) {
    static const QRegularExpression plain("^\\d{1,6}(\\.\\d{1,6}){0,3}$");
    return plain.match(v).hasMatch();
}

int Versions::compare(const QString &a, const QString &b) {
    QStringList x = a.split('.'), y = b.split('.');
    for (int i = 0; i < std::max(x.size(), y.size()); i++) {
        int p = i < x.size() ? x[i].toInt() : 0, q = i < y.size() ? y[i].toInt() : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

// ── The package ──

namespace {
/**
 * What the tests put in place of GitHub, the release keys and apt (LEMURI_UPDATE_API, _SIGNERS, _INSTALL). Only in
 * the tests' backend (lemuri-core-testing): in the package these are always empty, whatever the environment says.
 */
QString testHook(const char *name) {
#if defined(LEMURI_TEST_HOOKS)
    return qEnvironmentVariable(name);
#else
    Q_UNUSED(name);
    return QString();
#endif
}
}  // namespace

PackageUpdater::PackageUpdater()
    : api(testHook("LEMURI_UPDATE_API").isEmpty() ? LATEST : testHook("LEMURI_UPDATE_API")),
      testing(!testHook("LEMURI_UPDATE_INSTALL").isEmpty()) {}

QString PackageUpdater::current() const {
    QString v = QStringLiteral(LEMURI_VERSION);
    return Versions::valid(v) ? v : QString("0.0.0");
}

std::optional<Release> PackageUpdater::latest() {
    int status = 0;
    QByteArray json = get(api, {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"},
                                {"User-Agent", ("Lemuri/" + current()).toUtf8()}},
                          MAX_ANSWER, 20'000, &status);
    if (status == 404) return std::nullopt;
    if (status == 403 || status == 429) throw Fail{I18n::t("update.error.busy")};
    if (status != 200) throw Fail{QString("GitHub: HTTP %1").arg(status)};
    QJsonObject found;
    std::optional<Release> r = parse(json, arch(), testing, &found);
    if (r) {
        std::lock_guard<std::mutex> l(m);
        assets = found;
    }
    return r;
}

std::optional<Release> PackageUpdater::parse(const QByteArray &json, const QString &arch, bool anyUrl, QJsonObject *assets) {
    QJsonDocument d = QJsonDocument::fromJson(json);
    if (!d.isObject()) throw Fail{"GitHub: unexpected answer"};
    QJsonObject o = d.object();
    QString tag = text(o["tag_name"]);
    if (!tag.startsWith("v") || !Versions::valid(tag.mid(1))) throw Fail{"GitHub: unexpected tag " + tag};
    QString version = tag.mid(1);
    QJsonObject found;
    for (const QJsonValue &e : o["assets"].toArray()) {
        if (!e.isObject()) continue;
        QString name = text(e["name"]), url = text(e["browser_download_url"]);
        if (url.startsWith("https://") || anyUrl) found[name] = url;
    }
    QString deb = "lemuri_" + version + "_" + arch + ".deb";
    // A release without the package for this computer (only sources, or another processor) is not an update here.
    if (!found.contains(deb) || !found.contains(deb + ".sig")) return std::nullopt;
    QString url = text(o["html_url"]);
    if (!url.startsWith("https://github.com/getlemuri/lemuri/")) url = "https://github.com/getlemuri/lemuri/releases/tag/" + tag;
    if (assets) *assets = found;
    return Release{tag, version, text(o["body"]), url};
}

QString PackageUpdater::blocker() const {
#ifdef LEMURI_NO_SELF_UPDATE
    // Built for a package manager (LEMURI_SELF_UPDATE OFF): it brings the new versions.
    return I18n::t("update.blocker.packageManager");
#endif
    if (!testing) {
        if (!QCoreApplication::applicationFilePath().startsWith(HOME + "/") || run(10, {"dpkg-query", "-W", "lemuri"}).code != 0)
            return I18n::t("update.blocker.notDeb");
        if (!QFileInfo(HOME + "/bin/lemuri-install").isExecutable()) return I18n::t("update.blocker.notDeb");
        if (run(10, {"pkexec", "--version"}).code != 0)
            return I18n::t("update.blocker.noPkexec");
    }
    if (run(10, {"ssh-keygen", "-?"}).code == -1) return I18n::t("update.blocker.noSshKeygen");
    if (signers().isEmpty()) return I18n::t("update.blocker.noKey");
    return QString();
}

/** The allowed signers (ssh-keygen format) without comments; empty if there are none. */
QString PackageUpdater::signers() const {
    QString path = testHook("LEMURI_UPDATE_SIGNERS");
    QFile f(path.isEmpty() ? QString(":/lemuri/package-signers") : path);
    if (!f.open(QIODevice::ReadOnly)) return "";
    QString all;
    for (const QString &line : QString::fromUtf8(f.readAll()).split('\n')) {
        QString l = line.trimmed();
        if (!l.isEmpty() && !l.startsWith('#')) all += l + "\n";
    }
    return all;
}

void PackageUpdater::install(const Release &r, const std::function<void(const QString &)> &progress) {
    QString no = blocker();
    if (!no.isNull()) throw Fail{no};
    QString deb = packageName(r.version);
    QJsonObject known;
    {
        std::lock_guard<std::mutex> l(m);
        known = assets;
    }
    // Found by an earlier run (kept in the preferences): where to download it from is read again.
    if (!known.contains(deb)) {
        latest();
        std::lock_guard<std::mutex> l(m);
        known = assets;
    }
    QString debUrl = text(known[deb]), sigUrl = text(known[deb + ".sig"]);
    if (debUrl.isEmpty() || sigUrl.isEmpty())
        throw Fail{I18n::t("update.error.noPackage")};
    // Readable by everyone: apt reads it as its own user (_apt). Only this user can write in it.
    QByteArray pattern = QFile::encodeName(QDir::tempPath() + "/lemuri-update-XXXXXX");
    if (!::mkdtemp(pattern.data())) throw Fail{"Cannot create a temporary folder"};
    QString dir = QFile::decodeName(pattern);
    ::chmod(pattern.constData(), 0755);
    struct Clean {
        QString dir;
        ~Clean() { QDir(dir).removeRecursively(); }
    } clean{dir};
    progress(I18n::t("update.step.download", deb));
    QString file = dir + "/" + deb, sig = file + ".sig";
    download(sigUrl, sig, 64 << 10);
    download(debUrl, file, MAX_PACKAGE);

    progress(I18n::t("update.step.verify"));
    verify(file, sig, signers());
    Run fields = run(30, {"dpkg-deb", "--field", file, "Package", "Version"});
    if (fields.code != 0 || !fields.out.contains("Package: lemuri") || !fields.out.contains("Version: " + r.version))
        throw Fail{I18n::t("update.error.notLemuri", r.version)};

    // The package's helper checks it all again as root, on a copy no process of the user can change (see
    // packaging/linux/lemuri-install), then installs it.
    progress(I18n::t("update.step.install"));
    QString install = testHook("LEMURI_UPDATE_INSTALL");
    QStringList c = install.isEmpty() ? QStringList{"pkexec", HOME + "/bin/lemuri-install"} : install.split(' ', Qt::SkipEmptyParts);
    c << file << sig << r.version;
    Run done = run(1800, c);
    // pkexec: 126 when the user closes the password window, 127 when it is not authorised.
    if (done.code == 126 || done.code == 127) throw Fail(I18n::t("update.error.cancelled"));
    if (done.code == 3) throw Fail(I18n::t("update.error.badSignature"));
    if (done.code == 4) throw Fail(I18n::t("update.error.notLemuri", r.version));
    if (done.code == 6) throw Fail(I18n::t("update.error.notNewer", r.version));
    if (done.code != 0) throw Fail(I18n::t("update.error.apt", done.out));
}

void PackageUpdater::download(const QString &url, const QString &to, qint64 max) const {
    QFile part(to + ".part");
    if (!part.open(QIODevice::WriteOnly | QIODevice::Truncate)) throw Fail{part.errorString()};
    int status = 0;
    try {
        get(url, {{"User-Agent", ("Lemuri/" + current()).toUtf8()}}, max, 600'000, &status, &part);
    } catch (...) {
        part.remove();
        throw;
    }
    part.close();
    if (status != 200) {
        part.remove();
        throw Fail{QString("GitHub: HTTP %1 for %2").arg(status).arg(QFileInfo(to).fileName())};
    }
    QFile::remove(to);
    if (!part.rename(to)) throw Fail{part.errorString()};
    QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
}

void PackageUpdater::verify(const QString &file, const QString &sig, const QString &signers) {
    QTemporaryFile allowed(QDir::tempPath() + "/lemuri-signers-XXXXXX");
    if (!allowed.open()) throw Fail{allowed.errorString()};
    allowed.write(signers.toUtf8());
    allowed.flush();
    Run who = run(30, {"ssh-keygen", "-Y", "find-principals", "-s", sig, "-f", allowed.fileName()});
    QString principal = who.code == 0 ? who.out.split('\n').value(0).trimmed() : QString();
    if (principal.isEmpty())
        throw Fail(I18n::t("update.error.unsigned"));
    Run ok = run(30, {"ssh-keygen", "-Y", "verify", "-f", allowed.fileName(), "-I", principal, "-n", "file", "-s", sig}, file);
    if (ok.code != 0)
        throw Fail{I18n::t("update.error.badSignature")};
}

void PackageUpdater::restartAfterExit() const {
    // The app (the parent of this backend) and this backend end first: the new copy must not find the old one still
    // holding the single-copy socket.
    QString app = testing ? "true" : "/usr/bin/lemuri";
    QProcess p;
    p.setProgram("setsid");
    // A process is told by its id and its start time (/proc/<pid>/stat, after the name): an id reused by another
    // process does not keep the wait going, and after 60 s the new copy starts anyway.
    static const char *script =
        "started() { sed 's/.*) //' \"/proc/$1/stat\" 2>/dev/null | cut -d' ' -f20; }\n"
        "a=$(started \"$1\"); b=$(started \"$2\"); n=0\n"
        "alive() { [ -n \"$2\" ] && [ \"$(started \"$1\")\" = \"$2\" ]; }\n"
        "while { alive \"$1\" \"$a\" || alive \"$2\" \"$b\"; } && [ $n -lt 200 ]; do sleep 0.3; n=$((n + 1)); done\n"
        "exec \"$3\"\n";
    p.setArguments({"-f", "/bin/sh", "-c", script, "lemuri-restart", QString::number(::getpid()), QString::number(::getppid()), app});
    p.setStandardInputFile(QProcess::nullDevice());
    p.setStandardOutputFile(QProcess::nullDevice());
    p.setStandardErrorFile(QProcess::nullDevice());
    if (!p.startDetached()) throw Fail{"setsid: " + p.errorString()};
}

// ── The ops ──

Updates::Updates(Events &events, Loop &loop) : events(events), loop(loop) {}

void Updates::ops(Router &router) {
    router.op("update.state", [this](const QJsonObject &) { return state(); });
    router.op("update.start", [this](const QJsonObject &) {
        launch();
        return state();
    });
    router.op("update.automatic", [this](const QJsonObject &m) {
        if (m.contains("on")) Prefs(NODE).put("automatic", Params::flag(m, "on") ? "true" : "false");
        return QJsonValue(automatic());
    });
    router.later("update.check", [this](const QJsonObject &, const Reply &r) { check(r); });
    router.op("update.skip", [this](const QJsonObject &m) {
        Prefs(NODE).put("skipped", Params::need(m, "version"));
        offered.reset();
        publish();
        return QJsonValue();
    });
    router.op("update.later", [this](const QJsonObject &) {
        offered.reset();
        publish();
        return QJsonValue();
    });
    router.later("update.blocker", [this](const QJsonObject &, const Reply &r) {
        // It runs programs (dpkg-query, pkexec): away from the main thread.
        loop.work([this, r] {
            QString b = service.blocker();
            r.ok(b.isNull() ? QJsonValue() : QJsonValue(b));
        });
    });
    router.later("update.install", [this](const QJsonObject &, const Reply &r) { install(r); });
    // The new copy starts once this one has ended: the frontend quits right after the answer.
    router.op("update.restart", [this](const QJsonObject &) {
        service.restartAfterExit();
        return QJsonValue();
    });
}

/** A check now, asked by hand: the answer is the release found, or null. */
void Updates::check(const Reply &r) {
    checking = true;
    publish();
    loop.work([this, r] {
        std::optional<Release> found;
        QString error;
        try {
            found = fetchOrFail();
        } catch (const Fail &f) {
            error = f.why;
        }
        loop.post([this, r, found, error] {
            checking = false;
            publish();
            if (!error.isNull()) r.fail(error);
            else r.ok(found ? QJsonValue(found->json()) : QJsonValue());
        });
    });
}

/** The steps go out as "updateStep" events; the answer comes when it is installed. */
void Updates::install(const Reply &r) {
    if (!available) throw Fail{"No update available"};
    Release release = *available;
    loop.work([this, r, release] {
        try {
            service.install(release, [this](const QString &step) { events.event("updateStep", {{"step", step}}); });
            r.ok();
        } catch (const Fail &f) {
            r.fail(f.why);
        }
    });
}

bool Updates::automatic() const {
#ifdef LEMURI_NO_SELF_UPDATE
    // Updated by a package manager: no daily call to GitHub unless the user turns it on.
    return Prefs(NODE).getBool("automatic", false);
#else
    return Prefs(NODE).getBool("automatic", true);
#endif
}

QJsonObject Updates::state() const {
    return {{"current", service.current()},
            {"available", available ? QJsonValue(available->json()) : QJsonValue()},
            {"offered", offered ? QJsonValue(offered->json()) : QJsonValue()},
            {"checking", checking},
            {"automatic", automatic()},
            {"lastCheck", Prefs(NODE).getLong("lastCheck", 0)}};
}

void Updates::publish() {
    events.event("update", state());
}

/** The daily check: the first one a little after startup, so that it does not slow it down. */
void Updates::launch() {
    if (launched) return;
    launched = true;
    Prefs p(NODE);
    QString tag = p.get("releaseTag");
    if (tag.startsWith("v") && Versions::valid(tag.mid(1)) && Versions::compare(tag.mid(1), service.current()) > 0) {
        Release r{tag, tag.mid(1), p.get("releaseNotes"), p.get("releaseUrl")};
        available = r;
        if (automatic() && r.version != p.get("skipped")) offered = r;
    }
    auto round = [this] {
        if (!automatic() || QDateTime::currentMSecsSinceEpoch() - Prefs(NODE).getLong("lastCheck", 0) < DAY_MS) return;
        loop.work([this] {
            try {
                std::optional<Release> r = fetchOrFail();
                if (r)
                    loop.post([this, r] {
                        if (r->version != Prefs(NODE).get("skipped")) {
                            offered = r;
                            publish();
                        }
                    });
            } catch (const Fail &f) {
                std::fprintf(stderr, "Update check failed: %s\n", qPrintable(f.why));
            }
        });
    };
    QTimer::singleShot(20'000, &loop, round);
    timer = new QTimer(&loop);
    timer->setInterval(3600'000);
    QObject::connect(timer, &QTimer::timeout, &loop, round);
    timer->start();
}

std::optional<Release> Updates::fetchOrFail() {
    std::optional<Release> r = service.latest();
    if (r && Versions::compare(r->version, service.current()) <= 0) r.reset();
    QString notes = r ? r->notes : QString();
    Prefs(NODE).change({{"lastCheck", QString::number(QDateTime::currentMSecsSinceEpoch())},
                        {"releaseTag", r ? r->tag : QString("")},
                        {"releaseUrl", r ? r->url : QString("")},
                        {"releaseNotes", notes.size() > 6000 ? notes.left(6000) + "\n…" : notes}});
    loop.post([this, r] {
        available = r;
        if (!r) offered.reset();
        publish();
    });
    return r;
}
