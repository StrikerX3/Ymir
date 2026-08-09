#pragma once

#include "gfx/gfx_context.hpp"
#include "gfx/gfx_gui_types.hpp"
#include "gfx/gfx_result.hpp"

#include <SDL3/SDL_video.h>

#include <functional>
#include <memory>

namespace app::services {

/// @brief Provides services for managing graphics resources.
class GraphicsService {
public:
    GraphicsService();
    ~GraphicsService();

    /// @brief Initializes a graphics context.
    /// @param[in] backend the graphics backend
    /// @param[in] window the window on which to render graphics
    /// @param[in] presentMode the initial presentation mode
    /// @return nothing on success, an error message on failure
    gfx::GfxResult InitGraphicsContext(gfx::Backend backend, SDL_Window *window, gfx::PresentMode presentMode);

    /// @brief Destroys the graphics context, effectively replacing it with a null context.
    void DestroyGraphicsContext();

    /// @brief Retrieves the current graphics context's backend type.
    /// @return the current graphics backend in use
    gfx::Backend GetGraphicsContextBackend() const {
        return m_gfxContext->GetBackend();
    }

    /// @brief Initializes ImGui using the current graphics context.
    /// @return `true` if ImGui was initialized successfully, `false` on failure
    bool ImGuiInit();

    /// @brief Shuts down ImGui using the current graphics context.
    /// ImGui is automatically shut down when the context is replaced, but it is not initialized on context creation.
    void ImGuiShutdown();

    /// @brief Starts a new ImGui frame.
    void ImGuiNewFrame();

    /// @brief Renders the current ImGui frame.
    void ImGuiRenderFrame();

    /// @brief Clears the screen with the specified color.
    /// @param[in] color the clear color
    void ClearScreen(gfx::ColorRGBA color);

    /// @brief Draws a texture rotated about the given anchor point.
    /// @param[in] texture the texture to draw
    /// @param[in] srcRect portion of the texture to draw
    /// @param[in] dstRect where to draw the texture on the screen
    /// @param[in] rotAngle clockwise rotation amount (in degrees)
    /// @param[in,opt] anchorPoint rotation anchor point. If `nullptr`, rotates about the center of the texture
    /// @return nothing on success, an error message on failure
    gfx::GfxResult DrawTextureRotated(gfx::GUITextureHandle texture, const gfx::FRect &srcRect,
                                      const gfx::FRect &dstRect, double rotAngle,
                                      const gfx::FPoint2D *anchorPoint = nullptr);

    /// @brief Creates and registers a 2D texture.
    /// Once created, the texture is automatically recreated when the backend is changed through.
    /// @param[in] spec texture format specifications
    /// @return a handle to the texture, or an error message if the texture could not be created
    gfx::GfxValueResult<gfx::GUITextureHandle> CreateTexture(const gfx::Texture2DSpec &spec);

    /// @brief Checks if the texture handle is valid.
    /// @param[in] handle the texture handle to check
    /// @return `true` if the handle refers to a valid managed texture, `false` otherwise.
    bool IsTextureHandleValid(gfx::GUITextureHandle handle) const;

    /// @brief Attempts to resize the texture to the new dimensions.
    /// @param[in] handle the texture handle to try to resize
    /// @param[in] w the new width
    /// @param[in] h the new height
    /// @return nothing on success, an error message on failure
    gfx::GfxResult ResizeTexture(gfx::GUITextureHandle handle, int w, int h);

    /// @brief Updates the contents of a texture.
    /// @param[in] handle the texture handle
    /// @param[in] fnUpdate the update function, taking a pointer to writable texture data and the line pitch in bytes.
    /// This buffer should not be read by the CPU.
    /// @return nothing on success, an error message on failure
    gfx::GfxResult UpdateTexture(gfx::GUITextureHandle handle,
                                 const std::function<void(void *data, size_t pitch)> &fnUpdate);

    /// @brief Renders a texture to another texture. The destination texture must be a render target.
    /// @param[in] src the source texture
    /// @param[in] dst the destination texture
    /// @param[in] srcRect the source region to copy from
    /// @param[in] dstRect the destination region to copy to
    /// @return nothing on success, an error message on failure
    gfx::GfxResult RenderToTexture(gfx::GUITextureHandle src, gfx::GUITextureHandle dst, const gfx::FRect &srcRect,
                                   const gfx::FRect &dstRect);

    /// @brief Retrieves the ImGui texture ID for the given texture handle.
    /// @param[in] handle the texture
    /// @return the corresponding ImGui texture ID
    ImTextureID GetImGuiTextureID(gfx::GUITextureHandle handle) const;

    /// @brief Destroys a managed texture.
    /// @param[in] handle the texture handle
    /// @return `true` if the texture was destroyed, `false` if it wasn't registered.
    bool DestroyTexture(gfx::GUITextureHandle handle);

    /// @brief Changes the frame presentation mode.
    /// @param[in] mode the new frame presentation mode
    /// @return nothing on success, an error message on failure
    gfx::GfxResult SetPresentMode(gfx::PresentMode mode);

    /// @brief Presents the next frame.
    gfx::GfxResult Present();

private:
    std::unique_ptr<gfx::IGraphicsContext> m_gfxContext;
};

} // namespace app::services
