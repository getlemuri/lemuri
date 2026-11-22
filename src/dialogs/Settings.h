#pragma once

#include <QJsonObject>

class Actions;
class Model;
class QWidget;

/** Encrypted backups of everything. */
namespace BackupDialogs {
void exportAll(Model *model, Actions *actions, QWidget *parent);
void importFrom(Model *model, Actions *actions, QWidget *parent);
}  // namespace BackupDialogs

/** A new version: what changes, and the update with its steps. */
namespace UpdateDialogs {
void offer(Model *model, QWidget *parent, const QJsonObject &release);
}  // namespace UpdateDialogs

/**
 * The settings: sections with a search on the left, the page on the right,
 * OK, Cancel and Apply. The choices take effect with Apply or OK; the buttons in the pages act at once.
 */
namespace SettingsDialog {
void open(Model *model, Actions *actions, QWidget *parent);
}  // namespace SettingsDialog
