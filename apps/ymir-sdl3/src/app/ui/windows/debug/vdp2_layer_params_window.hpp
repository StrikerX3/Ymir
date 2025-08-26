#pragma once

#include "vdp_window_base.hpp"

#include <app/ui/views/debug/vdp2_layer_params_view.hpp>

namespace app::ui {

class VDP2LayerParamsWindow : public VDPWindowBase {
public:
    VDP2LayerParamsWindow(SharedContext &context);

protected:
    void DrawContents() override;

private:
    VDP2LayerParamsView m_layerParamsView;
};

} // namespace app::ui
