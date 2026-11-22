// lemuri-core: the backend of Lemuri, started by the app (see Core and PROTOCOL.md).
#include "base/Files.h"
#include "base/I18n.h"
#include "base/Legacy.h"
#include "base/Log.h"
#include "base/Prefs.h"
#include "Core.h"
#include "ssh/Ssh.h"

#include <QCoreApplication>
#include <csignal>
#include <cstdio>
#include <unistd.h>

int main(int argc, char *argv[]) {
    // The texts are compiled into the backend's static library: registered by hand, or the linker drops them.
    Q_INIT_RESOURCE(backend);
    // A peer that goes away is an error on send, never the end of the backend.
    std::signal(SIGPIPE, SIG_IGN);
    QCoreApplication app(argc, argv);
    app.setApplicationName("lemuri-core");
    // The names of the first versions, before anything reads them.
    Legacy::migrate();
    // The language chosen in the settings (shared with the first Lemuri), or the system's.
    I18n::choose(Prefs("lemuri/ui").get("language", "auto"));
    Files::privateFolder(Files::data());
    Log::start();
    std::fprintf(stderr, "Lemuri backend %s started\n", LEMURI_VERSION);
    Ssh::init();
    int code;
    {
        Core core;
        code = core.run();
    }
    std::fflush(stdout);
    Log::stop();
    return code;
}
