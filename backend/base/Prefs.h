#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

/**
 * A node of the user's settings, in the file where the first Lemuri keeps it (~/.java/.userPrefs/<node>/prefs.xml,
 * an XML <map> of <entry key value>): the settings are shared with it. Read at every call (the first Lemuri may have
 * changed it) and written whole, through a temporary file; the first Lemuri notices the newer file by itself.
 */
class Prefs {
public:
    /** `node`: "lemuri/ui", "lemuri/updates"... */
    explicit Prefs(const QString &node);
    QString get(const QString &key, const QString &otherwise = {}) const;
    int getInt(const QString &key, int otherwise) const;
    qint64 getLong(const QString &key, qint64 otherwise) const;
    bool getBool(const QString &key, bool otherwise) const;
    QHash<QString, QString> all() const;
    void put(const QString &key, const QString &value);
    void remove(const QString &key);
    /** Several changes in one write. */
    void change(const QHash<QString, QString> &values, const QStringList &removed = {});

    /** The folder of the user's preferences (tests point HOME elsewhere). */
    static QString root();

private:
    QString file() const;
    QString node;
};
