#pragma once

#include "hosts/list/Host.h"

#include <QJsonObject>
#include <QString>

/**
 * What an envelope contains: the host without id or status, and maybe the password. A random
 * id and the time of sending sit INSIDE the encrypted and signed content, so a copy pasted again months later is seen.
 * Null strings are missing fields, as Gson leaves them.
 */
struct Parcel {
    QString name, address, user, os, password, message, id, authMethod;
    int port = 22;
    qint64 when = 0;

    static Parcel from(const Host &h, const QString &password, const QString &message);
    /** As Gson reads it: missing fields stay null, an unknown authMethod is null. *ok false if it is not an object. */
    static Parcel fromJson(const QString &json, bool *ok);
    QString toJson() const;
    /** Cleans the free texts, then says what is wrong with it, or null: the one way in for a host from a contact. */
    QString admit();
    /** What is wrong with an incoming parcel, or null if it is fine. Same bounds as the host dialog. */
    QString problem() const;
    /** The host for this PC: never with the sender's key path, which names a file on their PC. */
    Host asHost() const;

private:
    void clean();
};

/** The ids of the envelopes already accepted or rejected, in the vault: replays refused. */
namespace DecidedEnvelopes {
/** An envelope older than this is not accepted: nobody leaves a host waiting for months. */
constexpr qint64 MAX_AGE_MS = 30LL * 24 * 60 * 60 * 1000;
/** The i18n key of the reason to reject the parcel, or null if it can be offered. */
QString check(const Parcel &p, const QJsonObject &decided, qint64 now);
}  // namespace DecidedEnvelopes
