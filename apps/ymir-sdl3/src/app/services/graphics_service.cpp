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

GfxValueResult<GUITextureHandle> GraphicsService::CreateTexture(const Texture2DSpec &spec, FnTextureSetup &&fnSetup) {
    auto result = m_gfxContext->CreateTexture(spec);
    if (!result) {
        return result.Error();
    }

    const GUITextureHandle handle = GetNextTextureHandle();
    Texture2DInstance &texture = m_textures[handle];
    texture.id = result.Value();
    texture.spec = spec;
    texture.fnSetup = std::move(fnSetup);
    UpdateTexture(handle, nullptr, [&](void *data, size_t pitch) { texture.fnSetup(handle, false, data, pitch); });
    return handle;
}

bool GraphicsService::IsTextureHandleValid(GUITextureHandle handle) const {
    const Texture2DInstance *texture = GetTexture(handle);
    return texture != nullptr && m_gfxContext->IsTextureValid(texture->id);
}

GfxResult GraphicsService::ResizeTexture(GUITextureHandle handle, uint32 width, uint32 height) {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return GfxOperationError{"Invalid texture handle"};
    }
    return m_gfxContext->ResizeTexture(texture->id, width, height);
}

GfxResult GraphicsService::UpdateTexture(GUITextureHandle handle, const IRect *rect,
                                         const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return GfxOperationError{"Invalid texture handle"};
    }
    return m_gfxContext->UpdateTexture(texture->id, rect, fnUpdate);
}

GfxResult GraphicsService::RenderToTexture(GUITextureHandle src, GUITextureHandle dst, const FRect &srcRect,
                                           const FRect &dstRect) {
    const Texture2DInstance *srcTexture = GetTexture(src);
    if (srcTexture == nullptr) {
        return GfxOperationError{"Invalid source texture handle"};
    }
    const Texture2DInstance *dstTexture = GetTexture(dst);
    if (dstTexture == nullptr) {
        return GfxOperationError{"Invalid destination texture handle"};
    }
    return m_gfxContext->RenderToTexture(srcTexture->id, dstTexture->id, srcRect, dstRect);
}

GfxResult GraphicsService::DrawTextureRotated(GUITextureHandle handle, const FRect &srcRect, const FRect &dstRect,
                                              double rotAngle, const FPoint2D *anchorPoint) {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return GfxOperationError{"Invalid source texture handle"};
    }
    return m_gfxContext->DrawTextureRotated(texture->id, srcRect, dstRect, rotAngle, anchorPoint);
}

ImTextureID GraphicsService::GetImGuiTextureID(GUITextureHandle handle) const {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return 0;
    }
    return m_gfxContext->GetImGuiTextureID(texture->id);
}

bool GraphicsService::DestroyTexture(GUITextureHandle handle) {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return false;
    }
    m_gfxContext->DestroyTexture(texture->id);
    m_textures.erase(handle);
    m_freeTexHandles.push_back(handle);
    return true;
}

GfxResult GraphicsService::SetPresentMode(PresentMode mode) {
    return m_gfxContext->SetPresentMode(mode);
}

GfxResult GraphicsService::Present() {
    return m_gfxContext->Present();
}

GUITextureHandle GraphicsService::GetNextTextureHandle() {
    if (!m_freeTexHandles.empty()) {
        const GUITextureHandle handle = m_freeTexHandles.back();
        m_freeTexHandles.pop_back();
        return handle;
    }

    const GUITextureHandle handle = m_nextHandle++;

    // This really should not happen unless we somehow managed to create over 4 billion textures.
    assert(m_nextHandle != kInvalidGUITextureHandle);
    assert(!m_textures.contains(handle));

    return handle;
}

auto GraphicsService::GetTexture(GUITextureHandle handle) -> Texture2DInstance * {
    if (handle == kInvalidGUITextureHandle) {
        return nullptr;
    }
    if (auto it = m_textures.find(handle); it != m_textures.end()) {
        return &it->second;
    }
    return nullptr;
}

auto GraphicsService::GetTexture(GUITextureHandle handle) const -> const Texture2DInstance * {
    return const_cast<GraphicsService *>(this)->GetTexture(handle);
}

} // namespace app::services
