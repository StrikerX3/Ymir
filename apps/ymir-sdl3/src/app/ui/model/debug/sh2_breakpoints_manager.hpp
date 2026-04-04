#pragma once

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

struct SH2Breakpoint {
    bool enabled = true;
    // TODO: condition
};

/// @brief Manages breakpoints on an SH2 instance.
class SH2BreakpointsManager {
public:
    /// @brief Binds the given SH2 instance to this manager.
    /// Upon binding, the SH2 instance's breakpoints are replaced with this manager's.
    /// @param[in] sh2 the SH2 instance to bind to
    void Bind(ymir::sh2::SH2 &sh2);

    /// @brief If an SH2 instance is bound, clears all of its breakpoints and unbinds it.
    void Unbind();

    /// @brief Sets a breakpoint at the specified address.
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return `true` if the breakpoint was newly set, `false` if it was already set
    bool SetBreakpoint(uint32 address);

    /// @brief Clears a breakpoint at the specified address.
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return `true` if the breakpoint was cleared, `false` if it wasn't set
    bool ClearBreakpoint(uint32 address);

    /// @brief Moves a breakpoint from `address` to `newAddress`, if it exists
    /// If there is a breakpoint at `newAddress`, it is replaced with the breakpoint from `address`.
    /// If there is no breakpoint at `address`, this function has no effect and returns `false`.
    /// If `address` and `newAddress` point to the same location after force-alignment, the function does nothing and
    /// returns `true`.
    ///
    /// @param[in] address the source address (force-aligned to 16-bit)
    /// @param[in] newAddress the destination address (force-aligned to 16-bit)
    /// @return `true` if the breakpoint exists and was moved, `false` otherwise
    bool MoveBreakpoint(uint32 address, uint32 newAddress);

    /// @brief Toggles (sets or clears) a breakpoint at the specified address.
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return `true` if the breakpoint was set, `false` if cleared
    bool ToggleBreakpointSet(uint32 address);

    /// @brief Toggles (enables or disables) a breakpoint at the specified address.
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return `true` if the breakpoint was enabled, `false` if disabled or not set
    bool ToggleBreakpointEnable(uint32 address);

    /// @brief Clears all breakpoints.
    void ClearAllBreakpoints();

    /// @brief Replaces all breakpoints.
    /// @param[in] breakpoints the breakpoints to use
    void ReplaceBreakpoints(std::map<uint32, SH2Breakpoint> breakpoints);

    /// @brief Retrieves all configured breakpoints.
    /// @return the breakpoints
    std::map<uint32, SH2Breakpoint> GetBreakpoints() const {
        return m_breakpoints;
    }

    /// @brief Checks if a breakpoint is set at the specified address.
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return `true` if there is a breakpoint at the address (even if disabled), `false` otherwise
    bool IsBreakpointSet(uint32 address) const;

    /// @brief Enables or disables a breakpoint at the specified address, if present.
    /// If disabled, a breakpoint remains registered on this manager, but is not applied to the SH2 instance.
    /// If there is no breakpoint at the specified address, this function has no effect and returns `false`.
    ///
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @param[in] enable whether to enable (`true`) or disable (`false`) the breakpoint
    /// @return `true` if the breakpoint exists, `false` otherwise
    bool EnableBreakpoint(uint32 address, bool enable);

    /// @brief Checks if a breakpoint is enabled at the specified address.
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return `true` if there is an enabled breakpoint at the address, `false` if disabled or not set
    bool IsBreakpointEnabled(uint32 address) const;

    /// @brief Checks if the condition of the breakpoint at the specified address holds, if present.
    /// If there is no breakpoint set at the address, returns `false`.
    /// If the breakpoint has no condition, returns `true`.
    ///
    /// @param[in] address the address (force-aligned to 16-bit)
    /// @return `true` if the condition passes, `false` if it fails or there is no breakpoint at the address
    bool CheckBreakpointCondition(uint32 address) const;

private:
    ymir::sh2::SH2 *m_sh2 = nullptr;

    std::map<uint32, SH2Breakpoint> m_breakpoints{};

    /// @brief Builds the set of active breakpoints.
    /// @return the set of active breakpoints to be applied to an SH2 instance
    std::set<uint32> BuildActiveBreakpointsSet() const;
};

} // namespace app::ui
