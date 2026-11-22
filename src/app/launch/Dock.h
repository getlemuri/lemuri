#pragma once

#include <QString>
#include <QStringList>

#include <optional>

/**
 * Lemuri among the favourites of the GNOME dock (the bar on the left of Ubuntu), for the user running the app.
 * The package cannot do it: it installs as root, and the favourites belong to each user's session.
 */
namespace Dock {
/** The desktop entry of the installed app: what the dock shows. */
inline const QString ENTRY = "lemuri.desktop";

/** On GNOME, with gsettings and the installed desktop entry (a copy built from source has none). */
bool available();
/** Whether Lemuri is among the favourites. */
bool pinned();
/** Adds Lemuri at the end of the favourites, or takes it away. False if gsettings failed. */
bool pin(bool on);

/** The favourites in the text gsettings prints ("['a.desktop', 'b.desktop']", "@as []"), or nothing if unreadable. */
std::optional<QStringList> parse(const QString &printed);
/** The favourites as gsettings reads them back. */
QString print(const QStringList &apps);
} // namespace Dock
