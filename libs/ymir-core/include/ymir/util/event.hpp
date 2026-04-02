#pragma once

/**
@file
@brief Defines `util::Event`, a synchronization primitive that blocks threads until a signal is raised.
*/

#if defined(_WIN32)
    #include <ymir/core/types.hpp>

    #include <atomic>
#elif defined(__linux__)
    #include <atomic>
#elif defined(__FreeBSD__)
    #include <ymir/core/types.hpp>

    #include <atomic>
#else
    #include <condition_variable>
    #include <mutex>
#endif

namespace util {

/// @brief An event that blocks threads until it is signaled.
///
/// Example usage:
///
/// ```cpp
/// // A simple blocking queue implementation using two Events signaling the queue is not full or not empty.
/// // The consumer thread is blocked while the queue is empty.
/// // The producer thread is blocked while the queue is full.
/// template <typename T>
/// class BlockingQueue {
///     // Inserts a value in the queue.
///     void Offer(T value) {
///         // Wait until the queue is not full.
///         m_queueNotFullEvent.Wait();
///
///         // Push the element to the queue
///         // NOTE: synchronization omitted for brevity
///         m_queue.offer(value);
///
///         // Signal not empty event now that there is at least one element on the queue.
///         // This will unblock all threads waiting for this event to be signaled.
///         m_queueNotEmptyEvent.Set();
///     }
///
///     // Removes a value from the queue.
///     T Poll() {
///         // Wait until the queue is not empty.
///         m_queueNotEmptyEvent.Wait();
///
///         // Pop the element from the queue
///         // NOTE: synchronization omitted for brevity
///         T value = m_queue.poll();
///
///         // Signal not full event now that there is at least one free space on the queue.
///         // This will unblock all threads waiting for this event to be signaled.
///         m_queueNotFullEvent.Set();
///     }
/// private:
///     Queue<T> m_queue = ...; // some queue implementation
///
///     // Initialize events to their default states.
///     // The queue starts out empty, so the "not full" event is signaled and the "not empty" event is not.
///     util::Event m_queueNotFullEvent{true};
///     util::Event m_queueNotEmptyEvent{false};
/// };
/// ```
class Event {
#if defined(_WIN32) || defined(__linux__) || defined(__FreeBSD__)
    // Windows-, Linux- and FreeBSD-specific implementation

public:
    /// @brief Constructs a new event with an initial signal state.
    /// @param[in] set the signal state
    [[nodiscard]] explicit Event(bool set = false) noexcept;

    /// @brief Waits until the event is signaled.
    void Wait();

    /// @brief Signals the event and notifies all waiting threads.
    void Set();

    /// @brief Resets (clears) the event signal.
    void Reset();

private:
    #ifdef _WIN32
    std::atomic<uint8> m_value;
    #elif defined(__linux__)
    std::atomic<int> m_value;
    #else // __FreeBSD__
    std::atomic<uint32> m_value;
    #endif

#else
    // Generic condvar-based implementation

public:
    /// @brief Constructs a new event with an initial signal state.
    /// @param[in] set the signal state
    [[nodiscard]] explicit Event(bool set = false) noexcept
        : m_set(set) {}

    /// @brief Waits until the event is signaled.
    void Wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condVar.wait(lock, [this] { return m_set; });
    }

    /// @brief Signals the event and notifies all waiting threads.
    void Set() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_set = true;
        m_condVar.notify_all();
    }

    /// @brief Resets (clears) the event signal.
    void Reset() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_set = false;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_condVar;
    bool m_set;

#endif
};

} // namespace util
