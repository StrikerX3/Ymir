#pragma once

#include <ymir/core/configuration_defs.hpp>
#include <ymir/sys/clocks.hpp>
#include <ymir/sys/memory_defs.hpp>

#include <ymir/core/hash.hpp>

namespace ymir::savestate {

struct SystemSaveState {
    core::config::sys::VideoStandard videoStandard;
    sys::ClockSpeed clockSpeed;
    bool slaveSH2Enabled;

    alignas(16) XXH128Hash iplRomHash;

    alignas(16) std::array<uint8, sys::kWRAMLowSize> WRAMLow;
    alignas(16) std::array<uint8, sys::kWRAMHighSize> WRAMHigh;
};

} // namespace ymir::savestate
