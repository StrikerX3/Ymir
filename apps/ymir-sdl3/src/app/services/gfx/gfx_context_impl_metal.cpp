#include "gfx_context_impl_metal.hpp"

namespace app::gfx {

MetalGraphicsContext::MetalGraphicsContext()
    : IGraphicsContext(kBackend) {}

util::ObjectResult<MetalGraphicsContext> MetalGraphicsContext::Create(const MetalGraphicsContextSpec &spec) {
    return util::ErrorMessage{"Unimplemented"};
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

util::ValueResult<TextureID> MetalGraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    // TODO: create and store texture object in a hash map
    // The texture ID will be the hash map key, not the native object pointer, because resizing the texture requires
    // creating a new object and these IDs must be immutable for the lifetime of the logical texture.
    return util::ErrorMessage{"Unimplemented"};
}

void MetalGraphicsContext::DestroyTexture(TextureID id) {
    // TODO: delete texture
}

bool MetalGraphicsContext::IsTextureValid(TextureID id) const {
    // TODO: check if the texture is still live
    return true;
}

ImTextureID MetalGraphicsContext::GetImGuiTextureID(TextureID id) const {
    // TODO: get and return texture ID
    return 0;
}

util::VoidResult<> MetalGraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    // TODO: destroy and recreate texture with new dimensions
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> MetalGraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                                       const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    // TODO: map texture, invoke fnUpdate with contents, unmap texture; handle errors
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> MetalGraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                         const FRect &dstRect) {
    // TODO: set render target to dst texture, draw texture, restore render target
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> MetalGraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect, const FRect &dstRect,
                                                            double rotAngle, const FPoint2D *anchorPoint) {
    // TODO: imitate SDL_RenderTextureRotated:
    // - srcRect specifies the source texture region to copy from (in texels)
    // - dstRect specifies the destination texture region to copy to (in texels)
    // - rotAngle is the clockwise rotation angle (in degrees)
    // - anchorPoint is the rotation anchor point. If null, use the center of the destination rectangle
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> MetalGraphicsContext::SetPresentMode(PresentMode mode) {
    // TODO: set presentation mode
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> MetalGraphicsContext::Present() {
    // TODO: present next frame and wait for vertical retrace if enabled
    return util::ErrorMessage{"Unimplemented"};
}

} // namespace app::gfx
