// A line from any PC of the local network, as Lan answers it: hello, delivery, anything else. One trusted contact
// ("contact" below) whose deliveries are sealed with a fixed key, so the fuzzer can reach past the envelope too.
#include "base/I18n.h"
#include "sharing/Envelope.h"
#include "sharing/Lan.h"

#include <QCoreApplication>
#include <cstdint>
#include <mutex>

/** Outside the extern "C" function: there Q_INIT_RESOURCE would declare the C++ function with C linkage. */
static void initResources() {
    Q_INIT_RESOURCE(backend);
}

namespace {
Lan *lan = nullptr;

void setUp() {
    static int argc = 1;
    static char name[] = "fuzz";
    static char *argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
    initResources();
    I18n::choose("en");
    // Any free ports, no announcements, preferences untouched.
    qputenv("LEMURI_LAN_PORTS", "0,0");
    qputenv("HOME", "/tmp/lemuri-fuzz-home");
    static Envelope::Keys mine = Envelope::newKeys();
    static std::mutex m;
    static QHash<QString, QString> vault;
    lan = new Lan({[] { return mine; },
                   [](const QString &k) {
                       std::lock_guard<std::mutex> l(m);
                       return vault.value(k);
                   },
                   [](const QString &k, const QString &v) {
                       std::lock_guard<std::mutex> l(m);
                       vault.insert(k, v);
                   },
                   [](const Delivery &) {}});
    lan->turnOn(true);
    lan->trust(Envelope::newKeys().publicKey, "contact");
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!lan) setUp();
    try {
        lan->answerLine(QByteArray(reinterpret_cast<const char *>(data), qsizetype(size)), "192.168.1.20");
    } catch (const std::exception &) {
        // a malformed line: the connection is dropped, which is the right outcome
    }
    return 0;
}
