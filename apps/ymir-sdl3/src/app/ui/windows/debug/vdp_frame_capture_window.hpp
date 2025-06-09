#pragma once

#include "vdp_window_base.hpp"

// #include <app/ui/views/debug/vdp_framecap_list_view.hpp>
// #include <app/ui/views/debug/vdp_framecap_display_view.hpp>
// #include <app/ui/views/debug/vdp_framecap_cmdlist_view.hpp>
// #include <app/ui/views/debug/vdp_framecap_pixel_details_view.hpp>

namespace app::ui {

class VDPFrameCaptureWindow : public VDPWindowBase {
public:
    VDPFrameCaptureWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    // VDPFrameCaptureListView m_listView;
    // VDPFrameCaptureDisplayView m_displayView;
    // VDPFrameCaptureCommandListView m_cmdListView;
    // VDPFrameCapturePixelDetailsView m_pixelDetailsView;
};

} // namespace app::ui
