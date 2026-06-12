#pragma once

#include "../update_checker.hpp"

#include <ymir/util/event.hpp>

#include <functional>
#include <thread>

namespace app {
struct SharedContext;
struct Settings;
} // namespace app

namespace app::services {

/// @brief Checks for software updates on a background thread.
class UpdateCheckerService {
public:
    UpdateCheckerService() = default;
    ~UpdateCheckerService();

    UpdateCheckerService(const UpdateCheckerService &) = delete;
    UpdateCheckerService &operator=(const UpdateCheckerService &) = delete;

    /// @brief Starts the background update checking thread.
    /// @param[in] context Shared application context.
    /// @param[in] settings Application settings.
    /// @param[in] onUpdateAvailable Callback triggered when a new update is found.
    void Start(SharedContext &context, Settings &settings, std::function<void()> onUpdateAvailable);

    /// @brief Stops the background update checking thread.
    void Stop();

    /// @brief Triggers an update check.
    /// @param[in] skipCache True to bypass the local cache and query GitHub directly.
    void CheckForUpdates(bool skipCache);

private:
    std::thread m_thread;
    util::Event m_event;
    UpdateCheckMode m_mode = UpdateCheckMode::Offline;
    bool m_running = false;

    void ThreadLoop(SharedContext &context, Settings &settings, std::function<void()> onUpdateAvailable);
};

} // namespace app::services
