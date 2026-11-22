#pragma once

#include "hosts/Host.h"

#include <QList>
#include <QString>

/**
 * The hosts of ~/.ssh/config, read as ssh reads it: the first value obtained for an option wins, "Host *" blocks
 * apply to every host, negated patterns exclude, Include brings in other files; Match blocks are skipped. Only names
 * without wildcards become hosts.
 */
namespace SshConfig {
struct Entry {
    QString alias, hostName, user;  // user empty: the local user
    int port = 22;
    QString identityFile, proxyJump;  // identityFile already expanded; both empty if not set
    bool gitService() const;
    /** user@host:port, as shown in the import list. */
    QString destination() const;
    /** The host for Lemuri, in `group`: with an IdentityFile it uses that key, otherwise ssh-agent and ~/.ssh. */
    Host toHost(const QString &group) const;
};
QString file();
QList<Entry> read(const QString &path = file());
bool glob(const QString &pattern, const QString &text);
}  // namespace SshConfig
