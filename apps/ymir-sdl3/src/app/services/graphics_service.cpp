#include "graphics_service.hpp"

#include "gfx/gfx_context_impl_null.hpp"
#include "gfx/gfx_context_impl_sdl_renderer.hpp"

using namespace app::gfx;

namespace app::services {

GraphicsService::GraphicsService()
    : m_gfxContext(std::make_unique<NullGraphicsContext>()) {}

GraphicsService::~GraphicsService() {}

GfxResult GraphicsService::InitGraphicsContext(Backend backend, SDL_Window *window, PresentMode presentMode) {
    switch (backend) {
    case Backend::Null: return GfxOperationError{"The null backend should not be initialized directly"};
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    case Backend::Direct3D11: return GfxOperationError{"Unimplemented"};
    case Backend::Direct3D12: return GfxOperationError{"Unimplemented"};
#endif
#ifdef YMIR_PLATFORM_HAS_VULKAN
    case Backend::Vulkan: return GfxOperationError{"Unimplemented"};
#endif
#ifdef YMIR_PLATFORM_HAS_METAL
    case Backend::Metal: return GfxOperationError{"Unimplemented"};
#endif
    case Backend::SDLRenderer: //
    {
        auto result = SDLRendererGraphicsContext::Create({.window = window});
        if (!result) {
            return result.Error();
        }
        m_gfxContext = result.Value();
    }
    }
    m_gfxContext->SetPresentMode(presentMode);
    return {};
}

void GraphicsService::DestroyGraphicsContext() {
    m_gfxContext = std::make_unique<NullGraphicsContext>();
}

bool GraphicsService::ImGuiInit() {
    return m_gfxContext->ImGuiInit();
}

void GraphicsService::ImGuiShutdown() {
    m_gfxContext->ImGuiShutdown();
}

void GraphicsService::ImGuiNewFrame() {
    m_gfxContext->ImGuiNewFrame();
}

void GraphicsService::ImGuiRenderFrame() {
    m_gfxContext->ImGuiRenderFrame();
}

void GraphicsService::ClearScreen(ColorRGBA color) {
    m_gfxContext->ClearScreen(color);
}

GfxResult GraphicsService::DrawTextureRotated(GUITextureHandle texture, const FRect &srcRect, const FRect &dstRect,
                                              double rotAngle, const FPoint2D *anchorPoint) {
    return GfxOperationError{"Unimplemented"};
}

GfxValueResult<GUITextureHandle> GraphicsService::CreateTexture(const Texture2DSpec &spec, gfx::FnSetup &&fnSetup) {
    // TODO: create and register texture internally
    // should hold the context's TextureID which may change between invocations, then:
    //   m_gfxContext->GetImGuiTextureID(textureID);
    return GfxOperationError{"Unimplemented"};
}

bool GraphicsService::IsTextureHandleValid(GUITextureHandle handle) const {
    return false;
}

GfxResult GraphicsService::ResizeTexture(GUITextureHandle handle, int w, int h) {
    return GfxOperationError{"Unimplemented"};
}

GfxResult GraphicsService::UpdateTexture(GUITextureHandle handle,
                                         const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    return GfxOperationError{"Unimplemented"};
}

GfxResult GraphicsService::RenderToTexture(GUITextureHandle src, GUITextureHandle dst, const FRect &srcRect,
                                           const FRect &dstRect) {
    return GfxOperationError{"Unimplemented"};
}

ImTextureID GraphicsService::GetImGuiTextureID(GUITextureHandle handle) const {
    // TODO: get texture ID from handle, then:
    //   return m_gfxContext->GetImGuiTextureID(textureID);
    return 0;
}

bool GraphicsService::DestroyTexture(GUITextureHandle handle) {
    return false;
}

GfxResult GraphicsService::SetPresentMode(PresentMode mode) {
    return m_gfxContext->SetPresentMode(mode);
}

GfxResult GraphicsService::Present() {
    return m_gfxContext->Present();
}

} // namespace app::services
