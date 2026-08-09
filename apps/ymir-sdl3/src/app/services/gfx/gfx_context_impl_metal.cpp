#include "gfx_context_impl_metal.hpp"

#include "gfx_result.hpp"

namespace app::gfx {

GfxObjectResult<MetalGraphicsContext> MetalGraphicsContext::Create(const MetalGraphicsContextSpec &spec) {
    return GfxOperationError{"Unimplemented"};
}

void MetalGraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    // TODO: enqueue command to clear screen
}

bool MetalGraphicsContext::ImGuiInit() {
    // TODO: invoke the appropriate ImGui_Impl*_Init* functions
    return false;
}

void MetalGraphicsContext::ImGuiShutdown() {
    // TODO: invoke the appropriate ImGui_Impl*_Shutdown* functions
}

void MetalGraphicsContext::ImGuiNewFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_NewFrame functions
}

void MetalGraphicsContext::ImGuiRenderFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_RenderDrawData function
}

ImTextureID MetalGraphicsContext::GetImGuiTextureID(TextureID textureID) const {
    // TODO: get and return texture ID
    return 0;
}

GfxResult MetalGraphicsContext::SetPresentMode(PresentMode mode) {
    return GfxOperationError{"Unimplemented"};
}

GfxResult MetalGraphicsContext::Present() {
    return GfxOperationError{"Unimplemented"};
}

} // namespace app::gfx
