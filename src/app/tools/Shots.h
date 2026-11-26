#pragma once

#include <QString>

class Backend;
class MainWindow;
class Model;

/**
 * Pictures of the window without a screen, to check the look (tests/shots.sh). With LEMURI_SHOT=file.png the app
 * shows a node (LEMURI_SHOT_NODE: dc, team, log, folder:<path>, host:<id>, console:<id>), opens a dialog
 * (LEMURI_SHOT_DO, see Shots.cpp; several separated by ";"), saves a picture of what is in front after LEMURI_SHOT_MS
 * (5000) and quits. LEMURI_SHOT_SIZE=WxH sets the size of the window; LEMURI_SHOT_DEMO says the backend (LEMURI_CORE)
 * is a demo that makes up metrics, updates and security, so the app may ask for them.
 */
namespace Shots {
/** The file to save the picture to; empty for a normal start. */
QString file();
/** Arms the timers for one picture of `window`. */
void take(MainWindow *window, Model *model, Backend *backend, const QString &file);
/** One action of LEMURI_SHOT_DO (see Shots.cpp), also a step of a film's script. */
void act(MainWindow *window, Model *model, Backend *backend, const QString &action);
}  // namespace Shots
