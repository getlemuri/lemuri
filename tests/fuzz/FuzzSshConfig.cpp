// ~/.ssh/config, as the import of hosts reads it: Host blocks, patterns, Include, % tokens.
#include "base/config/I18n.h"
#include "hosts/routes/SshConfig.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdint>

/** Outside the extern "C" function: there Q_INIT_RESOURCE would declare the C++ function with C linkage. */
static void initResources() {
    Q_INIT_RESOURCE(backend);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // A home of its own: an Include of ~/... never reaches the real one.
    static QTemporaryDir home;
    static bool ready = [] {
        initResources();
        I18n::choose("en");
        qputenv("HOME", QFile::encodeName(home.path()));
        QDir().mkpath(home.path() + "/.ssh");
        return true;
    }();
    (void)ready;
    QString path = home.path() + "/.ssh/config";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 0;
    f.write(reinterpret_cast<const char *>(data), qint64(size));
    f.close();
    for (const SshConfig::Entry &e : SshConfig::read(path)) e.toHost("imported");
    return 0;
}
