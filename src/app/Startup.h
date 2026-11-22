#pragma once

#include "app/Backend.h"
#include "app/Model.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <memory>

class MainWindow;

/**
 * The start of the app: the backend, the model, the main window built at once with the texts of the last run (or
 * as soon as the backend gives them), the splash screen in front of it, then the hosts and the background work.
 */
class Startup {
public:
    /** `shot`: the file of a picture without a screen (see Shots), or empty. */
    explicit Startup(QString shot);
    ~Startup();
    /** From here on the event loop does the rest. */
    void begin();
    /** A command from another start of the app ("show" or "toggle"). */
    void fromOutside(const QString &command);
    /** Back in view (the Dock icon on a Mac): only if nothing is visible. */
    void reopen();

private:
    void open();
    void hello(const QJsonObject &h);
    void texts(const QJsonValue &all, const QString &language);
    /** The hosts, then the probe and the readings, the sharing and the daily update check. */
    void startWork();
    /** The first time on GNOME: asks whether Lemuri goes among the favourites of the dock (see Dock). */
    void offerDock();

    QString shot, cache;
    QJsonObject saved, said;
    QElapsedTimer clock;
    Backend backend;
    Model model{&backend};
    std::unique_ptr<MainWindow> window;
    bool splashing = false;
};
