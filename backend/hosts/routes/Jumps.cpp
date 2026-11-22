#include "hosts/routes/Jumps.h"

#include "base/core/Error.h"
#include "base/system/Files.h"
#include "base/config/I18n.h"

#include <QRegularExpression>

namespace {
/** A chain longer than this is a loop. */
constexpr int MAX_HOPS = 8;

int port(const QString &colonPort, const QString &token) {
    bool ok = false;
    int p = colonPort.startsWith(':') ? colonPort.mid(1).toInt(&ok) : 0;
    if (!ok || p < 1 || p > 65535) throw Fail{I18n::t("jump.err.invalid", token)};
    return p;
}

void add(const QString &spec, const QList<Host> &known, QList<Host> &route, QStringList visiting, int depth) {
    for (const QString &raw : spec.split(',')) {
        QString token = raw.trimmed();
        if (token.isEmpty()) throw Fail{I18n::t("jump.err.empty")};
        if (route.size() >= MAX_HOPS || depth > MAX_HOPS) throw Fail{I18n::t("jump.err.tooMany", QString::number(MAX_HOPS))};
        const Host *named = nullptr;
        for (const Host &x : known)
            if (token.compare(x.name(), Qt::CaseInsensitive) == 0) {
                named = &x;
                break;
            }
        if (!named) {
            route << Jumps::parse(token);
            continue;
        }
        for (const QString &v : visiting)
            if (v.compare(token, Qt::CaseInsensitive) == 0) throw Fail{I18n::t("jump.err.loop", token)};
        // A host in the list may itself be behind other jump hosts: those come first.
        if (!named->jump().trimmed().isEmpty()) add(named->jump(), known, route, visiting + QStringList{named->name()}, depth + 1);
        Host hop = *named;
        hop.json.remove("jump");
        route << hop;
    }
}
}  // namespace

Jumps::Route Jumps::resolve(const Host &h, const QList<Host> &known) {
    Route r;
    if (h.jump().trimmed().isEmpty()) return r;
    try {
        add(h.jump(), known, r.hops, {h.name()}, 0);
    } catch (const Fail &f) {
        r.hops.clear();
        r.error = f.why;
    }
    return r;
}

Host Jumps::parse(const QString &token) {
    QString user = Files::localUser(), rest = token;
    qsizetype at = rest.lastIndexOf('@');
    if (at >= 0) {
        user = rest.left(at);
        rest = rest.mid(at + 1);
    }
    QString address = rest;
    int p = 22;
    if (rest.startsWith('[')) {
        qsizetype close = rest.indexOf(']');
        if (close < 0) throw Fail{I18n::t("jump.err.invalid", token)};
        address = rest.mid(1, close - 1);
        if (rest.size() > close + 1) p = port(rest.mid(close + 1), token);
    } else if (rest.count(':') == 1) {
        address = rest.left(rest.indexOf(':'));
        p = port(rest.mid(rest.indexOf(':')), token);
    }
    static const QRegularExpression plain("^[^\\s/@]+$");
    if (user.isEmpty() || address.isEmpty() || !plain.match(address).hasMatch()) throw Fail{I18n::t("jump.err.invalid", token)};
    return Host::from({{"name", token}, {"user", user}, {"address", address}, {"port", p}, {"authMethod", "AGENT"}});
}
