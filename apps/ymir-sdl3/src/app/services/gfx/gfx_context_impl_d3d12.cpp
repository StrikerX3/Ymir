#include "gfx_context_impl_d3d12.hpp"

#include "gfx_result.hpp"

namespace app::gfx {

GfxObjectResult<Direct3D12GraphicsContext>
Direct3D12GraphicsContext::Create(const Direct3D12GraphicsContextSpec &spec) {
    return GfxOperationError{"Unimplemented"};
}

void Direct3D12GraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    // TODO: enqueue command to clear screen
}

bool Direct3D12GraphicsContext::ImGuiInit() {
    // TODO: invoke the appropriate ImGui_Impl*_Init* functions
    return false;
}

void Direct3D12GraphicsContext::ImGuiShutdown() {
    // TODO: invoke the appropriate ImGui_Impl*_Shutdown* functions
}

void Direct3D12GraphicsContext::ImGuiNewFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_NewFrame functions
}

void Direct3D12GraphicsContext::ImGuiRenderFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_RenderDrawData function
}

ImTextureID Direct3D12GraphicsContext::GetImGuiTextureID(TextureID textureID) const {
    // TODO: get and return texture ID
    return 0;
}

GfxResult Direct3D12GraphicsContext::SetPresentMode(PresentMode mode) {
    return GfxOperationError{"Unimplemented"};
}

GfxResult Direct3D12GraphicsContext::Present() {
    return GfxOperationError{"Unimplemented"};
}

} // namespace app::gfx
