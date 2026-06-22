#pragma once

#include "vdp_window_base.hpp"

#include <app/ui/views/debug/vdp2_window_params_view.hpp>

namespace app::ui {

class VDP2WindowParamsWindow : public VDPWindowBase {
public:
    VDP2WindowParamsWindow(SharedContext &context);

protected:
    void DrawContents() override;

private:
    VDP2WindowParamsView m_layerParamsView;
};

} // namespace app::ui
