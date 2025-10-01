#pragma once

#include "vdp1_registers_window.hpp"
#include "vdp2_cram_window.hpp"
#include "vdp2_layer_params_window.hpp"
#include "vdp2_layer_visibility_window.hpp"
#include "vdp2_vram_delay_window.hpp"

namespace app::ui {

struct VDPWindowSet {
    VDPWindowSet(SharedContext &context)
        : vdp1Regs(context)
        , vdp2LayerVisibility(context)
        , vdp2LayerParams(context)
        , vdp2VRAMDelay(context)
        , vdp2CRAM(context) {}

    void DisplayAll() {
        vdp1Regs.Display();
        vdp2LayerVisibility.Display();
        vdp2LayerParams.Display();
        vdp2VRAMDelay.Display();
        vdp2CRAM.Display();
    }

    VDP1RegistersWindow vdp1Regs;
    VDP2LayerVisibilityWindow vdp2LayerVisibility;
    VDP2LayerParamsWindow vdp2LayerParams;
    VDP2VRAMDelayWindow vdp2VRAMDelay;
    VDP2CRAMWindow vdp2CRAM;
};

} // namespace app::ui
