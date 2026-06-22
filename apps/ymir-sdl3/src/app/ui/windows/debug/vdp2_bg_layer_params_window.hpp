#pragma once

#include "vdp_window_base.hpp"

#include <app/ui/views/debug/vdp2_bg_layer_params_view.hpp>

namespace app::ui {

class VDP2BGLayerParamsWindow : public VDPWindowBase {
public:
    VDP2BGLayerParamsWindow(SharedContext &context);

protected:
    void DrawContents() override;

private:
    VDP2BGLayerParamsView m_layerParamsView;
};

} // namespace app::ui
