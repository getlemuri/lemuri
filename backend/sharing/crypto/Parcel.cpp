#include "sharing/crypto/Parcel.h"

#include "base/config/Legacy.h"
#include "sharing/team/ContactNames.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

namespace {
constexpr qint64 DAY_MS = 24LL * 60 * 60 * 1000;

/**
 * Name, system and message are shown when the user decides whether to trust the host: zero-width and text direction
 * characters go, line breaks and other controls become spaces. The message keeps its line breaks.
 */
QString text(const QString &s, bool lines) {
    if (s.isNull()) return s;
    QList<uint> b;
    for (char32_t c : s.toUcs4()) {
        QChar::Category k = QChar::category(c);
        if (k == QChar::Other_Format || k == QChar::Other_PrivateUse || k == QChar::Other_NotAssigned || k == QChar::Other_Surrogate) continue;
        if (lines && c == '\n') b.append('\n');
        else if (k == QChar::Other_Control || k == QChar::Separator_Line || k == QChar::Separator_Paragraph) b.append(' ');
        else b.append(c);
    }
    return ContactNames::strip(QString::fromUcs4(reinterpret_cast<const char32_t *>(b.constData()), b.size()));
}

bool empty(const QString &s) {
    return s.isNull() || ContactNames::strip(s).isEmpty();
}

bool tooLong(const QString &s, int max) {
    return !s.isNull() && s.size() > max;
}

QString string(const QJsonObject &o, const char *k) {
    QJsonValue v = o[k];
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble());
    if (v.isBool()) return v.toBool() ? "true" : "false";
    return QString();
}
}  // namespace

Parcel Parcel::from(const Host &h, const QString &password, const QString &message) {
    Parcel p;
    p.name = h.name();
    p.address = h.address();
    p.port = h.port();
    p.user = h.user();
    p.authMethod = h.authMethod();
    p.os = h.json["os"].isString() ? h.os() : QString();
    p.password = password;
    p.message = message;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.when = QDateTime::currentMSecsSinceEpoch();
    return p;
}

Parcel Parcel::fromJson(const QString &json, bool *ok) {
    QJsonDocument d = QJsonDocument::fromJson(json.toUtf8());
    *ok = d.isObject();
    Parcel p;
    if (!*ok) return p;
    QJsonObject o = d.object();
    p.name = string(o, "name");
    p.address = string(o, "address");
    p.user = string(o, "user");
    p.os = string(o, "os");
    p.password = string(o, "password");
    p.message = string(o, "message");
    p.id = string(o, "id");
    if (o.contains("port") && !o["port"].isNull()) p.port = o["port"].isString() ? o["port"].toString().toInt() : o["port"].toInt();
    p.when = o["when"].isString() ? o["when"].toString().toLongLong() : qint64(o["when"].toDouble());
    QString a = string(o, "authMethod");
    a = Legacy::authMethod(a);
    p.authMethod = a == "KEY" || a == "PASSWORD" || a == "AGENT" ? a : QString();
    return p;
}

QString Parcel::toJson() const {
    QJsonObject o;
    auto put = [&](const char *k, const QString &v) {
        if (!v.isNull()) o[k] = v;
    };
    put("name", name);
    put("address", address);
    put("user", user);
    put("os", os);
    put("password", password);
    put("message", message);
    o["port"] = port;
    put("id", id);
    o["when"] = when;
    put("authMethod", authMethod);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString Parcel::admit() {
    clean();
    return problem();
}

void Parcel::clean() {
    name = text(name, false);
    os = text(os, false);
    message = text(message, true);
}

QString Parcel::problem() const {
    if (empty(name) || empty(address) || empty(user)) return "name, address and user are required";
    if (port < 1 || port > 65535) return QString("port out of range: %1").arg(port);
    if (name.size() > 64 || address.size() > 255 || user.size() > 64) return "name, address or user too long";
    // Not only the controls: also format (bidi, zero-width) and separator characters, which hide what is shown.
    static const QRegularExpression plain("\\A[^ \\t\\n\\x0B\\f\\r\\p{Cc}\\p{Cf}\\p{Z}]+\\z");
    if (!plain.match(address).hasMatch() || !plain.match(user).hasMatch()) return "address or user with spaces or control characters";
    // libssh reads "user@host" in the address, and an address or user starting with "-" looks like an option to ssh.
    if (address.contains('@') || address.startsWith('-') || user.contains('@') || user.startsWith('-')) return "address or user with @ or a leading -";
    if (tooLong(os, 200) || tooLong(password, 4096) || tooLong(message, 2000)) return "a field is too long";
    if (tooLong(id, 64)) return "id too long";
    return QString();
}

Host Parcel::asHost() const {
    QJsonObject o{{"name", name}, {"address", address}, {"port", port}, {"user", user},
                  {"authMethod", authMethod.isNull() ? QString("PASSWORD") : authMethod}};
    if (!os.isNull()) o["os"] = os;
    return Host::from(o);
}

QString DecidedEnvelopes::check(const Parcel &p, const QJsonObject &decided, qint64 now) {
    if (p.id.isNull() || p.when <= 0) return "sharing.envelope.old";
    if (now - p.when > MAX_AGE_MS || p.when - now > DAY_MS) return "sharing.envelope.tooOld";
    if (decided.contains(p.id)) return "sharing.envelope.replay";
    return QString();
}
