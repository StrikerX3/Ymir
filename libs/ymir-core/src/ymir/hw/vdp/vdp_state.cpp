#include <ymir/hw/vdp/vdp_state.hpp>

#include <ymir/util/dev_log.hpp>

namespace ymir::vdp {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "VDP-State";
    };

} // namespace grp

void VDPState::LogResolution(uint32 dotClockMult) {
    devlog::info<grp::base>("Screen resolution set to {}x{}", HRes, VRes);
    switch (regs2.TVMD.LSMDn) {
    case InterlaceMode::None: devlog::info<grp::base>("Non-interlace mode"); break;
    case InterlaceMode::Invalid: devlog::info<grp::base>("Invalid interlace mode"); break;
    case InterlaceMode::SingleDensity: devlog::info<grp::base>("Single-density interlace mode"); break;
    case InterlaceMode::DoubleDensity: devlog::info<grp::base>("Double-density interlace mode"); break;
    }
    devlog::info<grp::base>("Dot clock mult = {}, display {}", dotClockMult, (regs2.TVMD.DISP ? "ON" : "OFF"));
}

} // namespace ymir::vdp
