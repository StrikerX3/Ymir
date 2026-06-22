#pragma once

#include "vdp_window_base.hpp"

#include <app/ui/views/debug/vdp2_color_calc_params_view.hpp>

namespace app::ui {

class VDP2ColorCalcParamsWindow : public VDPWindowBase {
public:
    VDP2ColorCalcParamsWindow(SharedContext &context);

protected:
    void DrawContents() override;

private:
    VDP2ColorCalcParamsView m_layerParamsView;
};

} // namespace app::ui
