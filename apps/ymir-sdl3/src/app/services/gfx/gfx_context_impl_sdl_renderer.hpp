#pragma once

#include "gfx_context.hpp"
#include "gfx_result.hpp"

#include <SDL3/SDL_render.h>

namespace app::gfx {

/// @brief Specifications for creating a SDL Renderer-backed graphics context.
struct SDLRendererGraphicsContextSpec {
    SDL_Window *window = nullptr; // Required
};

/// @brief Graphics context backed by the SDL Renderer API. Provided as fallback in case none of the platform graphics
/// APIs are supported by the host system.
class SDLRendererGraphicsContext final : public IGraphicsContext {
public:
    SDLRendererGraphicsContext(SDL_Window *window, SDL_Renderer *renderer);
    ~SDLRendererGraphicsContext();

    /// @brief Creates a SDL Renderer graphics context.
    /// @param[in] spec the backend specifications
    /// @return the graphics context instance or an error message
    static GfxObjectResult<SDLRendererGraphicsContext> Create(const SDLRendererGraphicsContextSpec &spec);

    void ClearScreen(gfx::ColorRGBA color) override;

    bool ImGuiInit() override;
    void ImGuiShutdown() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderFrame() override;

    ImTextureID GetImGuiTextureID(TextureID textureID) const override;

    GfxResult SetPresentMode(PresentMode mode) override;
    GfxResult Present() override;

private:
    SDL_Window *m_window;
    SDL_Renderer *m_renderer;

    bool m_imguiInitialized = false;
};

} // namespace app::gfx
