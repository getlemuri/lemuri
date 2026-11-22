#include "hosts/Host.h"

#include "base/Legacy.h"
#include "hosts/Folders.h"

Host Host::from(const QJsonObject &o) {
    Host h;
    h.json = Legacy::host(o);
    auto otherwise = [&](const char *k, const QJsonValue &v) {
        if (!h.json.contains(k) || h.json[k].isNull()) h.json[k] = v;
    };
    otherwise("group", "");
    otherwise("port", 22);
    otherwise("user", "root");
    otherwise("authMethod", "KEY");
    otherwise("note", "");
    otherwise("state", "UNKNOWN");
    otherwise("received", false);
    h.json["group"] = Folders::normalize(h.json["group"].toString());
    // Never saved: worked out before each connection.
    h.json.remove("route");
    h.json.remove("routeError");
    return h;
}

QJsonObject Host::saved() const {
    QJsonObject r;
    for (auto it = json.begin(); it != json.end(); ++it)
        if (!it.value().isNull() && !it.value().isUndefined()) r[it.key()] = it.value();
    return r;
}

QString Host::destination() const {
    return QString("%1@%2:%3").arg(user(), address()).arg(port());
}

Host::Auth Host::auth() const {
    QString m = authMethod();
    return m == "PASSWORD" ? Auth::Password : m == "AGENT" ? Auth::Agent : Auth::Key;
}

Host::Reach Host::reach() const {
    QString s = json["state"].toString();
    return s == "ONLINE" ? Reach::Online : s == "OFFLINE" ? Reach::Offline : Reach::Unknown;
}

QString Host::name(Reach r) {
    switch (r) {
    case Reach::Online: return "ONLINE";
    case Reach::Offline: return "OFFLINE";
    case Reach::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}
