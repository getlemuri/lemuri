// The ~/.ssh files of a backup, as an import judges and merges them (the backup's author may be anyone).
#include "backup/SshFolder.h"
#include "base/config/I18n.h"

#include <cstdint>

/** Outside the extern "C" function: there Q_INIT_RESOURCE would declare the C++ function with C linkage. */
static void initResources() {
    Q_INIT_RESOURCE(backend);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static bool ready = [] {
        initResources();
        I18n::choose("en");
        return true;
    }();
    (void)ready;
    QByteArray b(reinterpret_cast<const char *>(data), qsizetype(size));
    // Split in two: what is here already, and what arrives.
    qsizetype half = b.size() / 2;
    QByteArray here = b.left(half), arrived = b.mid(half);
    SshFolder::newKnownHosts(arrived);
    SshFolder::markerLines(arrived);
    SshFolder::missingLines(here, arrived);
    SshFolder::riskyDirectives(arrived);
    for (const char *name : {"known_hosts", "config", "id_ed25519", "id_ed25519.pub"}) SshFolder::kind(name, arrived);
    return 0;
}
