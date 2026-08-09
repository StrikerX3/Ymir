#pragma once

#include "gfx_context.hpp"
#include "gfx_result.hpp"

namespace app::gfx {

struct Direct3D11GraphicsContextSpec {};

class Direct3D11GraphicsContext final : public IGraphicsContext {
public:
    /// @brief Creates a Direct3D 11 graphics context.
    /// @param[in] spec the backend specifications
    /// @return the graphics context instance or an error message
    static GfxObjectResult<Direct3D11GraphicsContext> Create(const Direct3D11GraphicsContextSpec &spec);

    void ClearScreen(gfx::ColorRGBA color) override;

    bool ImGuiInit() override;
    void ImGuiShutdown() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderFrame() override;

    ImTextureID GetImGuiTextureID(TextureID textureID) const override;

    GfxResult SetPresentMode(PresentMode mode) override;
    GfxResult Present() override;

private:
};

} // namespace app::gfx
