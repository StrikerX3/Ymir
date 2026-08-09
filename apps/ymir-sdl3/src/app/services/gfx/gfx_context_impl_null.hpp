#pragma once

#include "gfx_context.hpp"

namespace app::gfx {

class NullGraphicsContext final : public IGraphicsContext {
public:
    NullGraphicsContext()
        : IGraphicsContext(Backend::Null) {}

    void ClearScreen(gfx::ColorRGBA color) override {}

    bool ImGuiInit() override {
        return false;
    }
    void ImGuiShutdown() override {}
    void ImGuiNewFrame() override {}
    void ImGuiRenderFrame() override {}

    GfxValueResult<TextureID> CreateTexture(const Texture2DSpec &spec) override {
        return UnimplementedError();
    }
    void DestroyTexture(TextureID id) override {}
    bool IsTextureValid(TextureID id) const override {
        return false;
    }
    ImTextureID GetImGuiTextureID(TextureID id) const override {
        return 0;
    }
    GfxResult ResizeTexture(TextureID id, uint32 width, uint32 height) override {
        return UnimplementedError();
    }
    GfxResult UpdateTexture(TextureID id, const IRect *rect,
                            const std::function<void(void *data, size_t pitch)> &fnUpdate) override {
        return UnimplementedError();
    }
    GfxResult RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect, const FRect &dstRect) override {
        return UnimplementedError();
    }
    GfxResult DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect, double rotAngle,
                                 const FPoint2D *anchorPoint = nullptr) override {
        return UnimplementedError();
    }

    GfxResult SetPresentMode(PresentMode mode) override {
        return UnimplementedError();
    }
    GfxResult Present() override {
        return UnimplementedError();
    }

private:
    static GfxOperationError UnimplementedError() {
        return GfxOperationError{"Unimplemented"};
    }
};

} // namespace app::gfx
