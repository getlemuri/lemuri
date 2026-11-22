#pragma once

#include <QJsonObject>
#include <QVariant>
#include <QString>
#include <QStringList>

/**
 * The interface texts, as the backend gives them (op "texts", from backend/i18n/messages*.properties):
 * MessageFormat patterns, so values go in {0}, {1}... and '' is an apostrophe.
 */
namespace Texts {
/** `language`: "en" or "it", for the few texts Qt gives by itself (Cancel). */
void load(const QJsonObject &all, const QString &language);
QString format(const QString &key, const QStringList &values);
/** Whether the texts have `key`: none at all before the backend has ever given them. */
bool has(const QString &key);
}  // namespace Texts

/** (Not "tr": inside a QObject that name is QObject::tr.) The text of `key` with the values in place; a missing key shows as !key!. */
template <typename... A>
QString T(const char *key, const A &...values) {
    QStringList l;
    (l.append(QVariant(values).toString()), ...);
    return Texts::format(QString::fromLatin1(key), l);
}

inline QString T(const QString &key) { return Texts::format(key, {}); }
