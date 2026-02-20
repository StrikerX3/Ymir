#include <ymir/util/event.hpp>

// Implementation adapted from cppcoro's lightweight_manual_reset_event.
// See https://github.com/lewissbaker/cppcoro/blob/master/lib/lightweight_manual_reset_event.cpp

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <windows.h>
#elif defined(__linux__)
    #include <cassert>
    #include <cerrno>
    #include <climits>
    #include <linux/futex.h>
    #include <sys/syscall.h>
    #include <sys/time.h>
    #include <unistd.h>
#elif defined(__FreeBSD__)
    #include <cassert>
    #include <sys/umtx.h>
#endif

namespace util {

#if defined(_WIN32)

// Ymir requires Windows 10+, so we don't need to check for the old SetEvent/ResetEvent method.

Event::Event(bool set) noexcept
    : m_value(set ? 1u : 0u) {}

void Event::Wait() {
    // Wait in a loop as WaitOnAddress() can have spurious wake-ups.
    int value = m_value.load(std::memory_order_acquire);
    BOOL ok = TRUE;
    while (value == 0u) {
        if (!ok) {
            // Previous call to WaitOnAddress() failed for some reason.
            // Put thread to sleep to avoid sitting in a busy loop if it keeps failing.
            ::Sleep(1);
        }

        ok = ::WaitOnAddress(&m_value, &value, sizeof(m_value), INFINITE);
        value = m_value.load(std::memory_order_acquire);
    }
}

void Event::Set() {
    m_value.store(1u, std::memory_order_release);
    ::WakeByAddressAll(&m_value);
}

void Event::Reset() {
    m_value.store(0u, std::memory_order_relaxed);
}

#elif defined(__linux__)

namespace {
    namespace local {
        // No futex() function provided by libc.
        // Wrap the syscall ourselves here.
        int futex(int *UserAddress, int FutexOperation, int Value, const struct timespec *timeout, int *UserAddress2,
                  int Value3) {
            return syscall(SYS_futex, UserAddress, FutexOperation, Value, timeout, UserAddress2, Value3);
        }
    } // namespace local
} // namespace

Event::Event(bool set) noexcept
    : m_value(set ? 1 : 0) {}

void Event::Wait() {
    // Wait in a loop as futex() can have spurious wake-ups.
    int oldValue = m_value.load(std::memory_order_acquire);
    while (oldValue == 0) {
        int result = local::futex(reinterpret_cast<int *>(&m_value), FUTEX_WAIT_PRIVATE, oldValue, nullptr, nullptr, 0);
        if (result == -1) {
            if (errno == EAGAIN) {
                // The state was changed from zero before we could wait.
                // Must have been changed to 1.
                return;
            }

            // Other errors we'll treat as transient and just read the
            // value and go around the loop again.
        }

        oldValue = m_value.load(std::memory_order_acquire);
    }
}

void Event::Set() {
    m_value.store(1, std::memory_order_release);

    constexpr int numberOfWaitersToWakeUp = INT_MAX;

    [[maybe_unused]] int numberOfWaitersWokenUp = local::futex(reinterpret_cast<int *>(&m_value), FUTEX_WAKE_PRIVATE,
                                                               numberOfWaitersToWakeUp, nullptr, nullptr, 0);

    // There are no errors expected here unless this class (or the caller)
    // has done something wrong.
    assert(numberOfWaitersWokenUp != -1);
}

void Event::Reset() {
    m_value.store(0, std::memory_order_relaxed);
}

#elif defined(__FreeBSD__)

// Mirror the Linux implementation by replacing the Linux-specific futex(2)
// syscall with the equivalent FreeBSD-specific _umtx_op(2) libc interface.
// Intra-process futexes are only available for unsigned integers.

Event::Event(bool set) noexcept
    : m_value(set ? 1u : 0u) {}

void Event::Wait() {
    uint32 oldValue = m_value.load(std::memory_order_acquire);
    while (oldValue == 0u) {
        int result = _umtx_op(reinterpret_cast<uint32 *>(&m_value), UMTX_OP_WAIT_UINT_PRIVATE, oldValue, nullptr, nullptr);
        if (result == -1) {
            if (errno == EAGAIN) {
                return;
            }
        }

        oldValue = m_value.load(std::memory_order_acquire);
    }
}

void Event::Set() {
    m_value.store(1u, std::memory_order_release);

    constexpr int numberOfWaitersToWakeUp = INT_MAX;

    [[maybe_unused]] int numberOfWaitersWokenUp = _umtx_op(reinterpret_cast<uint32 *>(&m_value), UMTX_OP_WAKE_PRIVATE,
                                                               numberOfWaitersToWakeUp, nullptr, nullptr);

    assert(numberOfWaitersWokenUp != -1);
}

void Event::Reset() {
    m_value.store(0u, std::memory_order_relaxed);
}

#else

// condvar-based implementation entirely done on the header

#endif

} // namespace util
