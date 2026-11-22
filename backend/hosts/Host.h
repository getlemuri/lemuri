#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

/**
 * A server reachable over SSH, as hosts.json keeps it: id, name, group (folder), address,
 * port, user, authMethod (KEY, PASSWORD, AGENT), key (a file in ~/.ssh), note, os, state (ONLINE, OFFLINE, UNKNOWN),
 * fromContact, received, jump (ProxyJump hops). Kept as JSON, so fields this version does not know survive.
 */
struct Host {
    enum class Auth { Key, Password, Agent };
    /** Whether the last probe reached its SSH port. */
    enum class Reach { Online, Offline, Unknown };
    /** As hosts.json writes it. */
    static QString name(Reach r);

    QJsonObject json;

    /** From hosts.json or the frontend: the Italian names of the first versions read, the defaults filled in. */
    static Host from(const QJsonObject &o);
    /** As saved and sent: no null fields, as Gson writes it. */
    QJsonObject saved() const;

    QString id() const { return json["id"].toString(); }
    QString name() const { return json["name"].toString(); }
    QString group() const { return json["group"].toString(); }
    QString address() const { return json["address"].toString(); }
    int port() const { return json["port"].toInt(22); }
    QString user() const { return json["user"].toString("root"); }
    /** As saved: KEY, PASSWORD or AGENT. */
    QString authMethod() const { return json["authMethod"].toString("KEY"); }
    Auth auth() const;
    QString key() const { return json["key"].toString(); }
    QString jump() const { return json["jump"].toString(); }
    QString os() const { return json["os"].toString(); }
    Reach reach() const;
    bool received() const { return json["received"].toBool(); }
    /** user@address:port */
    QString destination() const;
    void set(const QString &field, const QJsonValue &v) { json[field] = v; }
};
