#pragma once

#include "gfx_context.hpp"

namespace app::gfx {

class NullGraphicsContext final : public IGraphicsContext {
public:
    static constexpr Backend kBackend = Backend::Null;

    NullGraphicsContext()
        : IGraphicsContext(kBackend) {}

    void ClearScreen(gfx::ColorRGBA color) override {}

    bool ImGuiInit() override {
        return false;
    }
    void ImGuiShutdown() override {}
    void ImGuiNewFrame() override {}
    void ImGuiRenderFrame() override {}

    util::ValueResult<TextureID> CreateTexture(const Texture2DSpec &spec) override {
        return UnimplementedError();
    }
    void DestroyTexture(TextureID id) override {}
    bool IsTextureValid(TextureID id) const override {
        return false;
    }
    ImTextureID GetImGuiTextureID(TextureID id) const override {
        return 0;
    }
    util::VoidResult<> ResizeTexture(TextureID id, uint32 width, uint32 height) override {
        return UnimplementedError();
    }
    util::VoidResult<> UpdateTexture(TextureID id, const IRect *rect,
                                     const std::function<void(void *data, size_t pitch)> &fnUpdate) override {
        return UnimplementedError();
    }
    util::VoidResult<> RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                       const FRect &dstRect) override {
        return UnimplementedError();
    }
    util::VoidResult<> DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect, double rotAngle,
                                          const FPoint2D *anchorPoint = nullptr) override {
        return UnimplementedError();
    }

    util::VoidResult<> SetPresentMode(PresentMode mode) override {
        return UnimplementedError();
    }
    util::VoidResult<> Present() override {
        return UnimplementedError();
    }

private:
    static util::ErrorMessage UnimplementedError() {
        return util::ErrorMessage{"Unimplemented"};
    }
};

} // namespace app::gfx
