#pragma once

#include "gfx_context.hpp"
#include "gfx_result.hpp"

namespace app::gfx {

struct Direct3D12GraphicsContextSpec {};

class Direct3D12GraphicsContext final : public IGraphicsContext {
public:
    /// @brief Creates a Direct3D 12 graphics context.
    /// @param[in] spec the backend specifications
    /// @return the graphics context instance or an error message
    static GfxObjectResult<Direct3D12GraphicsContext> Create(const Direct3D12GraphicsContextSpec &spec);

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
