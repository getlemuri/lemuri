#pragma once

#include <QString>
#include <functional>

class QObject;
class QRect;
class QScreen;
class QWidget;

/**
 * One copy of the app per user: a second start (the menu icon, "lemuri
 * --toggle" on a global shortcut) hands its command to the running copy and quits. A local socket in the user's
 * runtime folder carries one line: "show" or "toggle".
 */
namespace Instance {
/** If a copy is already running, gives it `command` and returns true. */
bool handOver(const QString &command);
/** Takes the commands of later starts. */
void listen(QObject *parent, std::function<void(const QString &)> on);
/**
 * Brings `w` to the front with the keyboard. On X11 as a pager would ask: GNOME refuses a plain request from
 * an app that has not just been clicked (it shows "Lemuri is ready" instead), which is what a shortcut or the
 * tray is.
 */
void activate(QWidget *w);
/**
 * The part of `s` not taken by the desktop's panels (on Ubuntu the top bar and the dock). Under X11 Qt gives
 * the whole screen: there it is read from the work areas the window manager publishes for every monitor
 * (_GTK_WORKAREAS_D<n>, GNOME), else from the one for all of them (_NET_WORKAREA).
 */
QRect workArea(QScreen *s);
}  // namespace Instance
