#pragma once

#include "gfx_context.hpp"

namespace app::gfx {

struct Direct3D11GraphicsContextSpec {};

class Direct3D11GraphicsContext final : public IGraphicsContext {
public:
    static constexpr Backend kBackend = Backend::Direct3D11;

    Direct3D11GraphicsContext();

    /// @brief Creates a Direct3D 11 graphics context.
    /// @param[in] spec the backend specifications
    /// @return the graphics context instance or an error message
    static util::ObjectResult<Direct3D11GraphicsContext> Create(const Direct3D11GraphicsContextSpec &spec);

    util::VoidResult<> Initialize() override;
    void Shutdown() override;
    bool IsInitialized() const override;

    util::VoidResult<> ResizeFramebuffer(uint32 width, uint32 height) override;

    util::VoidResult<> BeginFrame() override;
    util::VoidResult<> EndFrame() override;

    void ClearScreen(gfx::ColorRGBA color) override;

    bool ImGuiInit() override;
    void ImGuiShutdown() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderFrame() override;

    util::ValueResult<TextureID> CreateTexture(const Texture2DSpec &spec) override;
    void DestroyTexture(TextureID id) override;
    bool IsTextureValid(TextureID id) const override;
    ImTextureID GetImGuiTextureID(TextureID id) const override;
    util::VoidResult<> ResizeTexture(TextureID id, uint32 width, uint32 height) override;
    util::VoidResult<> UpdateTexture(TextureID id, const IRect *rect,
                                     const std::function<void(void *data, size_t pitch)> &fnUpdate) override;
    util::VoidResult<> RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                       const FRect &dstRect) override;
    util::VoidResult<> DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect, double rotAngle,
                                          const FPoint2D *anchorPoint = nullptr) override;

    util::VoidResult<> SetPresentMode(PresentMode mode) override;
    util::VoidResult<> Present() override;

private:
};

} // namespace app::gfx
