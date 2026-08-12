#pragma once

#include <app/settings.hpp>

#include "gfx/gfx_context.hpp"
#include "gfx/gfx_gui_types.hpp"

#include <ymir/util/result.hpp>

#include <SDL3/SDL_video.h>

#include <imgui.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace app::services {

/// @brief Provides services for managing graphics resources.
class GraphicsService {
public:
    GraphicsService(Settings &settings);
    ~GraphicsService();

    /// @brief Initializes a graphics context.
    /// `gfx::Backend::Null` cannot be created this way. Use `DestroyGraphicsContext()` to use it.
    /// @param[in] backend the graphics backend
    /// @param[in] window the window on which to render graphics
    /// @param[in] presentMode the initial presentation mode
    /// @return nothing on success, an error message on failure
    util::VoidResult<> InitGraphicsContext(gfx::Backend backend, SDL_Window *window, gfx::PresentMode presentMode);

private:
    util::ObjectResult<gfx::IGraphicsContext> CreateGraphicsContext(gfx::Backend backend, SDL_Window *window);

public:
    /// @brief Destroys the graphics context, effectively replacing it with a null context.
    void DestroyGraphicsContext();

    /// @brief Reverts the graphics backend to one of the safe options: the default for the platform or SDL Renderer.
    /// This should be invoked in case the application fails to start up when attempting to initialize graphics
    /// resources after the context was successfully initialized. This is a safeguard against potential graphics driver
    /// issues, limitations, programming oversights or bugs.
    void RevertGraphicsBackend();

    /// @brief Retrieves the current graphics context's backend type.
    /// @return the current graphics backend in use
    gfx::Backend GetGraphicsContextBackend() const {
        return m_gfxContext->GetBackend();
    }

    /// @brief Retrieves a reference to the currently instantiated graphics context.
    /// This is intended to grant access to low-level graphics API objects where needed.
    /// @return a reference to the graphics context
    const gfx::IGraphicsContext &GetGraphicsContext() const {
        return *m_gfxContext;
    }

    /// @brief Resizes the framebuffer to the specified dimensions.
    /// @param[in] width the new framebuffer width
    /// @param[in] height the new framebuffer height
    /// @return nothing on success, an error message on failure
    util::VoidResult<> ResizeFramebuffer(uint32 width, uint32 height);

    /// @brief Clears the screen with the specified color.
    /// @param[in] color the clear color
    void ClearScreen(gfx::ColorRGBA color);

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

    /// @brief Creates and registers a 2D texture.
    /// Once created, the texture is automatically recreated when the backend is changed through.
    /// @param[in] spec texture format specifications
    /// @param[in] fnSetup texture setup function, invoked upon texture creation and recreation
    /// @return a handle to the texture, or an error message if the texture could not be created
    util::ValueResult<gfx::GUITextureHandle> CreateTexture(
        const gfx::Texture2DSpec &spec,
        gfx::FnTextureSetup &&fnSetup = [](gfx::GUITextureHandle, bool, void *, size_t) {});

    /// @brief Checks if the texture handle is valid.
    /// @param[in] handle the texture handle to check
    /// @return `true` if the handle refers to a valid managed texture, `false` otherwise.
    bool IsTextureHandleValid(gfx::GUITextureHandle handle) const;

    /// @brief Resizes the texture to the new dimensions.
    /// @param[in] handle the texture handle
    /// @param[in] width the new width
    /// @param[in] height the new height
    /// @return nothing on success, an error message on failure
    util::VoidResult<> ResizeTexture(gfx::GUITextureHandle handle, uint32 width, uint32 height);

    /// @brief Updates the contents of a texture.
    /// @param[in] handle the texture handle
    /// @param[in,opt] rect the target region to update; `nullptr` updates the entire texture
    /// @param[in] fnUpdate the update function, taking a pointer to writable texture data and the line pitch in bytes.
    /// This buffer should not be read by the CPU.
    /// @return nothing on success, an error message on failure
    util::VoidResult<> UpdateTexture(gfx::GUITextureHandle handle, const gfx::IRect *rect,
                                     const std::function<void(void *data, size_t pitch)> &fnUpdate);

    /// @brief Renders a texture to another texture. The destination texture must be a render target.
    /// @param[in] src the source texture
    /// @param[in] dst the destination texture
    /// @param[in] srcRect the source region to copy from
    /// @param[in] dstRect the destination region to copy to
    /// @return nothing on success, an error message on failure
    util::VoidResult<> RenderToTexture(gfx::GUITextureHandle src, gfx::GUITextureHandle dst, const gfx::FRect &srcRect,
                                       const gfx::FRect &dstRect);

    /// @brief Draws a texture rotated about the given anchor point.
    /// @param[in] handle the texture to draw
    /// @param[in] srcRect portion of the texture to draw
    /// @param[in] dstRect where to draw the texture on the screen
    /// @param[in] rotAngle clockwise rotation amount (in degrees)
    /// @param[in,opt] anchorPoint rotation anchor point. If `nullptr`, rotates about the center of the texture
    /// @return nothing on success, an error message on failure
    util::VoidResult<> DrawTextureRotated(gfx::GUITextureHandle handle, const gfx::FRect &srcRect,
                                          const gfx::FRect &dstRect, double rotAngle,
                                          const gfx::FPoint2D *anchorPoint = nullptr);

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
    util::VoidResult<> SetPresentMode(gfx::PresentMode mode);

    /// @brief Presents the next frame.
    util::VoidResult<> Present();

private:
    Settings &m_settings;
    std::unique_ptr<gfx::IGraphicsContext> m_gfxContext;

    bool m_imguiInitialized = false;

    struct Texture2DInstance {
        gfx::TextureID id;
        gfx::Texture2DSpec spec;
        gfx::FnTextureSetup fnSetup;
    };
    std::unordered_map<gfx::GUITextureHandle, Texture2DInstance> m_textures;
    std::vector<gfx::GUITextureHandle> m_freeTexHandles;
    gfx::GUITextureHandle m_nextHandle = 0;

    gfx::GUITextureHandle GetNextTextureHandle();
    Texture2DInstance *GetTexture(gfx::GUITextureHandle handle);
    const Texture2DInstance *GetTexture(gfx::GUITextureHandle handle) const;
    util::VoidResult<> RecreateTextures();
};

} // namespace app::services
