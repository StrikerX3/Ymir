#pragma once

#include "vdp_window_base.hpp"

#include <app/ui/views/debug/vdp2_cram_view.hpp>

namespace app::ui {

class VDP2CRAMWindow : public VDPWindowBase {
public:
    VDP2CRAMWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    VDP2CRAMView m_cramView;
};

} // namespace app::ui
