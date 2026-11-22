#pragma once

#include <QByteArray>
#include <QString>

/** App data in ~/.lemuri: readable by the user only, replaced in one go (an interruption leaves the previous file). */
namespace Files {
/** The user the app runs as (whose ~/.lemuri it is): $USER, $LOGNAME, else the system's account; empty if none. */
QString localUser();
/** ~/.lemuri (tests point HOME elsewhere). */
QString data();
/** Writes through a file beside it, born with permissions 600, then renamed over it. False if it failed. */
bool writePrivate(const QString &file, const QByteArray &bytes, QString *error = nullptr);
/** Creates the folder if missing and lets only the user open it. */
void privateFolder(const QString &dir);
}  // namespace Files
