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

GfxValueResult<TextureID> VulkanGraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    // TODO: create and store texture object in a hash map
    // The texture ID will be the hash map key, not the native object pointer, because resizing the texture requires
    // creating a new object and these IDs must be immutable for the lifetime of the logical texture.
    return GfxOperationError{"Unimplemented"};
}

void VulkanGraphicsContext::DestroyTexture(TextureID id) {
    // TODO: delete texture
}

bool VulkanGraphicsContext::IsTextureValid(TextureID id) const {
    // TODO: check if the texture is still live
    return true;
}

ImTextureID VulkanGraphicsContext::GetImGuiTextureID(TextureID id) const {
    // TODO: get and return texture ID
    return 0;
}

GfxResult VulkanGraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    // TODO: destroy and recreate texture with new dimensions
    return GfxOperationError{"Unimplemented"};
}

GfxResult VulkanGraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                               const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    // TODO: map texture, invoke fnUpdate with contents, unmap texture; handle errors
    return GfxOperationError{"Unimplemented"};
}

GfxResult VulkanGraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                 const FRect &dstRect) {
    // TODO: set render target to dst texture, draw texture, restore render target
    return GfxOperationError{"Unimplemented"};
}

GfxResult VulkanGraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect,
                                                    double rotAngle, const FPoint2D *anchorPoint) {
    // TODO: imitate SDL_RenderTextureRotated:
    // - srcRect specifies the source texture region to copy from (in texels)
    // - dstRect specifies the destination texture region to copy to (in texels)
    // - rotAngle is the clockwise rotation angle (in degrees)
    // - anchorPoint is the rotation anchor point. If null, use the center of the destination rectangle
    return GfxOperationError{"Unimplemented"};
}

GfxResult VulkanGraphicsContext::SetPresentMode(PresentMode mode) {
    // TODO: set presentation mode
    return GfxOperationError{"Unimplemented"};
}

GfxResult VulkanGraphicsContext::Present() {
    // TODO: present next frame and wait for vertical retrace if enabled
    return GfxOperationError{"Unimplemented"};
}

} // namespace app::gfx
