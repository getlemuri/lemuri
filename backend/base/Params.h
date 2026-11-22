#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

/** Reading the fields of a request. A missing required field is a `Fail`. */
namespace Params {
/** The field as text (a number or a boolean too); null if absent. */
QString str(const QJsonObject &m, const QString &k);
/** As str(), but the field must be there. */
QString need(const QJsonObject &m, const QString &k);
bool flag(const QJsonObject &m, const QString &k);
int integer(const QJsonObject &m, const QString &k, int otherwise);
QStringList strings(const QJsonObject &m, const QString &k);
}  // namespace Params
