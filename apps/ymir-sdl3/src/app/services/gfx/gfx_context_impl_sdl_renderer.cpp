#include "gfx_context_impl_sdl_renderer.hpp"

#include "gfx_result.hpp"

#include <fmt/format.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <SDL3/SDL_render.h>

namespace app::gfx {

SDLRendererGraphicsContext::SDLRendererGraphicsContext(SDL_Window *window, SDL_Renderer *renderer)
    : IGraphicsContext(Backend::SDLRenderer)
    , m_window(window)
    , m_renderer(renderer) {}

SDLRendererGraphicsContext::~SDLRendererGraphicsContext() {
    ImGuiShutdown();
    SDL_DestroyRenderer(m_renderer);
}

GfxObjectResult<SDLRendererGraphicsContext>
SDLRendererGraphicsContext::Create(const SDLRendererGraphicsContextSpec &spec) {
    if (spec.window == nullptr) {
        return GfxOperationError{"Could not create SDL renderer: no window pointer provided"};
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(spec.window, nullptr);
    if (renderer == nullptr) {
        return GfxOperationError{fmt::format("Could not create SDL renderer: {}", SDL_GetError())};
    }
    return std::make_unique<SDLRendererGraphicsContext>(spec.window, renderer);
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

ImTextureID SDLRendererGraphicsContext::GetImGuiTextureID(TextureID textureID) const {
    // TODO: get and return texture ID
    return 0;
}

static int GetVSyncMode(PresentMode mode) {
    switch (mode) {
    default:
    case PresentMode::VSync: return 1;
    case PresentMode::Adaptive: return SDL_RENDERER_VSYNC_ADAPTIVE;
    case PresentMode::Mailbox: return SDL_RENDERER_VSYNC_DISABLED;
    }
}

GfxResult SDLRendererGraphicsContext::SetPresentMode(PresentMode mode) {
    if (SDL_SetRenderVSync(m_renderer, GetVSyncMode(mode))) {
        return {};
    }
    return GfxOperationError{fmt::format("Could not change VSync mode: {}", SDL_GetError())};
}

GfxResult SDLRendererGraphicsContext::Present() {
    if (SDL_RenderPresent(m_renderer)) {
        return {};
    }
    return GfxOperationError{fmt::format("Could not present frame: {}", SDL_GetError())};
}

} // namespace app::gfx
