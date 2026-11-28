#include "app/launch/Startup.h"

#include "app/launch/Dock.h"
#include "app/launch/Instance.h"
#include "app/tools/Shots.h"
#include "app/tools/Texts.h"
#include "window/main/MainWindow.h"
#include "widgets/parts/Ui.h"
#include "window/main/Splash.h"
#include "widgets/look/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QLabel>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>

Startup::Startup(QString shot) : shot(std::move(shot)) {
    clock.start();
    // The texts of the last run, kept on disk: the window is built at once in the right language. The backend's
    // fresh copy replaces them for the next run (a new version may have new texts).
    cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/texts.json";
    QFile cached(cache);
    if (cached.open(QIODevice::ReadOnly)) saved = QJsonDocument::fromJson(cached.readAll()).object();
}

Startup::~Startup() = default;

void Startup::begin() {
    // The backend starts first: it gets ready while the window is being built.
    backend.start();
    // The splash screen starts at once; the main window is built behind it and shows when it ends.
    splashing = shot.isEmpty() && Splash::wanted();
    if (splashing)
        new Splash([this] {
            splashing = false;
            if (!window) return;
            window->show();
            Instance::activate(window.get());
        });
    if (!saved.isEmpty()) {
        Texts::load(saved["texts"].toObject(), saved["language"].toString());
        open();
    }
    backend.request("hello", {}, [this](const QJsonValue &ok, const QString &) { hello(ok.toObject()); });
}

void Startup::fromOutside(const QString &command) {
    if (window) window->fromOutside(command == "toggle");
}

void Startup::reopen() {
    if (!window) return;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (w->isVisible()) return;
    window->fromOutside(false);
}

void Startup::open() {
    window = std::make_unique<MainWindow>(&backend, &model);
    window->setWindowIcon(QIcon(":/logo.png"));
    if (!splashing) window->show();
    if (!said.isEmpty()) window->hello(said);
    std::fprintf(stderr, "window shown after %lld ms\n", static_cast<long long>(clock.elapsed()));
    if (!shot.isEmpty()) Shots::take(window.get(), &model, &backend, shot);
}

void Startup::hello(const QJsonObject &h) {
    said = h;
    if (window) window->hello(h);
    QString language = h["language"].toString();
    backend.request("texts", {}, [this, language](const QJsonValue &all, const QString &error) {
        if (error.isEmpty()) texts(all, language);
    });
}

void Startup::texts(const QJsonValue &all, const QString &language) {
    QDir().mkpath(QFileInfo(cache).path());
    QFile out(cache);
    if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(QJsonObject{{"language", language}, {"texts", all}}).toJson(QJsonDocument::Compact));
    // No window yet (the first start), or one built with other texts: another language, or the cached ones of the
    // version before an update (its new keys would show as !key!). Built again with these texts.
    if (!window || saved["language"].toString() != language || saved["texts"] != all) {
        Texts::load(all.toObject(), language);
        window.reset();
        open();
    }
    startWork();
    backend.request("prefs.get", {{"node", "ui"}}, [this](const QJsonValue &p, const QString &) {
        if (p.toObject()["splash"].isString()) Splash::remember(p.toObject()["splash"].toString() != "false");
        if (p.toObject()["theme"].isString()) Theme::remember(p.toObject()["theme"].toString());
        if (window) window->prefs(p.toObject());
        if (shot.isEmpty() && p.toObject()["dockAsked"].toString() != "true") offerDock();
    });
}

void Startup::offerDock() {
    // Once the window is in view, past the splash screen; asked once, whatever the answer.
    auto *wait = new QTimer(&model);
    QObject::connect(wait, &QTimer::timeout, &model, [this, wait] {
        if (splashing || !window || !window->isVisible() || !window->isActiveWindow()) return;
        wait->deleteLater();
        if (!Dock::available()) return;
        if (!Dock::pinned()) {
            FramedDialog d(window.get(), T("dock.title"));
            auto *text = new QLabel(T("dock.question"));
            text->setWordWrap(true);
            text->setMinimumWidth(380);
            d.body()->addWidget(text);
            d.buttons(T("dock.add"), false, T("dock.notNow"))->setFocus();
            if (d.exec() == QDialog::Accepted && !Dock::pin(true)) Ui::warning(window.get(), T("dock.title"), T("settings.dock.failed"));
        }
        backend.request("prefs.put", {{"node", "ui"}, {"values", QJsonObject{{"dockAsked", "true"}}}});
    });
    wait->start(1500);
}

void Startup::startWork() {
    backend.request("hosts", {}, [this](const QJsonValue &h, const QString &) {
        emit backend.event("hosts", h.toObject());
        // A picture of the window touches no server and no network.
        if (shot.isEmpty()) {
            backend.request("monitor.start");
            backend.request("team.start");
            backend.request("update.start", {}, [this](const QJsonValue &u, const QString &) { emit backend.event("update", u.toObject()); });
        }
        // A local shell tab is ready at startup.
        if (shot.isEmpty() || qEnvironmentVariableIsSet("LEMURI_SHOT_LOCAL")) model.openLocal();
    });
}
