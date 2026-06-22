#pragma once

#include "vdp_window_base.hpp"

#include <app/ui/views/debug/vdp2_sprite_layer_params_view.hpp>

namespace app::ui {

class VDP2SpriteLayerParamsWindow : public VDPWindowBase {
public:
    VDP2SpriteLayerParamsWindow(SharedContext &context);

protected:
    void DrawContents() override;

private:
    VDP2SpriteLayerParamsView m_layerParamsView;
};

} // namespace app::ui
