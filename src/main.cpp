#include "app/launch/Instance.h"
#include "app/tools/Shots.h"
#include "app/launch/Startup.h"
#include "widgets/look/Style.h"
#include "widgets/look/Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>

namespace {
/** The dark theme for the standard controls too (dialogs, menus, tooltips), in the app's colours. */
void darkTheme(QApplication &app) {
    app.setStyle(new Style);
    QPalette p;
    p.setColor(QPalette::Window, Theme::headPanel);
    p.setColor(QPalette::WindowText, Theme::text);
    p.setColor(QPalette::Base, Theme::bg);
    p.setColor(QPalette::AlternateBase, Theme::control);
    p.setColor(QPalette::Text, Theme::text);
    p.setColor(QPalette::Button, Theme::border);
    p.setColor(QPalette::ButtonText, Theme::text);
    p.setColor(QPalette::Highlight, Theme::accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, Theme::headPanel);
    p.setColor(QPalette::ToolTipText, Theme::text);
    p.setColor(QPalette::PlaceholderText, Theme::faint);
    p.setColor(QPalette::Disabled, QPalette::Text, Theme::disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, Theme::disabled);
    app.setPalette(p);
    // The interface font: "Helvetica Neue", 12 px (Noto Sans where it is missing).
    QFont f("Helvetica Neue");
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(12);
    app.setFont(f);
}
}  // namespace

int main(int argc, char *argv[]) {
    // On XWayland: on Wayland a window cannot place itself, and ghost mode docks to the sides.
    if (qgetenv("XDG_SESSION_TYPE") == "wayland" && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);
    // The window stays when hidden to the tray; quitting is explicit (tray menu, or the X without a tray).
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("lemuri");
    app.setDesktopFileName("lemuri");
    darkTheme(app);

    // "lemuri --toggle" (a global shortcut, F11 for example) or a second start: the running copy does it.
    // Pictures without a screen run beside the real app and keep to themselves.
    QString shot = Shots::file();
    bool toggle = app.arguments().contains("--toggle");
    if (shot.isEmpty() && Instance::handOver(toggle ? "toggle" : "show")) return 0;

    Startup start(shot);
    if (shot.isEmpty()) Instance::listen(&app, [&start](const QString &command) { start.fromOutside(command); });
#ifdef Q_OS_MACOS
    // A click on the Dock icon while every window is hidden (the X hides to the menu bar icon): back in view.
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &app, [&start](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) start.reopen();
    });
#endif
    start.begin();
    return app.exec();
}
