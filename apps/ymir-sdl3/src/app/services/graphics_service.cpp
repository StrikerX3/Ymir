#include "graphics_service.hpp"

#include "gfx/gfx_context_impls.hpp"
#include "gfx/gfx_context_specs.hpp"

#include <SDL3/SDL_video.h>

#include <imgui.h>

#include <cassert>
#include <functional>
#include <memory>
#include <utility>

using namespace app::gfx;

namespace app::services {

GraphicsService::GraphicsService()
    : m_gfxContext(std::make_unique<NullGraphicsContext>()) {}

GraphicsService::~GraphicsService() {}

util::VoidResult<> GraphicsService::InitGraphicsContext(Backend backend, SDL_Window *window, PresentMode presentMode) {
    auto result = CreateGraphicsContext(backend, window);
    if (!result) {
        return result.Error();
    }
    m_gfxContext = result.Value();
    m_gfxContext->SetPresentMode(presentMode);
    return {};
}

template <typename T>
static util::ObjectResult<IGraphicsContext> ConvertResult(util::ObjectResult<T> &&result) {
    if (!result) {
        return result.Error();
    }
    return std::unique_ptr<IGraphicsContext>{result.Value()};
}

#if YMIR_PLATFORM_HAS_DIRECT3D
    #ifdef NDEBUG
        #define YMIR_D3D_ENABLE_DEBUG true
    #else
        #define YMIR_D3D_ENABLE_DEBUG false
    #endif

static HWND GetWindowHWND(SDL_Window *window) {
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    void *ptr = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    return static_cast<HWND>(ptr);
}
#endif

util::ObjectResult<IGraphicsContext> GraphicsService::CreateGraphicsContext(gfx::Backend backend, SDL_Window *window) {
    switch (backend) {
    case Backend::Null:
        // Use DestroyGraphicsContext instead
        return util::ErrorMessage{"Cannot initialize the null backend"};
#if YMIR_PLATFORM_HAS_DIRECT3D
    case Backend::Direct3D11: return ConvertResult(Direct3D11GraphicsContext::Create({/*TODO*/}));
    case Backend::Direct3D12:
        return ConvertResult(Direct3D12GraphicsContext::Create({
            .featureLevel = D3D_FEATURE_LEVEL_11_0,
            .hwnd = GetWindowHWND(window),
            .adapter = nullptr,
            .debug =
                {
                    // TODO: make this externally configurable?
                    .enabled = YMIR_D3D_ENABLE_DEBUG,
                    .breakOnWarnings = YMIR_D3D_ENABLE_DEBUG,
                },
        }));
#endif
#if YMIR_PLATFORM_HAS_VULKAN
    case Backend::Vulkan: return ConvertResult(VulkanGraphicsContext::Create({/*TODO*/}));
#endif
#if YMIR_PLATFORM_HAS_METAL
    case Backend::Metal: return ConvertResult(MetalGraphicsContext::Create({/*TODO*/}));
#endif
    case Backend::SDLRenderer: return ConvertResult(SDLRendererGraphicsContext::Create({.window = window}));
    }
    return util::ErrorMessage{"Invalid backend"};
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

util::ValueResult<GUITextureHandle> GraphicsService::CreateTexture(const Texture2DSpec &spec,
                                                                   FnTextureSetup &&fnSetup) {
    auto createResult = m_gfxContext->CreateTexture(spec);
    if (!createResult) {
        return util::ErrorMessage{fmt::format("Failed to create texture: {}", createResult.Error().message)};
    }

    const GUITextureHandle handle = GetNextTextureHandle();
    Texture2DInstance &texture = m_textures[handle];
    texture.id = createResult.Value();
    texture.spec = spec;
    texture.fnSetup = std::move(fnSetup);
    auto updateResult = m_gfxContext->UpdateTexture(
        texture.id, nullptr, [&](void *data, size_t pitch) { texture.fnSetup(handle, false, data, pitch); });
    if (!updateResult) {
        return util::ErrorMessage{fmt::format("Failed to upload texture: {}", updateResult.Error().message)};
    }
    return handle;
}

bool GraphicsService::IsTextureHandleValid(GUITextureHandle handle) const {
    const Texture2DInstance *texture = GetTexture(handle);
    return texture != nullptr && m_gfxContext->IsTextureValid(texture->id);
}

util::VoidResult<> GraphicsService::ResizeTexture(GUITextureHandle handle, uint32 width, uint32 height) {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return util::ErrorMessage{"Invalid texture handle"};
    }
    return m_gfxContext->ResizeTexture(texture->id, width, height);
}

util::VoidResult<> GraphicsService::UpdateTexture(GUITextureHandle handle, const IRect *rect,
                                                  const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return util::ErrorMessage{"Invalid texture handle"};
    }
    return m_gfxContext->UpdateTexture(texture->id, rect, fnUpdate);
}

util::VoidResult<> GraphicsService::RenderToTexture(GUITextureHandle src, GUITextureHandle dst, const FRect &srcRect,
                                                    const FRect &dstRect) {
    const Texture2DInstance *srcTexture = GetTexture(src);
    if (srcTexture == nullptr) {
        return util::ErrorMessage{"Invalid source texture handle"};
    }
    const Texture2DInstance *dstTexture = GetTexture(dst);
    if (dstTexture == nullptr) {
        return util::ErrorMessage{"Invalid destination texture handle"};
    }
    return m_gfxContext->RenderToTexture(srcTexture->id, dstTexture->id, srcRect, dstRect);
}

util::VoidResult<> GraphicsService::DrawTextureRotated(GUITextureHandle handle, const FRect &srcRect,
                                                       const FRect &dstRect, double rotAngle,
                                                       const FPoint2D *anchorPoint) {
    const Texture2DInstance *texture = GetTexture(handle);
    if (texture == nullptr) {
        return util::ErrorMessage{"Invalid source texture handle"};
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

util::VoidResult<> GraphicsService::SetPresentMode(PresentMode mode) {
    return m_gfxContext->SetPresentMode(mode);
}

util::VoidResult<> GraphicsService::Present() {
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
