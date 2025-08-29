#pragma once

/**
@file
@brief Debug break information.
*/

#include <ymir/util/callback.hpp>
#include <ymir/util/inline.hpp>

#include <ymir/core/types.hpp>

#include <cassert>

// -----------------------------------------------------------------------------
// Forward declarations

namespace ymir {

struct Saturn;

} // namespace ymir

// -----------------------------------------------------------------------------

namespace ymir::debug {

enum class MemoryAccessSize : uint8 { Byte, Half, Word };
enum class MemoryAccessDirection : uint8 { Read, Write };

/// @brief Describes a debug break event.
///
/// This is a tagged union -- `event` is the tag, and `details` is the union. Each event indicates its valid field.
struct DebugBreakInfo {
    /// @brief The event that raised the debug break signal.
    enum class Event {
        /// @brief An SH-2 CPU hit a breakpoint.
        /// The `DebugBreakInfo::details::sh2Breakpoint` field has details about this event.
        SH2Breakpoint,

        /// @brief An SH-2 CPU hit a watchpoint.
        /// The `DebugBreakInfo::details::sh2Watchpoint` field has details about this event.
        SH2Watchpoint,
    } event;

    /// @brief Details about the event.
    union Details {
        /// @brief Details about the `Event::SH2Breakpoint` event.
        struct SH2Breakpoint {
            bool master; ///< Triggered from the MSH2 (`true`) or SSH2 (`false`)
            uint32 pc;   ///< PC address of the breakpoint
        } sh2Breakpoint;

        /// @brief Details about the `Event::SH2Watchpoint` event.
        struct SH2Watchpoint {
            bool master;               ///< Triggered from the MSH2 (`true`) or SSH2 (`false`)
            MemoryAccessDirection dir; ///< Direction of the memory access
            MemoryAccessSize size;     ///< Size of the memory access
            uint32 address;            ///< Address of the memory access
            uint32 pc;                 ///< PC address where the watchpoint was triggered
        } sh2Watchpoint;
    } details;

    /// @brief Constructs a `DebugBreakInfo` for an SH-2 breakpoint hit.
    /// @param[in] master whether the event was triggered from the MSH2 (`true`) or SSH2 (`false`) CPU
    /// @param[in] pc the PC address of the breakpoint
    /// @return a `DebugBreakInfo` struct with the `Event::SH2Breakpoint` event
    static DebugBreakInfo SH2Breakpoint(bool master, uint32 pc) {
        return DebugBreakInfo{.event = Event::SH2Breakpoint,
                              .details = {.sh2Breakpoint = {.master = master, .pc = pc}}};
    }

    /// @brief Constructs a `DebugBreakInfo` for an SH-2 watchpoint hit.
    /// @param[in] master whether the event was triggered from the MSH2 (`true`) or SSH2 (`false`) CPU
    /// @param[in] dir direction of the memory access
    /// @param[in] size size of the memory access
    /// @param[in] address address of the memory access
    /// @param[in] pc the PC address where the watchpoint was triggerd
    /// @return a `DebugBreakInfo` struct with the `Event::SH2Watchpoint` event
    static DebugBreakInfo SH2Watchpoint(bool master, MemoryAccessDirection dir, MemoryAccessSize size, uint32 address,
                                        uint32 pc) {
        return DebugBreakInfo{
            .event = Event::SH2Watchpoint,
            .details = {.sh2Watchpoint = {.master = master, .dir = dir, .size = size, .address = address, .pc = pc}}};
    }
};

/// @brief Invoked when a debug break signal is raised.
using CBDebugBreakRaised = util::OptionalCallback<void(const DebugBreakInfo &info)>;

/// @brief Manages the debug break signal.
class DebugBreakManager {
public:
    /// @brief Sets the debug break callback to be invoked when the debug break signal is raised.
    /// @param[in] callback the debug break callback
    FORCE_INLINE void SetDebugBreakRaisedCallback(debug::CBDebugBreakRaised callback) {
        m_cbDebugBreakRaised = callback;
    }

    /// @brief Signals a debug break which interrupts emulation and invokes the attached debug break handler.
    FORCE_INLINE void SignalDebugBreak(const debug::DebugBreakInfo &info) {
        // Debug break signals should only be raised while debug tracing is enabled
        // assert(m_systemFeatures.enableDebugTracing);

        m_debugBreak = true;
        m_cbDebugBreakRaised(info);
    }

    /// @brief Determines if the debug break signal was raised.
    /// @return `true` if the debug break signal was raised
    [[nodiscard]] FORCE_INLINE bool IsDebugBreakRaised() const {
        return m_debugBreak;
    }

private:
    bool m_debugBreak = false; ///< Debug break signal

    /// @brief Callback invoked when the debug break signal is raised.
    CBDebugBreakRaised m_cbDebugBreakRaised;

    /// @brief Lowers the debug break signal.
    /// @return `true` if the signal was lowered
    FORCE_INLINE bool LowerDebugBreak() {
        if (!m_debugBreak) {
            return false;
        }
        m_debugBreak = false;
        return true;
    }

    friend struct ::ymir::Saturn;
};

} // namespace ymir::debug
