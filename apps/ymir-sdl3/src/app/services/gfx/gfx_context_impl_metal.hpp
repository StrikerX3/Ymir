#pragma once

#include "gfx_context.hpp"
#include "gfx_result.hpp"

namespace app::gfx {

struct MetalGraphicsContextSpec {};

class MetalGraphicsContext final : public IGraphicsContext {
public:
    /// @brief Creates a Metal graphics context.
    /// @param[in] spec the backend specifications
    /// @return the graphics context instance or an error message
    static GfxObjectResult<MetalGraphicsContext> Create(const MetalGraphicsContextSpec &spec);

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
