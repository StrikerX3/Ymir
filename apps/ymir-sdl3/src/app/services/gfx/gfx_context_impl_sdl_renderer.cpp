#include "gfx_context_impl_sdl_renderer.hpp"

#include <fmt/format.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <ymir/util/scope_guard.hpp>

#include <SDL3/SDL_render.h>

namespace app::gfx {

static int GetVSyncMode(PresentMode mode) {
    switch (mode) {
    default:
    case PresentMode::VSync: return 1;
    case PresentMode::Mailbox: return 1;
    case PresentMode::Adaptive: return SDL_RENDERER_VSYNC_ADAPTIVE;
    case PresentMode::NoSync: return 0;
    }
}

static SDL_PixelFormat ToSDL3Value(PixelFormat format) {
    switch (format) {
    default: [[fallthrough]];
    case PixelFormat::Unknown: return SDL_PIXELFORMAT_UNKNOWN;
    case PixelFormat::R8G8B8A8_UNORM: return SDL_PIXELFORMAT_ABGR8888;
    case PixelFormat::R8G8B8X8_UNORM: return SDL_PIXELFORMAT_XBGR8888;
    case PixelFormat::B8G8R8A8_UNORM: return SDL_PIXELFORMAT_ARGB8888;
    case PixelFormat::B8G8R8X8_UNORM: return SDL_PIXELFORMAT_XRGB8888;
    }
}

static SDL_TextureAccess ToSDL3Value(TextureAccess access) {
    switch (access) {
    default: [[fallthrough]];
    case TextureAccess::Static: return SDL_TEXTUREACCESS_STATIC;
    case TextureAccess::Streaming: return SDL_TEXTUREACCESS_STREAMING;
    case TextureAccess::RenderTarget: return SDL_TEXTUREACCESS_TARGET;
    }
}

static SDL_ScaleMode ToSDL3Value(TextureFilterMode mode) {
    switch (mode) {
    default: [[fallthrough]];
    case TextureFilterMode::Linear: return SDL_SCALEMODE_LINEAR;
    case TextureFilterMode::Nearest: return SDL_SCALEMODE_NEAREST;
    }
}

// -----------------------------------------------------------------------------

SDLRendererGraphicsContext::SDLRendererGraphicsContext(SDL_Window *window)
    : IGraphicsContext(kBackend)
    , m_window(window) {}

SDLRendererGraphicsContext::~SDLRendererGraphicsContext() {
    Shutdown();
}

util::ObjectResult<SDLRendererGraphicsContext>
SDLRendererGraphicsContext::Create(const SDLRendererGraphicsContextSpec &spec) {
    if (spec.window == nullptr) {
        return util::ErrorMessage{"Could not create SDL renderer: no window pointer provided"};
    }
    auto context = std::make_unique<SDLRendererGraphicsContext>(spec.window);
    auto result = context->Initialize();
    if (!result) {
        return result.Error();
    }
    return std::move(context);
}

util::VoidResult<> SDLRendererGraphicsContext::Initialize() {
    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (m_renderer == nullptr) {
        return util::ErrorMessage{fmt::format("Could not create SDL renderer: {}", SDL_GetError())};
    }
    return {};
}

void SDLRendererGraphicsContext::Shutdown() {
    if (m_renderer != nullptr) {
        ImGuiShutdown();
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
}

bool SDLRendererGraphicsContext::IsInitialized() const {
    return m_renderer != nullptr;
}

util::VoidResult<> SDLRendererGraphicsContext::ResizeFramebuffer(uint32 width, uint32 height) {
    // Nothing to do here. SDL Renderer handles this internally when the window resize event is processed.
    return {};
}

void SDLRendererGraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    SDL_SetRenderDrawColorFloat(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(m_renderer);
}

bool SDLRendererGraphicsContext::ImGuiInit() {
    if (!m_imguiInitialized) {
        m_imguiInitialized =                                           //
            ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer) && //
            ImGui_ImplSDLRenderer3_Init(m_renderer);
    }
    return m_imguiInitialized;
}

void SDLRendererGraphicsContext::ImGuiShutdown() {
    if (m_imguiInitialized) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        m_imguiInitialized = false;
    }
}

