#pragma once

#include "gfx_context.hpp"

#include <wil/com.h>

// -----------------------------------------------------------------------------
// Forward declarations

struct ID3D12Device;

// -----------------------------------------------------------------------------
// Implementation

namespace app::gfx {

struct Direct3D12GraphicsContextSpec;

class Direct3D12GraphicsContext final : public IGraphicsContext {
    struct Impl;

public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    Direct3D12GraphicsContext(std::unique_ptr<Impl> &&impl);
    ~Direct3D12GraphicsContext();

    /// @brief Creates a Direct3D 12 graphics context.
    /// @param[in] spec the backend specifications
    /// @return the graphics context instance or an error message
    static util::ObjectResult<Direct3D12GraphicsContext> Create(const Direct3D12GraphicsContextSpec &spec);

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

    /// @brief Retrieves a pointer to the `ID3D12Device` managed by this graphics context.
    /// @return a reference-counted pointer to the context's Direct3D 12 device instance
    wil::com_ptr_nothrow<ID3D12Device> GetDevice() const;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace app::gfx
