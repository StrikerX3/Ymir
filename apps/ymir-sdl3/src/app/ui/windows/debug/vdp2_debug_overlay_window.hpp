#pragma once

#include "vdp_window_base.hpp"

#include <app/ui/views/debug/vdp2_debug_overlay_view.hpp>

namespace app::ui {

class VDP2DebugOverlayWindow : public VDPWindowBase {
public:
    VDP2DebugOverlayWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    VDP2DebugOverlayView m_debugOverlayView;
};

} // namespace app::ui
