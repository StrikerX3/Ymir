#pragma once

#include <ymir/debug/watchpoint_defs.hpp>

#include <ymir/core/types.hpp>

#include <map>

// -----------------------------------------------------------------------------
// Forward declarations

namespace ymir::sh2 {

class SH2;

} // namespace ymir::sh2

// -----------------------------------------------------------------------------
// Implementation

namespace app::ui {

struct SH2Watchpoint {
    bool enabled = true;
    ymir::debug::WatchpointFlags flags = ymir::debug::WatchpointFlags::None;
    // TODO: condition
};

/// @brief Manages watchpoints on an SH2 instance.
///
/// None of the method in this class are thread-safe. Methods touch the bound SH2 instance are annotated as such and
/// require additional synchronization if the emulator runs in its own thread.
class SH2WatchpointsManager {
public:
    /// @brief Binds the given SH2 instance to this manager.
    /// Upon binding, the SH2 instance's watchpoints are replaced with this manager's.
    /// @param[in] sh2 the SH2 instance to bind to
    void Bind(ymir::sh2::SH2 &sh2);

    /// @brief If an SH2 instance is bound, clears all of its watchpoints and unbinds it.
    void Unbind();

    /// @brief Adds watchpoints at the specified address.
    ///
    /// Updates the SH2 instance.
    ///
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @param[in] flags the watchpoint flags to add
    void AddWatchpoint(uint32 address, ymir::debug::WatchpointFlags flags);

    /// @brief Removes watchpoints from the specified address.
    ///
    /// Updates the SH2 instance.
    ///
    /// @param[in] address the address (force-aligned to 16-bit)
    void RemoveWatchpoint(uint32 address, ymir::debug::WatchpointFlags flags);

    /// @brief Removes all watchpoints from the specified address.
    ///
    /// Updates the SH2 instance.
    ///
    /// @param[in] address the address (force-aligned to 16-bit)
    void ClearWatchpoint(uint32 address);

    /// @brief Moves watchpoints from `address` to `newAddress`, if it exists
    /// If there are watchpoints at `newAddress`, they are replaced with the watchpoints from `address`.
    /// If there are no watchpoints at `address`, this function has no effect and returns `false`.
    /// If `address` and `newAddress` point to the same location after force-alignment, the function does nothing and
    /// returns `true`.
    ///
    /// Updates the SH2 instance.
    ///
    /// @param[in] address the source address (force-aligned to 16-bit)
    /// @param[in] newAddress the destination address (force-aligned to 16-bit)
    /// @return `true` if the watchpoint exists and was moved, `false` otherwise
    bool MoveWatchpoint(uint32 address, uint32 newAddress);

    /// @brief Toggles (enables or disables) watchpoints at the specified address.
    ///
    /// Updates the SH2 instance.
    ///
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return `true` if the watchpoints were enabled, `false` if disabled or not set
    bool ToggleWatchpointEnabled(uint32 address);

    /// @brief Clears all watchpoints.
    ///
    /// Updates the SH2 instance.
    void ClearAllWatchpoints();

    /// @brief Replaces all watchpoints.
    ///
    /// Updates the SH2 instance.
    ///
    /// @param[in] watchpoints the watchpoints to use
    void ReplaceWatchpoints(std::map<uint32, SH2Watchpoint> watchpoints);

    /// @brief Retrieves all configured watchpoints.
    /// @return the watchpoints
    std::map<uint32, SH2Watchpoint> GetWatchpoints() const {
        return m_watchpoints;
    }

    /// @brief Retrieves the configured watchpoint at the specified address, if any.
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return the configured watchpoints at the address
    ymir::debug::WatchpointFlags GetWatchpointFlags(uint32 address) const;

    /// @brief Enables or disables a watchpoint at the specified address, if present.
    /// If disabled, a watchpoint remains registered on this manager, but is not applied to the SH2 instance.
    /// If there is no watchpoint at the specified address, this function has no effect and returns `false`.
    ///
    /// Updates the SH2 instance.
    ///
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @param[in] enable whether to enable (`true`) or disable (`false`) the watchpoint
    /// @return `true` if the watchpoint exists, `false` otherwise
    bool EnableWatchpoint(uint32 address, bool enable);

    /// @brief Checks if a watchpoint is enabled at the specified address.
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return `true` if there is an enabled watchpoint at the address, `false` if disabled or not set
    bool IsWatchpointEnabled(uint32 address) const;

    /// @brief Checks if the condition of the watchpoint at the specified address holds, if present.
    /// If there is no watchpoint set at the address, returns `false`.
    /// If the watchpoint has no condition, returns `true`.
    ///
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return `true` if the condition passes, `false` if it fails or there is no watchpoint at the address
    bool CheckWatchpointCondition(uint32 address) const;

private:
    ymir::sh2::SH2 *m_sh2 = nullptr;

    std::map<uint32, SH2Watchpoint> m_watchpoints{};

    /// @brief Builds the set of active watchpoints.
    /// @return the set of active watchpoints to be applied to an SH2 instance
    std::map<uint32, ymir::debug::WatchpointFlags> BuildActiveWatchpointsSet() const;
};

} // namespace app::ui
