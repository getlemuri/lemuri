#include "hosts/list/Folders.h"

#include <QStringList>

QString Folders::normalize(const QString &p) {
    QStringList out;
    for (const QString &s : p.split('/')) {
        QString t = s.trimmed();
        if (!t.isEmpty()) out << t;
    }
    return out.join('/');
}

QString Folders::parent(const QString &p) {
    qsizetype i = p.lastIndexOf('/');
    return i < 0 ? QString("") : p.left(i);
}

QString Folders::name(const QString &p) {
    return p.mid(p.lastIndexOf('/') + 1);
}

QString Folders::merge(const QString &parent, const QString &name) {
    return parent.isEmpty() ? name : parent + "/" + name;
}

bool Folders::inside(const QString &p, const QString &folder) {
    return folder.isEmpty() || p == folder || p.startsWith(folder + "/");
}

QString Folders::rename(const QString &p, const QString &old, const QString &created) {
    if (p == old) return created;
    if (p.startsWith(old + "/")) return merge(created, p.mid(old.size() + 1));
    return p;
}

QString Folders::label(const QString &p) {
    return p.isEmpty() ? "Datacenter" : p;
}
