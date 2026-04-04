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

/// @brief Manages watchpoints on an SH2 instance.
class SH2WatchpointsManager {
public:
    void Bind(ymir::sh2::SH2 &sh2) {
        m_sh2 = &sh2;
    }

    void Unbind() {
        m_sh2 = nullptr;
    }

private:
    struct Watchpoint {
        bool enabled;
        ymir::debug::WatchpointFlags flags;
        // TODO: condition
    };

    ymir::sh2::SH2 *m_sh2 = nullptr;

    std::map<uint32, Watchpoint> m_watchpoints{};
};

} // namespace app::ui
