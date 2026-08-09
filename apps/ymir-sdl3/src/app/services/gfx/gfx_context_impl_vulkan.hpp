#pragma once

#include "gfx_context.hpp"
#include "gfx_result.hpp"

namespace app::gfx {

struct VulkanGraphicsContextSpec {};

class VulkanGraphicsContext final : public IGraphicsContext {
public:
    /// @brief Creates a Vulkan graphics context.
    /// @param[in] spec the backend specifications
    /// @return the graphics context instance or an error message
    static GfxObjectResult<VulkanGraphicsContext> Create(const VulkanGraphicsContextSpec &spec);

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
