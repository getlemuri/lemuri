#pragma once

// Owners for what the C libraries hand out: each freed once, by its own function, on every way out of a scope.

#include <memory>
#include <unistd.h>
#include <utility>

/** A deleter calling `F`; nothing for null. */
template <auto F>
struct FreeWith {
    template <typename T>
    void operator()(T *p) const {
        if (p) F(p);
    }
};

/** A unique_ptr freed by `F`, e.g. `Owned<EVP_PKEY, EVP_PKEY_free>`. */
template <typename T, auto F>
using Owned = std::unique_ptr<T, FreeWith<F>>;

/** A file descriptor, closed when it goes; move-only. */
class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) : fd(fd) {}
    ~Fd() { reset(); }
    Fd(Fd &&o) noexcept : fd(std::exchange(o.fd, -1)) {}
    Fd &operator=(Fd &&o) noexcept {
        if (this != &o) {
            reset();
            fd = std::exchange(o.fd, -1);
        }
        return *this;
    }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;

    int get() const { return fd; }
    bool valid() const { return fd >= 0; }
    explicit operator bool() const { return valid(); }
    /** Gives the descriptor up: the caller closes it. */
    int release() { return std::exchange(fd, -1); }
    void reset(int to = -1) {
        if (fd >= 0) ::close(fd);
        fd = to;
    }

private:
    int fd = -1;
};
