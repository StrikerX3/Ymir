#pragma once

#include <ymir/util/dev_log.hpp>

namespace ymir::scu::grp {

// -----------------------------------------------------------------------------
// Dev log groups

// Hierarchy:
//
// base
//   regs
//   intr
//   dma
//     dma_start
//   debug
//   dsp

struct base {
    static constexpr bool enabled = true;
    static constexpr devlog::Level level = devlog::level::debug;
    static constexpr std::string_view name = "SCU";
};

struct regs : public base {
    static constexpr std::string_view name = "SCU-Regs";
};

struct intr : public base {
    static constexpr std::string_view name = "SCU-Interrupt";
};

struct dma : public base {
    // static constexpr devlog::Level level = devlog::level::trace;
    static constexpr std::string_view name = "SCU-DMA";
};

struct dma_start : public base {
    // static constexpr devlog::Level level = devlog::level::trace;
    static constexpr std::string_view name = "SCU-DMAStart";
};

struct debug : public base {
    static constexpr std::string_view name = "SCU-Debug";
};

struct dsp : public base {
    static constexpr std::string_view name = "SCU-DSP";
};

} // namespace ymir::scu::grp
