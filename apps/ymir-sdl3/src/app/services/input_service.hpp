#pragma once

#include <app/settings.hpp>
#include <app/shared_context.hpp>
#include <functional>
#include <imgui.h>
#include <utility>
#include <ymir/hw/smpc/peripheral/peripheral_report.hpp>

namespace app::services {

struct InputServiceCallbacks {
    std::function<void()> openSettings;
    std::function<void()> showMessageHistory;
    std::function<void(size_t)> selectSaveStateSlot;
    std::function<void(size_t)> loadSaveStateSlot;
    std::function<void(size_t)> saveSaveStateSlot;
    std::function<void()> toggleRewindBuffer;
};

/// @brief Handles hotkeys, key bindings, and peripheral input mapping.
class InputService {
public:
    InputService(SharedContext &context, Settings &settings, InputServiceCallbacks callbacks);
    ~InputService() = default;

    InputService(const InputService &) = delete;
    InputService &operator=(const InputService &) = delete;

    /// @brief Rebinds all controller and keyboard action mappings.
    void RebindInputs();

    /// @brief Polls and updates input states.
    /// @param[in] timeDelta Seconds elapsed since the last update.
    void UpdateInputs(double timeDelta);

    /// @brief Draws lightgun crosshairs or other input overlays.
    /// @param[out] drawList ImGui draw list to render into.
    void DrawInputs(ImDrawList *drawList);

    /// @brief Reads input data from a specific controller port.
    /// @tparam port Port index.
    /// @param[out] report Report struct to fill with input data.
    template <int port>
    void ReadPeripheral(ymir::peripheral::PeripheralReport &report);

private:
    std::pair<float, float> WindowToScreen(float x, float y) const;

    SharedContext &m_context;
    Settings &m_settings;
    InputServiceCallbacks m_callbacks;
};

} // namespace app::services
