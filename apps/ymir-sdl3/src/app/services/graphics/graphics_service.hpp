#pragma once

#include "types.hpp"

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include <functional>
#include <memory>
#include <unordered_map>

namespace app::services {

/// @brief Provides services for managing graphics resources.
/// Implemented on top of SDL3's Renderer API.
class GraphicsService {
public:
    ~GraphicsService();

    /// @brief Creates a new renderer using the specified graphics API, bound to the given window, and with initial
    /// vsync configuration.
    /// The renderer can be recreated at any point. Resources such as textures are automatically recreated with their
    /// original parameters when the renderer backend is changed.
    ///
    /// @param[in] backend the graphics backend to use
    /// @param[in] window the window to bind the renderer to
    /// @param[in] vsync the initial vsync option (see SDL_SetRenderVSync)
    /// @return a pointer to the renderer instance, or `nullptr` if could not be created. Use `SDL_GetError()` to get
    /// the error.
    SDL_Renderer *CreateRenderer(gfx::Backend backend, SDL_Window *window, int vsync);

    /// @brief Retrieves a pointer to the SDL renderer instance, if any was created.
    /// @return a pointer to the `SDL_Renderer` managed by this service.
    SDL_Renderer *GetRenderer() const {
        return m_renderer;
    }

    /// @brief Creates and registers a texture.
    /// @param[in] format the pixel format
    /// @param[in] access the access mode
    /// @param[in] w the initial texture width
    /// @param[in] h the initial texture height
    /// @param[in] fnSetup additional setup for the texture: filtering, mipmaps, reload texture contents, etc.
    /// @return a handle to the texture resource, or `kInvalidTextureHandle` if could not be created. Use
    /// `SDL_GetError()` to get the error.
    gfx::TextureHandle CreateTexture(SDL_PixelFormat format, SDL_TextureAccess access, int w, int h,
                                     gfx::FnTextureSetup fnSetup = {});

    /// @brief Checks if the texture handle is valid.
    /// @param[in] handle the texture handle to check
    /// @return `true` if the handle refers to a valid managed texture, `false` otherwise.
    bool IsTextureHandleValid(gfx::TextureHandle handle) const;

    /// @brief Attempts to resize the texture to the new dimensions.
    /// @param[in] handle the texture handle to try to resize
    /// @param[in] w the new width
    /// @param[in] h the new height
    /// @return `true` if the resize operation succeeded, `false` otherwise. Use `SDL_GetError()` to get the error.
    bool ResizeTexture(gfx::TextureHandle handle, int w, int h);

    /// @brief Retrieves a pointer to the SDL renderer texture for the given texture handle.
    /// @param[in] handle the texture handle
    /// @return a pointer to `SDL_Texture`, or `nullptr` if the handle is invalid or the texture was not created.
    SDL_Texture *GetSDLTexture(gfx::TextureHandle handle) const;

    /// @brief Destroys a managed texture.
    /// @param[in] handle the texture handle
    /// @return `true` if the texture was destroyed, `false` if it wasn't registered.
    bool DestroyTexture(gfx::TextureHandle handle);

private:
    struct TextureParams {
        SDL_Texture *texture = nullptr;
        SDL_PixelFormat format;
        SDL_TextureAccess access;
        int width;
        int height;
        gfx::FnTextureSetup fnSetup;
    };
    std::unordered_map<gfx::TextureHandle, TextureParams> m_textures;
    gfx::TextureHandle m_nextTextureHandle = 1u;

    gfx::TextureHandle GetNextTextureHandle();
    SDL_Texture *InternalCreateTexture(TextureParams &params, bool recreated);

    SDL_Renderer *m_renderer = nullptr;

    void RecreateResources();
    void DestroyResources();
};

} // namespace app::services
