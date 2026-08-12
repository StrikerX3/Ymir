#include "gfx_context_impl_d3d11.hpp"

namespace app::gfx {

Direct3D11GraphicsContext::Direct3D11GraphicsContext()
    : IGraphicsContext(kBackend) {}

util::ObjectResult<Direct3D11GraphicsContext>
Direct3D11GraphicsContext::Create(const Direct3D11GraphicsContextSpec &spec) {
    auto context = std::make_unique<Direct3D11GraphicsContext>();
    auto result = context->Initialize();
    if (!result) {
        return result.Error();
    }
    return std::move(context);
}

util::VoidResult<> Direct3D11GraphicsContext::Initialize() {
    return util::ErrorMessage{"Unimplemented"};
}

void Direct3D11GraphicsContext::Shutdown() {}

bool Direct3D11GraphicsContext::IsInitialized() const {
    return false;
}

util::VoidResult<> Direct3D11GraphicsContext::ResizeFramebuffer(uint32 width, uint32 height) {
    // TODO: destroy and recreate swap chain resources
    return util::ErrorMessage{"Unimplemented"};
}

void Direct3D11GraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    // TODO: enqueue command to clear screen
}

bool Direct3D11GraphicsContext::ImGuiInit() {
    // TODO: invoke the appropriate ImGui_Impl*_Init* functions
    return false;
}

void Direct3D11GraphicsContext::ImGuiShutdown() {
    // TODO: invoke the appropriate ImGui_Impl*_Shutdown* functions
}

void Direct3D11GraphicsContext::ImGuiNewFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_NewFrame functions
}

void Direct3D11GraphicsContext::ImGuiRenderFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_RenderDrawData function
}

util::ValueResult<TextureID> Direct3D11GraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    // TODO: create and store texture object in a hash map
    // The texture ID will be the hash map key, not the native object pointer, because resizing the texture requires
    // creating a new object and these IDs must be immutable for the lifetime of the logical texture.
    return util::ErrorMessage{"Unimplemented"};
}

void Direct3D11GraphicsContext::DestroyTexture(TextureID id) {
    // TODO: delete texture
}

bool Direct3D11GraphicsContext::IsTextureValid(TextureID id) const {
    // TODO: check if the texture is still live
    return true;
}

ImTextureID Direct3D11GraphicsContext::GetImGuiTextureID(TextureID id) const {
    // TODO: get and return texture ID
    return 0;
}

util::VoidResult<> Direct3D11GraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    // TODO: destroy and recreate texture with new dimensions
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<>
Direct3D11GraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                         const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    // TODO: map texture, invoke fnUpdate with contents, unmap texture; handle errors
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D11GraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                              const FRect &dstRect) {
    // TODO: set render target to dst texture, draw texture, restore render target
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D11GraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect,
                                                                 const FRect &dstRect, double rotAngle,
                                                                 const FPoint2D *anchorPoint) {
    // TODO: imitate SDL_RenderTextureRotated:
    // - srcRect specifies the source texture region to copy from (in texels)
    // - dstRect specifies the destination texture region to copy to (in texels)
    // - rotAngle is the clockwise rotation angle (in degrees)
    // - anchorPoint is the rotation anchor point. If null, use the center of the destination rectangle
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D11GraphicsContext::SetPresentMode(PresentMode mode) {
    // TODO: set presentation mode
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D11GraphicsContext::Present() {
    // TODO: present next frame and wait for vertical retrace if enabled
    return util::ErrorMessage{"Unimplemented"};
}

} // namespace app::gfx
