#include "app/Dock.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
const QString SCHEMA = "org.gnome.shell";
const QString KEY = "favorite-apps";

/** gsettings with these arguments: what it printed, or nothing if it failed. */
std::optional<QString> gsettings(const QStringList &args) {
    QProcess p;
    p.start("gsettings", args);
    if (!p.waitForFinished(3000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        p.kill();
        return std::nullopt;
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

std::optional<QStringList> favourites() {
    auto printed = gsettings({"get", SCHEMA, KEY});
    if (!printed) return std::nullopt;
    return Dock::parse(*printed);
}
} // namespace

std::optional<QStringList> Dock::parse(const QString &printed) {
    QString s = printed.trimmed();
    if (s.startsWith("@as ")) s = s.mid(4).trimmed();
    if (!s.startsWith('[') || !s.endsWith(']')) return std::nullopt;
    QStringList apps;
    // Desktop entry names are plain: letters, digits, dots, dashes and underscores, never quotes.
    static const QRegularExpression item("'([^'\\\\]*)'");
    for (auto it = item.globalMatch(s); it.hasNext();) apps << it.next().captured(1);
    return apps;
}

QString Dock::print(const QStringList &apps) {
    QStringList quoted;
    for (const QString &a : apps) quoted << "'" + a + "'";
    return "[" + quoted.join(", ") + "]";
}

bool Dock::available() {
    if (!qgetenv("XDG_CURRENT_DESKTOP").toUpper().contains("GNOME")) return false;
    if (QStandardPaths::findExecutable("gsettings").isEmpty()) return false;
    if (QStandardPaths::locate(QStandardPaths::ApplicationsLocation, ENTRY).isEmpty()) return false;
    return favourites().has_value();
}

bool Dock::pinned() {
    auto apps = favourites();
    return apps && apps->contains(ENTRY);
}

bool Dock::pin(bool on) {
    auto apps = favourites();
    if (!apps) return false;
    if (apps->contains(ENTRY) == on) return true;
    if (on)
        apps->append(ENTRY);
    else
        apps->removeAll(ENTRY);
    return gsettings({"set", SCHEMA, KEY, print(*apps)}).has_value();
}
