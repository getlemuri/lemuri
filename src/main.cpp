#include "app/launch/Instance.h"
#include "app/tools/Shots.h"
#include "app/launch/Startup.h"
#include "widgets/look/Style.h"
#include "widgets/look/Theme.h"
#include "widgets/parts/Ui.h"

#include <QApplication>
#include <QFont>
#include <QPalette>

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
/** The dark theme for the standard controls too (dialogs, menus, tooltips), in the app's colours. */
void applyTheme(QApplication &app) {
    app.setStyle(new Style);
    app.setPalette(Theme::palette());
    // The interface font: "Helvetica Neue", 12 px (Noto Sans where it is missing).
    QFont f("Helvetica Neue");
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(12);
    app.setFont(f);
}

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
/**
 * Typed in a terminal, it lets go of it: the prompt comes back at once and closing the terminal leaves the app running.
 * Started from the menu (no terminal), with --foreground, or for the site's pictures (the script waits for it), it stays.
 * Before QApplication: forking a process that already has threads is not safe.
 */
void detach(int argc, char *argv[]) {
    if (!isatty(STDIN_FILENO) && !isatty(STDOUT_FILENO) && !isatty(STDERR_FILENO)) return;
    if (qEnvironmentVariableIsSet("LEMURI_SHOT")) return;
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "--foreground") == 0) return;
    pid_t pid = fork();
    if (pid < 0) return;  // No fork: it simply runs attached.
    if (pid > 0) _exit(0);
    setsid();
    int null = open("/dev/null", O_RDWR);
    if (null >= 0) {
        dup2(null, STDIN_FILENO);
        dup2(null, STDOUT_FILENO);
        dup2(null, STDERR_FILENO);
        if (null > STDERR_FILENO) close(null);
    }
}
#endif
}  // namespace

int main(int argc, char *argv[]) {
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    detach(argc, argv);
#endif
    // On XWayland: on Wayland a window cannot place itself, and ghost mode docks to the sides.
    if (qgetenv("XDG_SESSION_TYPE") == "wayland" && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);
    // The window stays when hidden to the tray; quitting is explicit (tray menu, or the X without a tray).
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("lemuri");
    app.setDesktopFileName("lemuri");
    // The theme chosen in the settings, before any window: its colours are read as the windows are made.
    // LEMURI_THEME: another one for this run only (the pictures of each theme).
    Theme::apply(qEnvironmentVariable("LEMURI_THEME", Theme::saved()));
    applyTheme(app);
    Theme::style(nullptr, Ui::menus());

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
