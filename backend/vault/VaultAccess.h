#pragma once

#include <QJsonObject>
#include <QString>

class Events;
class Loop;
class Prompter;
class Router;
class Vault;

/**
 * The vault as the rest of the backend uses it: its requests (vault.*), its "vault" event when it opens or locks,
 * and opening it on demand by asking the user for the master password.
 */
class VaultAccess {
public:
    VaultAccess(Vault &vault, Prompter &prompter, Events &events, Loop &loop);
    void ops(Router &router);

    Vault &vault() const { return v; }
    /** Throws `Fail` if the vault is locked. */
    void requireOpen() const;
    /**
     * From a worker thread: opens the vault asking the master password, or creates it the first time; false if the
     * user gave up.
     */
    bool unlock(const QString &why);
    QJsonObject state() const;

private:
    Vault &v;
    Prompter &prompter;
};
