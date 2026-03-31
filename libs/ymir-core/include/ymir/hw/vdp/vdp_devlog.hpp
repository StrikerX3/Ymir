#pragma once

#include <ymir/util/dev_log.hpp>

namespace ymir::vdp {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   config
    //   phase
    //   intr
    //     intr_hb
    //   vdp1
    //     vdp1_regs
    //     vdp1_cmd
    //   vdp2
    //     vdp2_regs
    //     vdp2_render

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "VDP";
    };

    struct config : public base {
        static constexpr std::string_view name = "VDP-Config";
    };

    struct phase : public base {
        static constexpr std::string_view name = "VDP-Phase";
    };

    struct intr : public base {
        static constexpr std::string_view name = "VDP-Interrupt";
    };

    struct intr_hb : public intr {
        static constexpr devlog::Level level = devlog::level::debug;
    };

    struct vdp1 : public base {
        static constexpr std::string_view name = "VDP1";
    };

    struct vdp1_regs : public vdp1 {
        static constexpr std::string_view name = "VDP1-Regs";
    };

    struct vdp1_cmd : public vdp1 {
        static constexpr std::string_view name = "VDP1-Command";
    };

    struct vdp2 : public base {
        static constexpr std::string_view name = "VDP2";
    };

    struct vdp2_regs : public vdp2 {
        static constexpr std::string_view name = "VDP2-Regs";
    };

    struct vdp2_render : public vdp2 {
        static constexpr std::string_view name = "VDP2-Render";
    };

} // namespace grp

} // namespace ymir::vdp
