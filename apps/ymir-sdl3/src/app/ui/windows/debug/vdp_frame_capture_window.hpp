#pragma once

#include "vdp_window_base.hpp"

// #include <app/ui/views/debug/vdp_framecap_list_view.hpp>
// #include <app/ui/views/debug/vdp_framecap_display_view.hpp>
// #include <app/ui/views/debug/vdp_framecap_cmdlist_view.hpp>
// #include <app/ui/views/debug/vdp_framecap_pixel_details_view.hpp>

#include <ymir/hw/vdp/vdp_debug_renderer.hpp>

#include <SDL3/SDL_render.h>

namespace app::ui {

class VDPFrameCaptureWindow : public VDPWindowBase {
public:
    VDPFrameCaptureWindow(SharedContext &context);
    ~VDPFrameCaptureWindow();

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    // VDPFrameCaptureListView m_listView;
    // VDPFrameCaptureDisplayView m_displayView;
    // VDPFrameCaptureCommandListView m_cmdListView;
    // VDPFrameCapturePixelDetailsView m_pixelDetailsView;

    void OnFrame(uint32 *fb, uint32 width, uint32 height);

    ymir::vdp::VDPDebugRenderer m_renderer;

    SDL_Texture *m_texFrame = nullptr;
    uint32 m_fbWidth = 0;
    uint32 m_fbHeight = 0;
};

} // namespace app::ui
