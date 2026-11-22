#include "app/tools/Texts.h"

#include <QHash>

namespace {
QHash<QString, QString> &all() {
    static QHash<QString, QString> h;
    return h;
}
}  // namespace

void Texts::load(const QJsonObject &texts, const QString &language) {
    all().clear();
    Q_UNUSED(language);
    for (auto it = texts.begin(); it != texts.end(); ++it) all().insert(it.key(), it.value().toString());
}

bool Texts::has(const QString &key) {
    return all().contains(key);
}

QString Texts::format(const QString &key, const QStringList &values) {
    auto it = all().constFind(key);
    if (it == all().constEnd()) return "!" + key + "!";
    const QString &s = *it;
    // MessageFormat: '' is an apostrophe, text between single quotes is literal, {n} is a value.
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
