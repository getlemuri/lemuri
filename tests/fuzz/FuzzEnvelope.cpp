// Text pasted by the user: a host envelope or a key card, then the parcel inside it, as Team opens them.
#include "base/config/I18n.h"
#include "sharing/crypto/Envelope.h"
#include "sharing/crypto/Parcel.h"

#include <QCoreApplication>
#include <cstdint>

/** Outside the extern "C" function: there Q_INIT_RESOURCE would declare the C++ function with C linkage. */
static void initResources() {
    Q_INIT_RESOURCE(backend);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static Envelope::Keys mine = [] {
        initResources();
        I18n::choose("en");
        return Envelope::newKeys();
    }();
    QString text = QString::fromUtf8(reinterpret_cast<const char *>(data), qsizetype(size));
    try {
        TextEnvelope::parseKey(text);
        TextEnvelope::Posted p = TextEnvelope::unarmor(text);
        if (!p.isNull()) {
            bool ok = false;
            QString inside = Envelope::openFrom(p.sealed, mine, p.from, p.from, p.to, &ok);
            (void)inside;
        }
        // What an envelope carries, as if it had opened: the checks before a host is offered.
        bool parsed = false;
        Parcel parcel = Parcel::fromJson(text, &parsed);
        if (parsed) parcel.admit();
    } catch (const std::exception &) {
        // refused: fine
    }
    return 0;
}