void SDLRendererGraphicsContext::ImGuiNewFrame() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

void SDLRendererGraphicsContext::ImGuiRenderFrame() {
#if defined(__APPLE__)
    // Logical->Physical window-coordinate fix primarily for MacOS Retina displays
    const float pixelDensity = SDL_GetWindowPixelDensity(m_window);
    SDL_SetRenderScale(m_renderer, pixelDensity, pixelDensity);
#endif

    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);

#if defined(__APPLE__)
    SDL_SetRenderScale(m_renderer, 1.0f, 1.0f);
#endif
}

util::ValueResult<TextureID> SDLRendererGraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    SDL_Texture *texture =
        SDL_CreateTexture(m_renderer, ToSDL3Value(spec.format), ToSDL3Value(spec.access), spec.width, spec.height);
    if (texture == nullptr) {
        return util::ErrorMessage{fmt::format("Could not create texture: {}", SDL_GetError())};
    }
    SDL_SetTextureScaleMode(texture, ToSDL3Value(spec.filterMode));

    const TextureID id = GetNextTextureID();
    assert(!m_textures.contains(id));
    m_textures[id] = {
        .texture = texture,
        .spec = spec,
    };
    return id;
}

void SDLRendererGraphicsContext::DestroyTexture(TextureID id) {
    TextureInstance *instance = GetTexture(id);
    if (instance != nullptr) {
        SDL_DestroyTexture(instance->texture);
        m_textures.erase(id);
        FreeTextureID(id);
    }
}

bool SDLRendererGraphicsContext::IsTextureValid(TextureID id) const {
    const TextureInstance *instance = GetTexture(id);
    if (instance == nullptr) {
        return false;
    }
    // Check if the texture is still live by attempting to retrieve its properties
    SDL_PropertiesID props = SDL_GetTextureProperties(instance->texture);
    return props != 0;
}

ImTextureID SDLRendererGraphicsContext::GetImGuiTextureID(TextureID id) const {
    const TextureInstance *instance = GetTexture(id);
    if (instance == nullptr) {
        return 0;
    }
    return reinterpret_cast<ImTextureID>(instance->texture);
}

util::VoidResult<> SDLRendererGraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    TextureInstance *instance = GetTexture(id);
    if (instance == nullptr) {
        return util::ErrorMessage{"Invalid texture handle"};
    }
    const Texture2DSpec &spec = instance->spec;

    // Try creating the new texture first
    SDL_Texture *newTexture =
        SDL_CreateTexture(m_renderer, ToSDL3Value(spec.format), ToSDL3Value(spec.access), width, height);
    if (newTexture == nullptr) {
        return util::ErrorMessage{"Invalid texture handle"};
    }

    // Delete old texture and update parameters
    SDL_DestroyTexture(instance->texture);
    instance->texture = newTexture;
    instance->spec.width = width;
    instance->spec.height = height;
    SDL_SetTextureScaleMode(newTexture, ToSDL3Value(spec.filterMode));
    return {};
}

util::VoidResult<>
SDLRendererGraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                          const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    TextureInstance *instance = GetTexture(id);
    if (instance == nullptr) {
        return util::ErrorMessage{"Invalid texture handle"};
    }

    void *pixels = nullptr;
    int pitch = 0;
    SDL_Rect area{};
    SDL_Rect *areaPtr = nullptr;
    if (rect != nullptr) {
        area.x = rect->x;
        area.y = rect->y;
        area.w = rect->w;
        area.h = rect->h;
    }
    if (instance->spec.access == TextureAccess::Streaming) {
        // Streaming textures can be locked and unlocked
        if (!SDL_LockTexture(instance->texture, areaPtr, &pixels, &pitch)) {
            return util::ErrorMessage{fmt::format("Could not lock texture for update: {}", SDL_GetError())};
        }
        fnUpdate(pixels, pitch);
        SDL_UnlockTexture(instance->texture);
    } else {
        // Other types of textures need a staging buffer
        // Based on SDL_UpdateTextureNative (SDL_render.c)
        if (rect == nullptr) {
            area.x = 0;
            area.y = 0;
            area.w = instance->spec.width;
            area.h = instance->spec.height;
        }
        const int stagingPitch = (((area.w * SDL_BYTESPERPIXEL(ToSDL3Value(instance->spec.format))) + 3) & ~3);
        const size_t stagingBufferSize = (size_t)area.h * stagingPitch;
        if (stagingBufferSize > 0) {
            void *stagingPixels = SDL_malloc(stagingBufferSize);
            if (stagingPixels == nullptr) {
                return util::ErrorMessage{"Could not allocate memory for staging buffer"};
            }
            util::ScopeGuard sgFreeStagingPixels{[&] { SDL_free(stagingPixels); }};

            fnUpdate(stagingPixels, stagingPitch);
            if (!SDL_UpdateTexture(instance->texture, areaPtr, stagingPixels, stagingPitch)) {
                return util::ErrorMessage{fmt::format("Could not update texture: {}", SDL_GetError())};
            }
        }
    }
    return {};
}

