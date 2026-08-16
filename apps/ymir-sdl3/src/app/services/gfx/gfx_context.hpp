#pragma once

#include "gfx_types.hpp"

#include <ymir/util/result.hpp>

#include <imgui.h>

#include <functional>
#include <vector>

namespace app::gfx {

/// @brief Interface for platform graphics contexts.
/// Creates and manages a rendering context and grants access to raw API objects for more advanced graphics operations.
/// Use one of the platform factory methods to create one.
class IGraphicsContext {
protected:
    IGraphicsContext(Backend backend)
        : m_backend(backend) {}

public:
    virtual ~IGraphicsContext() = default;

    /// @brief Initializes the graphics context if not already initialized.
    /// @return nothing on success, an error message on failure
    virtual util::VoidResult<> Initialize() = 0;

    /// @brief Shuts down the graphics context, keeping the current settings intact.
    /// The context can be reinitialized with `Init()`.
    virtual void Shutdown() = 0;

    /// @brief Checks if the graphics context is properly initialized.
    /// @return `true` if the graphics context is initialized, `false` if not
    virtual bool IsInitialized() const = 0;

    /// @brief Retrieves the type of the backend of this graphics context instance.
    /// @return this graphics context's backend type
    Backend GetBackend() const {
        return m_backend;
    }

    /// @brief Dynamically cases the object to the specified target type.
    /// @tparam T the target type, derived from `TBase`
    /// @return this object cast to `T` if it is of that type, `nullptr` otherwise
    template <typename T>
        requires std::derived_from<T, IGraphicsContext> &&
                 std::same_as<Backend, std::decay_t<std::remove_cvref_t<decltype(T::kBackend)>>>
    T *As() {
        if (T::kBackend == m_backend) {
            return static_cast<T *>(this);
        }
        return nullptr;
    }

    /// @brief Dynamically cases the object to the specified target type.
    /// @tparam T the target type, derived from `TBase`
    /// @return this object cast to `T` if it is of that type, `nullptr` otherwise
    template <typename T>
        requires std::derived_from<T, IGraphicsContext> &&
                 std::same_as<Backend, std::decay_t<std::remove_cvref_t<decltype(T::kBackend)>>>
    const T *As() const {
        if (T::kBackend == m_backend) {
            return static_cast<const T *>(this);
        }
        return nullptr;
    }

    /// @brief Resizes the framebuffer to the specified dimensions.
    /// @param[in] width the new framebuffer width
    /// @param[in] height the new framebuffer height
    /// @return nothing on success, an error message on failure
    virtual util::VoidResult<> ResizeFramebuffer(uint32 width, uint32 height) = 0;

    /// @brief Clears the screen with the specified color.
    /// @param[in] color the clear color
    virtual void ClearScreen(ColorRGBA color) = 0;

    /// @brief Initializes ImGui.
    /// @return `true` if successfully initialized (or already initialized), `false` otherwise
    virtual bool ImGuiInit() = 0;

    /// @brief Shuts down ImGui.
    virtual void ImGuiShutdown() = 0;

    /// @brief Starts a new ImGui frame.
    virtual void ImGuiNewFrame() = 0;

    /// @brief Renders the current ImGui frame.
    virtual void ImGuiRenderFrame() = 0;

    /// @brief Creates a 2D texture.
    /// @param[in] spec the 2D texture specifications
    /// @return texture identifier, or an error message if failed to create
    virtual util::ValueResult<TextureID> CreateTexture(const Texture2DSpec &spec) = 0;

    /// @brief Destroys the specified texture.
    /// @param[in] id the texture ID
    virtual void DestroyTexture(TextureID id) = 0;

    /// @brief Retrieves the ImGui texture ID for the given texture ID.
    /// @param[in] id the texture ID
    /// @return the `ImTextureID` corresponding to the texture
    virtual ImTextureID GetImGuiTextureID(TextureID id) const = 0;

    /// @brief Determines if the texture with the given ID is valid.
    /// @param[in] id the texture ID
    /// @return `true` if the ID refers to a valid texture, `false` otherwise
    virtual bool IsTextureValid(TextureID id) const = 0;

    /// @brief Resizes the texture to the new dimensions.
    /// @param[in] id the texture ID
    /// @param[in] width the new width
    /// @param[in] height the new height
    /// @return nothing on success, an error message on failure
    virtual util::VoidResult<> ResizeTexture(TextureID id, uint32 width, uint32 height) = 0;

    /// @brief Resizes the texture to the new dimensions.
    /// @param[in] id the texture ID
    /// @param[in,opt] rect the target region to update; `nullptr` updates the entire texture
    /// @param[in] fnUpdate the update function, taking a pointer to writable texture data and the line pitch in bytes.
    /// This buffer should not be read by the CPU.
    /// @return nothing on success, an error message on failure
    virtual util::VoidResult<> UpdateTexture(TextureID id, const IRect *rect,
                                             const std::function<void(void *data, size_t pitch)> &fnUpdate) = 0;

    /// @brief Renders a texture to another texture. The destination texture must be a render target.
    /// @param[in] src the source texture ID
    /// @param[in] dst the destination texture ID
    /// @param[in] srcRect the source region to copy from
    /// @param[in] dstRect the destination region to copy to
    /// @return nothing on success, an error message on failure
    virtual util::VoidResult<> RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                               const FRect &dstRect) = 0;

    /// @brief Draws a texture rotated about the given anchor point.
    /// @param[in] texture the texture ID to draw
    /// @param[in] srcRect portion of the texture to draw
    /// @param[in] dstRect where to draw the texture on the screen
    /// @param[in] rotAngle clockwise rotation amount (in degrees)
    /// @param[in,opt] anchorPoint rotation anchor point. If `nullptr`, rotates about the center of the texture. (0, 0)
    /// is the top-left corner of the texture.
    /// @return nothing on success, an error message on failure
    virtual util::VoidResult<> DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect,
                                                  double rotAngle, const FPoint2D *anchorPoint = nullptr) = 0;

    /// @brief Changes the frame presentation mode.
    /// @param[in] mode the new frame presentation mode
    /// @return nothing on success, an error message on failure
    virtual util::VoidResult<> SetPresentMode(PresentMode mode) = 0;

    /// @brief Presents the next frame.
    /// @return presentation result on success, an error message on failure.
    /// If failed, it's highly likely that the device was destroyed and needs to be reinitialized.
    virtual util::ValueResult<PresentResult> Present() = 0;

protected:
    /// @brief Retrieves the next free texture ID.
    /// @return a free texture ID
    TextureID GetNextTextureID();

    /// @brief Releases the given texture ID for reuse.
    /// @param[in] id the texture ID to free
    void FreeTextureID(TextureID id);

private:
    Backend m_backend;

    std::vector<TextureID> m_freeTextureIDs;
    TextureID m_nextTextureID = 0;
};

} // namespace app::gfx
