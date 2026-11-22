#pragma once

#include "hosts/list/Host.h"

#include <QList>

/**
 * Jump hosts (ssh's ProxyJump): from the text of a host's "jump" to the hosts to go through, in order. A hop is the
 * name of another host in the list, with its user, port, key and saved password (and its own jump hosts before
 * it), or [user@]host[:port], reached with ssh-agent and the keys in ~/.ssh.
 */
namespace Jumps {
struct Route {
    QList<Host> hops;
    /** Why the jump hosts cannot be resolved, or null. */
    QString error;
};
Route resolve(const Host &h, const QList<Host> &known);
/** [user@]host[:port], with IPv6 written as [::1]:22. Throws Fail if it is not one. */
Host parse(const QString &token);
}  // namespace Jumps
