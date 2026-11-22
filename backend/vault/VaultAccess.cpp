#include "vault/VaultAccess.h"

#include "base/core/Error.h"
#include "base/config/I18n.h"
#include "base/core/Params.h"
#include "base/core/Runtime.h"
#include "vault/Vault.h"

using Params::flag;
using Params::need;
using Params::str;

VaultAccess::VaultAccess(Vault &vault, Prompter &prompter, Events &events, Loop &loop) : v(vault), prompter(prompter) {
    auto changed = [this, &events, &loop] { loop.post([this, &events] { events.event("vault", state()); }); };
    v.onUnlock(changed);
    v.onLock(changed);
}

QJsonObject VaultAccess::state() const {
    return {{"exists", v.exists()}, {"open", v.isOpen()}};
}

void VaultAccess::requireOpen() const {
    if (!v.isOpen()) throw Fail(I18n::t("sharing.msg.vault"));
}

bool VaultAccess::unlock(const QString &why) {
    QString error;
    while (!v.isOpen()) {
        bool create = !v.exists();
        QJsonValue a = prompter.ask(create ? "vaultCreate" : "vault",
                                    {{"message", why}, {"error", error.isNull() ? QJsonValue() : QJsonValue(error)}});
        if (!a.isObject() || !a.toObject().contains("text")) return false;
        try {
            if (create) v.create(a["text"].toString());
            else if (!v.open(a["text"].toString())) error = I18n::t("vault.wrong");
        } catch (const Fail &f) {
            error = f.why;
        }
    }
    return true;
}

void VaultAccess::ops(Router &router) {
    router.op("vault.state", [this](const QJsonObject &) { return state(); });
    router.op("vault.create", [this](const QJsonObject &m) {
        if (v.exists() && !flag(m, "replace")) throw Fail("The vault exists already");
        v.create(need(m, "password"));
        return state();
    });
    router.op("vault.open", [this](const QJsonObject &m) {
        bool ok = v.exists() && v.open(need(m, "password"));
        QJsonObject r = state();
        r["ok"] = ok;
        return r;
    });
    router.op("vault.lock", [this](const QJsonObject &) {
        v.lock();
        return state();
    });
    router.op("vault.change", [this](const QJsonObject &m) {
        bool ok = v.changePassword(need(m, "current"), need(m, "password"));
        QJsonObject r = state();
        r["ok"] = ok;
        return r;
    });
    // Whether a host has a saved password (never the password itself).
    router.op("vault.has", [this](const QJsonObject &m) { return !v.password(str(m, "host")).isNull(); });
    router.op("vault.remember", [this](const QJsonObject &m) {
        requireOpen();
        v.remember(need(m, "host"), need(m, "password"));
        return QJsonValue();
    });
    router.op("vault.forget", [this](const QJsonObject &m) {
        requireOpen();
        v.forget(need(m, "host"));
        return QJsonValue();
    });
}
