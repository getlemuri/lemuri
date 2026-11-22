#include "base/Prefs.h"

#include <QDir>
#include <QFile>
#include <QMutex>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {
QMutex lock;

QHash<QString, QString> load(const QString &file) {
    QHash<QString, QString> r;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return r;
    QXmlStreamReader x(&f);
    while (!x.atEnd()) {
        if (x.readNext() == QXmlStreamReader::StartElement && x.name() == QLatin1String("entry"))
            r.insert(x.attributes().value("key").toString(), x.attributes().value("value").toString());
    }
    return r;
}
}  // namespace

Prefs::Prefs(const QString &node) : node(node) {}

// The folder is the first Lemuri's: the two apps read each other's settings.
QString Prefs::root() {
    return QDir::homePath() + "/.java/.userPrefs";
}

QString Prefs::file() const {
    return root() + "/" + node + "/prefs.xml";
}

QString Prefs::get(const QString &key, const QString &otherwise) const {
    QMutexLocker l(&lock);
    auto all = load(file());
    return all.contains(key) ? all.value(key) : otherwise;
}

int Prefs::getInt(const QString &key, int otherwise) const {
    bool ok = false;
    int v = get(key).trimmed().toInt(&ok);
    return ok ? v : otherwise;
}

qint64 Prefs::getLong(const QString &key, qint64 otherwise) const {
    bool ok = false;
    qint64 v = get(key).trimmed().toLongLong(&ok);
    return ok ? v : otherwise;
}

bool Prefs::getBool(const QString &key, bool otherwise) const {
    QString v = get(key).trimmed().toLower();
    return v == "true" ? true : v == "false" ? false : otherwise;
}

QHash<QString, QString> Prefs::all() const {
    QMutexLocker l(&lock);
    return load(file());
}

void Prefs::put(const QString &key, const QString &value) {
    change({{key, value}});
}

void Prefs::remove(const QString &key) {
    change({}, {key});
}

void Prefs::change(const QHash<QString, QString> &values, const QStringList &removed) {
    QMutexLocker l(&lock);
    auto all = load(file());
    for (auto it = values.begin(); it != values.end(); ++it) all.insert(it.key(), it.value());
    for (const QString &k : removed) all.remove(k);
    QDir().mkpath(QFileInfo(file()).path());
    // As the first Lemuri writes it, so that it reads it back: the entries sorted, its DOCTYPE, two spaces.
    QSaveFile f(file());
    if (!f.open(QIODevice::WriteOnly)) return;
    QXmlStreamWriter x(&f);
    x.setAutoFormatting(true);
    x.setAutoFormattingIndent(2);
    f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
            "<!DOCTYPE map SYSTEM \"http://java.sun.com/dtd/preferences.dtd\">\n");
    x.writeStartElement("map");
    x.writeAttribute("MAP_XML_VERSION", "1.0");
    QStringList keys = all.keys();
    keys.sort();
    for (const QString &k : keys) {
        x.writeEmptyElement("entry");
        x.writeAttribute("key", k);
        x.writeAttribute("value", all.value(k));
    }
    x.writeEndElement();
    x.writeEndDocument();
    f.commit();
}
