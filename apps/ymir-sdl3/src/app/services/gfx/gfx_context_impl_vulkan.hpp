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
};

} // namespace app::gfx
