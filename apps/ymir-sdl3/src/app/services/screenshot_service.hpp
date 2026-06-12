#pragma once

#include "screenshot_types.hpp"

#include <ymir/util/event.hpp>

#include <filesystem>
#include <mutex>
#include <queue>
#include <thread>

namespace app {
struct SharedContext;
} // namespace app

namespace app::services {

/// @brief Processes and saves screenshots on a background thread.
class ScreenshotService {
public:
    ScreenshotService() = default;
    ~ScreenshotService();

    ScreenshotService(const ScreenshotService &) = delete;
    ScreenshotService &operator=(const ScreenshotService &) = delete;

    /// @brief Starts the background screenshot processing thread.
    /// @param[in] context Shared application context.
    void Start(SharedContext &context);

    /// @brief Stops the background processing thread.
    void Stop();

    /// @brief Enqueues a screenshot for background processing.
    /// @param[in] ss Screenshot data to queue.
    void Enqueue(screenshot::Screenshot &&ss);

private:
    std::thread m_thread;
    util::Event m_writeEvent;
    std::queue<screenshot::Screenshot> m_queue;
    std::mutex m_queueMtx;
    bool m_running = false;

    void ProcessingThread(SharedContext &context);
};

} // namespace app::services
