#pragma once

#include "gfx_result.hpp"
#include "gfx_types.hpp"

#include <imgui.h>

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

    /// @brief Retrieves the type of the backend of this graphics context instance.
    /// @return this graphics context's backend type
    Backend GetBackend() const {
        return m_backend;
    }

    /// @brief Clears the screen with the specified color.
    /// @param[in] color the clear color
    virtual void ClearScreen(gfx::ColorRGBA color) = 0;

    /// @brief Initializes ImGui.
    /// @return `true` if successfully initialized (or already initialized), `false` otherwise
    virtual bool ImGuiInit() = 0;

    /// @brief Shuts down ImGui.
    virtual void ImGuiShutdown() = 0;

    /// @brief Starts a new ImGui frame
    virtual void ImGuiNewFrame() = 0;

    /// @brief Renders the current ImGui frame
    virtual void ImGuiRenderFrame() = 0;

    /// @brief Retrieves the ImGui texture ID for the given texture ID.
    /// @param[in] textureID the texture ID
    /// @return the `ImTextureID` corresponding to the texture
    virtual ImTextureID GetImGuiTextureID(TextureID textureID) const = 0;

    /// @brief Changes the frame presentation mode.
    /// @param[in] mode the new frame presentation mode
    /// @return nothing on success, an error message on failure
    virtual GfxResult SetPresentMode(PresentMode mode) = 0;

    /// @brief Presents the next frame.
    /// @return nothing on success, an error message on failure
    virtual GfxResult Present() = 0;

private:
    Backend m_backend;
};

} // namespace app::gfx
