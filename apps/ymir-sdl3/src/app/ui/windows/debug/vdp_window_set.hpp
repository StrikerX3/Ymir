#pragma once

#include "vdp2_layers_window.hpp"
#include "vdp_frame_capture_window.hpp"

namespace app::ui {

struct VDPWindowSet {
    VDPWindowSet(SharedContext &context)
        : vdpFrameCap(context)
        , vdp2Layers(context) {}

    void DisplayAll() {
        vdpFrameCap.Display();
        vdp2Layers.Display();
    }

    VDPFrameCaptureWindow vdpFrameCap;
    VDP2LayersWindow vdp2Layers;
};

} // namespace app::ui