util::VoidResult<> SDLRendererGraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                               const FRect &dstRect) {
    TextureInstance *srcInstance = GetTexture(src);
    if (srcInstance == nullptr) {
        return util::ErrorMessage{"Invalid source texture handle"};
    }

    TextureInstance *dstInstance = GetTexture(dst);
    if (dstInstance == nullptr) {
        return util::ErrorMessage{"Invalid destination texture handle"};
    }

    // Remember previous render target to be restored later
    SDL_Texture *prevRenderTarget = SDL_GetRenderTarget(m_renderer);

    // Render scaled framebuffer into display texture
    SDL_FRect srcRectSDL{srcRect.x, srcRect.y, srcRect.w, srcRect.h};
    SDL_FRect dstRectSDL{dstRect.x, dstRect.y, dstRect.w, dstRect.h};

    SDL_SetRenderTarget(m_renderer, dstInstance->texture);
    SDL_RenderTexture(m_renderer, srcInstance->texture, &srcRectSDL, &dstRectSDL);

    // Restore render target
    SDL_SetRenderTarget(m_renderer, prevRenderTarget);

    return util::VoidResult<>();
}

util::VoidResult<> SDLRendererGraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect,
                                                                  const FRect &dstRect, double rotAngle,
                                                                  const FPoint2D *anchorPoint) {
    TextureInstance *instance = GetTexture(id);
    if (instance == nullptr) {
        return util::ErrorMessage{"Invalid texture handle"};
    }

    const SDL_FRect srcRectSDL{srcRect.x, srcRect.y, srcRect.w, srcRect.h};
    const SDL_FRect dstRectSDL{dstRect.x, dstRect.y, dstRect.w, dstRect.h};
    SDL_FPoint center{};
    SDL_FPoint *centerPtr = nullptr;
    if (anchorPoint != nullptr) {
        centerPtr = &center;
        center.x = anchorPoint->x;
        center.y = anchorPoint->y;
    }
    if (SDL_RenderTextureRotated(m_renderer, instance->texture, &srcRectSDL, &dstRectSDL, rotAngle, centerPtr,
                                 SDL_FLIP_NONE)) {
        return {};
    }
    return util::ErrorMessage{fmt::format("Failed to draw rotated texture: {}", SDL_GetError())};
}

util::VoidResult<> SDLRendererGraphicsContext::SetPresentMode(PresentMode mode) {
    if (SDL_SetRenderVSync(m_renderer, GetVSyncMode(mode))) {
        return {};
    }
    return util::ErrorMessage{fmt::format("Could not change VSync mode: {}", SDL_GetError())};
}

util::VoidResult<> SDLRendererGraphicsContext::Present() {
    if (SDL_RenderPresent(m_renderer)) {
        return {};
    }
    return util::ErrorMessage{fmt::format("Could not present frame: {}", SDL_GetError())};
}

auto SDLRendererGraphicsContext::GetTexture(TextureID id) -> TextureInstance * {
    if (auto it = m_textures.find(id); it != m_textures.end()) {
        return &it->second;
    }
    return nullptr;
}

auto SDLRendererGraphicsContext::GetTexture(TextureID id) const -> const TextureInstance * {
    return const_cast<SDLRendererGraphicsContext *>(this)->GetTexture(id);
}

} // namespace app::gfx
