#include "gfx_context_impl_vulkan.hpp"

#include "gfx_result.hpp"

namespace app::gfx {

GfxObjectResult<VulkanGraphicsContext> VulkanGraphicsContext::Create(const VulkanGraphicsContextSpec &spec) {
    return GfxOperationError{"Unimplemented"};
}

void VulkanGraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    // TODO: enqueue command to clear screen
}

bool VulkanGraphicsContext::ImGuiInit() {
    // TODO: invoke the appropriate ImGui_Impl*_Init* functions
    return false;
}

void VulkanGraphicsContext::ImGuiShutdown() {
    // TODO: invoke the appropriate ImGui_Impl*_Shutdown* functions
}

void VulkanGraphicsContext::ImGuiNewFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_NewFrame functions
}

void VulkanGraphicsContext::ImGuiRenderFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_RenderDrawData function
}

ImTextureID VulkanGraphicsContext::GetImGuiTextureID(TextureID textureID) const {
    // TODO: get and return texture ID
    return 0;
}

GfxResult VulkanGraphicsContext::SetPresentMode(PresentMode mode) {
    return GfxOperationError{"Unimplemented"};
}

GfxResult VulkanGraphicsContext::Present() {
    return GfxOperationError{"Unimplemented"};
}

} // namespace app::gfx
