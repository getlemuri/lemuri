#pragma once

#include <QString>
#include <cerrno>
#include <stdexcept>
#include <system_error>

/**
 * A request that cannot be done. `why` is shown to the user as it is, so it is already translated. Being a
 * `std::runtime_error`, one `catch (const std::exception &)` takes it with every other failure.
 */
class Fail : public std::runtime_error {
public:
    explicit Fail(const QString &why) : std::runtime_error(why.toStdString()), why(why) {}

    QString why;
};

/** The text of any exception: a `Fail`'s own, or `what()`. */
inline QString errorText(const std::exception &e) {
    if (auto *f = dynamic_cast<const Fail *>(&e)) return f->why;
    return QString::fromUtf8(e.what());
}

/** The system's text for an errno value, safe from any thread (std::strerror is not). */
inline QString systemError(int code = errno) {
    return QString::fromLocal8Bit(std::generic_category().message(code).c_str());
}
