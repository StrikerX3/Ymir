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

    ImTextureID GetImGuiTextureID(TextureID textureID) const override {
        return 0;
    }

    GfxResult SetPresentMode(PresentMode mode) override {
        return GfxOperationError{"Unimplemented"};
    }
    GfxResult Present() override {
        return GfxOperationError{"Unimplemented"};
    }

private:
};

} // namespace app::gfx
