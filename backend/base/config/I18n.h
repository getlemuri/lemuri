#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>

/**
 * The interface texts, in i18n/messages*.properties: English as the base, plus the
 * translations (messages_it). MessageFormat patterns: values go in {0}, {1}… and an apostrophe is written twice.
 */
namespace I18n {
/** The languages that have a translation; the others use English. */
inline const QStringList LANGUAGES{"en", "it"};
/** Chooses the language: "auto" (the system's), "en" or "it". Call once at startup. */
void choose(const QString &choice);
QString language();
/** The text of the key with the values in place; a missing key shows as !key!. */
QString format(const QString &key, const QStringList &values);
/** Every key in the chosen language (the base fills the gaps of a translation). */
QJsonObject all();
/** A message received from another PC: translated if it is a key, otherwise shown as it is. */
QString ifKey(const QString &message);
/** The lines of a .properties file: key=value, # and ! comments, continuation lines, \\ escapes and \\uXXXX. */
QHash<QString, QString> parseProperties(const QByteArray &utf8);
/** MessageFormat: '' is an apostrophe, text between single quotes is literal, {n} is a value. */
QString messageFormat(const QString &pattern, const QStringList &values);

template <typename... A>
QString t(const char *key, const A &...values) {
    QStringList l;
    (l.append(QVariant(values).toString()), ...);
    return format(QString::fromLatin1(key), l);
}
}  // namespace I18n
