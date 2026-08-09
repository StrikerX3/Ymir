#pragma once

#include "gfx_context.hpp"
#include "gfx_result.hpp"

#include <SDL3/SDL_render.h>

#include <unordered_map>

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

    GfxValueResult<TextureID> CreateTexture(const Texture2DSpec &spec) override;
    void DestroyTexture(TextureID id) override;
    bool IsTextureValid(TextureID id) const override;
    ImTextureID GetImGuiTextureID(TextureID id) const override;
    GfxResult ResizeTexture(TextureID id, uint32 width, uint32 height) override;
    GfxResult UpdateTexture(TextureID id, const IRect *rect,
                            const std::function<void(void *data, size_t pitch)> &fnUpdate) override;
    GfxResult RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect, const FRect &dstRect) override;
    GfxResult DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect, double rotAngle,
                                 const FPoint2D *anchorPoint = nullptr) override;

    GfxResult SetPresentMode(PresentMode mode) override;
    GfxResult Present() override;

private:
    SDL_Window *m_window;
    SDL_Renderer *m_renderer;

    bool m_imguiInitialized = false;

    struct TextureInstance {
        SDL_Texture *texture;
        Texture2DSpec spec;
    };
    std::unordered_map<TextureID, TextureInstance> m_textures;

    TextureInstance *GetTexture(TextureID id);
    const TextureInstance *GetTexture(TextureID id) const;
};

} // namespace app::gfx
