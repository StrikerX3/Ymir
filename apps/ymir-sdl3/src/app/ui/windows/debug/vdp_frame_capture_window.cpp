#include "vdp_frame_capture_window.hpp"

using namespace ymir;
using namespace util;

namespace app::ui {

VDPFrameCaptureWindow::VDPFrameCaptureWindow(SharedContext &context)
    : VDPWindowBase(context)
/*, m_listView(context, m_vdp)*/
/*, m_displayView(context, m_vdp)*/
/*, m_cmdListView(context, m_vdp)*/
/*, m_pixelDetailsView(context, m_vdp)*/
{

    m_windowConfig.name = "VDP frame capture";

    m_renderer.SetRenderCallback(util::MakeClassMemberOptionalCallback<&VDPFrameCaptureWindow::OnFrame>(this));
}

VDPFrameCaptureWindow::~VDPFrameCaptureWindow() {
    if (m_texFrame != nullptr) {
        SDL_DestroyTexture(m_texFrame);
    }
}

void VDPFrameCaptureWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(200 * m_context.displayScale, 200 * m_context.displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void VDPFrameCaptureWindow::DrawContents() {
    // m_listView.Display();
    // m_displayView.Display();
    // m_cmdListView.Display();
    // m_pixelDetailsView.Display();

    // TODO: move to an Init function
    if (m_texFrame == nullptr) {
        m_texFrame = SDL_CreateTexture(m_context.renderer, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING,
                                       vdp::kMaxResH, vdp::kMaxResV);
        if (m_texFrame == nullptr) {
            // devlog::error<grp::base>("Unable to create texture: {}", SDL_GetError());
        } else {
            SDL_SetTextureScaleMode(m_texFrame, SDL_SCALEMODE_NEAREST);
        }
    }

    if (ImGui::Button("Render test")) {
        m_tracer.CopyLatestState(m_renderer.State);
        m_renderer.Render();
    }

    if (m_texFrame != nullptr && m_fbWidth > 0 && m_fbHeight > 0) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        // TODO: maintain aspect ratio
        ImGui::Image((ImTextureID)m_texFrame, avail, ImVec2(0, 0),
                     ImVec2((float)m_fbWidth / vdp::kMaxResH, (float)m_fbHeight / vdp::kMaxResV));
    }
}

void VDPFrameCaptureWindow::OnFrame(uint32 *fb, uint32 width, uint32 height) {
    if (m_texFrame == nullptr) {
        return;
    }
    uint32 *pixels = nullptr;
    int pitch = 0;
    SDL_Rect area{.x = 0, .y = 0, .w = (int)width, .h = (int)height};
    if (SDL_LockTexture(m_texFrame, &area, (void **)&pixels, &pitch)) {
        for (uint32 y = 0; y < height; y++) {
            std::copy_n(&fb[y * width], width, &pixels[y * pitch / sizeof(uint32)]);
        }
        SDL_UnlockTexture(m_texFrame);
    }
    m_fbWidth = width;
    m_fbHeight = height;
}

} // namespace app::ui
