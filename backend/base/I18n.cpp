#include "base/I18n.h"

#include <QFile>
#include <QLocale>

namespace {
QHash<QString, QString> texts;
QString chosen = "en";

QHash<QString, QString> read(const QString &file) {
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? I18n::parseProperties(f.readAll()) : QHash<QString, QString>();
}
}  // namespace

void I18n::choose(const QString &choice) {
    QString l = choice == "auto" || choice.isEmpty() ? QLocale::system().name().section('_', 0, 0) : choice.section('-', 0, 0);
    chosen = LANGUAGES.contains(l) ? l : "en";
    texts = read(":/i18n/messages.properties");
    if (chosen != "en") {
        const auto translated = read(":/i18n/messages_" + chosen + ".properties");
        for (auto it = translated.begin(); it != translated.end(); ++it) texts.insert(it.key(), it.value());
    }
}

QString I18n::language() {
    return chosen;
}

QString I18n::format(const QString &key, const QStringList &values) {
    if (texts.isEmpty()) choose("auto");
    auto it = texts.constFind(key);
    return it == texts.constEnd() ? "!" + key + "!" : messageFormat(*it, values);
}

QJsonObject I18n::all() {
    if (texts.isEmpty()) choose("auto");
    QJsonObject r;
    for (auto it = texts.begin(); it != texts.end(); ++it) r[it.key()] = it.value();
    return r;
}

QString I18n::ifKey(const QString &message) {
    if (texts.isEmpty()) choose("auto");
    return texts.contains(message) ? format(message, {}) : message;
}

QHash<QString, QString> I18n::parseProperties(const QByteArray &utf8) {
    QHash<QString, QString> r;
    const QStringList lines = QString::fromUtf8(utf8).split('\n');
    QString logical;
    for (int i = 0; i < lines.size(); i++) {
        QString line = lines[i];
        if (line.endsWith('\r')) line.chop(1);
        QString trimmed = logical.isEmpty() ? line.trimmed() : line.trimmed();
        if (logical.isEmpty() && (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith('!'))) continue;
        // A backslash at the end (not escaped) continues the line on the next one, without its leading spaces.
        int slashes = 0;
        for (int j = int(trimmed.size()) - 1; j >= 0 && trimmed[j] == '\\'; j--) slashes++;
        if (slashes % 2 == 1) {
            logical += trimmed.left(trimmed.size() - 1);
            continue;
        }
        logical += trimmed;
        // Key up to the first unescaped '=', ':' or space.
        QString key, value;
        int k = 0;
        bool escaped = false;
        for (; k < logical.size(); k++) {
            QChar c = logical[k];
            if (escaped) {
                key += c;
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '=' || c == ':' || c.isSpace()) {
                break;
            } else {
                key += c;
            }
        }
        while (k < logical.size() && logical[k].isSpace()) k++;
        if (k < logical.size() && (logical[k] == '=' || logical[k] == ':')) k++;
        while (k < logical.size() && logical[k].isSpace()) k++;
        for (; k < logical.size(); k++) {
            QChar c = logical[k];
            if (c != '\\' || k + 1 >= logical.size()) {
                value += c;
                continue;
            }
            QChar e = logical[++k];
            if (e == 'n') value += '\n';
            else if (e == 't') value += '\t';
            else if (e == 'r') value += '\r';
            else if (e == 'f') value += '\f';
            else if (e == 'u' && k + 4 < logical.size()) {
                value += QChar(logical.mid(k + 1, 4).toUShort(nullptr, 16));
                k += 4;
            } else value += e;
        }
        r.insert(key, value);
        logical.clear();
    }
    return r;
}

QString I18n::messageFormat(const QString &s, const QStringList &values) {
    QString out;
    out.reserve(s.size() + 16);
    bool quoted = false;
    for (qsizetype i = 0; i < s.size(); i++) {
        QChar c = s[i];
        if (c == '\'') {
            if (i + 1 < s.size() && s[i + 1] == '\'') {
                out += '\'';
                i++;
            } else {
                quoted = !quoted;
            }
            continue;
        }
        if (!quoted && c == '{') {
            qsizetype end = s.indexOf('}', i);
            bool ok = false;
            int n = end > i ? s.mid(i + 1, end - i - 1).section(',', 0, 0).trimmed().toInt(&ok) : -1;
            if (ok) {
                out += n < values.size() ? values[n] : QString("{%1}").arg(n);
                i = end;
                continue;
            }
        }
        out += c;
    }
    return out;
}
