#pragma once

#include <QJsonObject>
#include <QString>
#include <functional>
#include <mutex>
#include <optional>

class QTimer;
class Events;
class Loop;
class Reply;
class Router;

/** A release on GitHub: tag (v0.2.0), version (0.2.0), notes (markdown) and its page. */
struct Release {
    QString tag, version, notes, url;
    QJsonObject json() const { return {{"tag", tag}, {"version", version}, {"notes", notes}, {"url", url}}; }
};

/**
 * What an install reports while it works. The steps are DOWNLOAD, VERIFY and INSTALL: each starts (`step`, with its
 * label) and ends (`done`, with what came of it: the size, the key, the versions). `log` gets the lines of what runs,
 * as they come: the output of apt above all. Called on the worker thread of the install.
 */
struct InstallProgress {
    enum Step { DOWNLOAD, VERIFY, INSTALL };
    std::function<void(Step, const QString &label, const QString &detail)> step;
    std::function<void(Step, const QString &detail)> done;
    std::function<void(const QString &line)> log;
};

/** Version numbers like 0.1.0 or 1.12.3. */
namespace Versions {
/** Only plain numbers: 0.2.0 yes, 0.2.0-rc1 no (they are not offered as updates). */
bool valid(const QString &v);
/** Negative if `a` comes before `b`; 0.2 and 0.2.0 are the same version. */
int compare(const QString &a, const QString &b);
}  // namespace Versions

/**
 * Updates of the installed Linux package (/opt/lemuri, from lemuri_VERSION_ARCH.deb), from the releases of
 * getlemuri/lemuri. The latest release counts only if it carries the package for
 * this processor and its signature (.deb.sig, ssh-keygen -Y sign -n file); the package is installed only if that
 * signature is by a key in package-signers and it really is lemuri at the release's version. apt installs it, with
 * the system password asked by pkexec.
 *
 * For the tests, environment variables replace the GitHub address (LEMURI_UPDATE_API), the signers
 * (LEMURI_UPDATE_SIGNERS) and the installing command (LEMURI_UPDATE_INSTALL, words separated by spaces, the package's
 * path added at the end). Thread-safe: the network and the processes run on worker threads.
 */
class PackageUpdater {
public:
    PackageUpdater();
    QString current() const;
    /** The latest release with a package for this computer, or nothing. Network; throws Fail. */
    std::optional<Release> latest();
    /** Why this copy cannot update itself (in the user's language), or null if it can. */
    QString blocker() const;
    /** Downloads the release's package, checks it and installs it, telling `progress`. Throws Fail. */
    void install(const Release &r, const InstallProgress &progress);
    /** Starts the app again once this backend and the app (its parent) have ended. */
    void restartAfterExit() const;

    /** The answer of the API: a release with the package for `arch` and its signature, and where to download them. */
    static std::optional<Release> parse(const QByteArray &json, const QString &arch, bool anyUrl, QJsonObject *assets);
    /** The signature must be by an allowed key, for the "file" namespace; the answer is that key's name. Throws Fail. */
    static QString verify(const QString &file, const QString &sig, const QString &signers);

private:
    QString signers() const;
    void download(const QString &url, const QString &to, qint64 max) const;

    const QString api;
    const bool testing;
    mutable std::mutex m;
    /** The package assets of the last release read: name -> download address. */
    QJsonObject assets;
};

/**
 * New versions: a check on GitHub once a day (it can
 * be turned off), one on request, the update of the package. Preferences in lemuri/updates. News goes out as
 * "update" events: available (newer than this one), offered (found by the automatic check and not skipped), checking.
 * Main thread.
 */
class Updates {
public:
    Updates(Events &events, Loop &loop);
    /** Registers its requests. */
    void ops(Router &router);

private:
    /** A check now, asked by hand: the answer is the release found, or null. */
    void check(const Reply &r);
    /**
     * The steps go out as "updateStep" events (index, step, detail) when they start and "updateStepDone" (index,
     * detail) when they end, the lines of apt as "updateLog" (line); the answer comes when it is installed.
     */
    void install(const Reply &r);
    QJsonObject state() const;
    void publish();
    void launch();
    /** On a worker thread: the latest release newer than this one, noted in the preferences. Throws Fail. */
    std::optional<Release> fetchOrFail();
    bool automatic() const;

    Events &events;
    Loop &loop;
    PackageUpdater service;
    std::optional<Release> available, offered;
    bool checking = false, launched = false;
    QTimer *timer = nullptr;
};
