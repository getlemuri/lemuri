#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

/**
 * The first versions saved preferences and some files under other names: the app was
 * called VpsMan (~/.vpsmanager, the "vpsmanager" preferences) and some keys were Italian. At startup, before anything
 * reads them, they move to today's names once; a value already saved under the new name wins. The vault and the JSON
 * files are converted where they are read, with the functions below.
 */
namespace Legacy {
inline const QString OLD_NAME = QStringLiteral("vpsmanager");
/** Everything below, for the user's home. Before ~/.lemuri is created. */
void migrate();
/** The old data folder takes the new name, if there is no new one yet. */
void folder(const QString &old, const QString &now);
/** The old preference tree (a folder of prefs.xml files) copied under the new name, values there winning, then removed. */
void tree(const QString &prefsRoot, const QString &old, const QString &now);
/** The old keys moved to their new names in the preferences under `prefsRoot`; some dropped. */
void preferences(const QString &prefsRoot);
/** Files of the first versions no longer used: the single-instance lock and the tray icon folder. */
void files(const QString &dataDir);

/** A field under today's name, else under the first versions' Italian one. */
QJsonValue field(const QJsonObject &o, const char *now, const char *old);
/** A host as hosts.json or a contact gives it: the Italian field names and values of the first versions renamed. */
QJsonObject host(QJsonObject o);
/** The authentication method: the first versions' CHIAVE is KEY. */
QString authMethod(const QString &m);
/** The vault file of the first versions, cassaforte.json in `dataDir`, takes the name `now` if that is free. */
void vaultFile(const QString &dataDir, const QString &now);
/** The vault's entries of the first versions renamed, those of the removed Firebase sharing dropped; true if any. */
bool vaultEntries(QHash<QString, QString> &secrets);
}  // namespace Legacy
