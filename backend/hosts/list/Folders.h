#pragma once

#include <QString>

/** Paths of the folders that hold the hosts: segments separated by "/", like "Production/EU/web"; "" is the datacenter. */
namespace Folders {
/** Removes extra spaces and slashes: " /Prod//EU/ " becomes "Prod/EU". */
QString normalize(const QString &p);
QString parent(const QString &p);
QString name(const QString &p);
QString merge(const QString &parent, const QString &name);
/** True if `p` is `folder` or lies inside it, at any depth. */
bool inside(const QString &p, const QString &folder);
/** Moves `p` from under `old` to under `created`; other paths stay as they were. */
QString rename(const QString &p, const QString &old, const QString &created);
QString label(const QString &p);
}  // namespace Folders
