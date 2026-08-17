#pragma once

#include "vdp_window_base.hpp"

#include <app/ui/views/debug/vdp2_registers_view.hpp>

namespace app::ui {

class VDP2RegistersWindow : public VDPWindowBase {
public:
    VDP2RegistersWindow(SharedContext &context);

protected:
    void DrawContents() override;

private:
    VDP2RegistersView m_regsView;
};

} // namespace app::ui
