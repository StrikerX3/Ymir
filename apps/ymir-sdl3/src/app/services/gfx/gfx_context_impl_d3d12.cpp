#include "gfx_context_impl_d3d12.hpp"

#include "gfx_context_spec_d3d12.hpp"

#include <d3d12.h>

#include <wil/com.h>

namespace app::gfx {

struct Direct3D12GraphicsContext::Impl {
    util::VoidResult<> Create(const Direct3D12GraphicsContextSpec &spec) {
        // TODO: create objects; handle errors

        // return {};
        return util::ErrorMessage{"Unimplemented"};
    }

    wil::com_ptr_nothrow<ID3D12Device> device;
};

// -----------------------------------------------------------------------------

Direct3D12GraphicsContext::Direct3D12GraphicsContext(std::unique_ptr<Impl> &&impl)
    : IGraphicsContext(kBackend)
    , m_impl(std::move(impl)) {}

Direct3D12GraphicsContext::~Direct3D12GraphicsContext() = default;

util::ObjectResult<Direct3D12GraphicsContext>
Direct3D12GraphicsContext::Create(const Direct3D12GraphicsContextSpec &spec) {
    auto impl = std::make_unique<Impl>();
    if (!impl) {
        return util::ErrorMessage{"Could not allocate memory for Direct3D 12 graphics context"};
    }

    auto result = impl->Create(spec);
    if (!result) {
        return result.Error();
    }

    return std::make_unique<Direct3D12GraphicsContext>(std::move(impl));
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

util::ValueResult<TextureID> Direct3D12GraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    // TODO: create and store texture object in a hash map
    // The texture ID will be the hash map key, not the native object pointer, because resizing the texture requires
    // creating a new object and these IDs must be immutable for the lifetime of the logical texture.
    return util::ErrorMessage{"Unimplemented"};
}

void Direct3D12GraphicsContext::DestroyTexture(TextureID id) {
    // TODO: delete texture
}

bool Direct3D12GraphicsContext::IsTextureValid(TextureID id) const {
    // TODO: check if the texture is still live
    return true;
}

ImTextureID Direct3D12GraphicsContext::GetImGuiTextureID(TextureID id) const {
    // TODO: get and return texture ID
    return 0;
}

util::VoidResult<> Direct3D12GraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    // TODO: destroy and recreate texture with new dimensions
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<>
Direct3D12GraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                         const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    // TODO: map texture, invoke fnUpdate with contents, unmap texture; handle errors
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                              const FRect &dstRect) {
    // TODO: set render target to dst texture, draw texture, restore render target
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect,
                                                                 const FRect &dstRect, double rotAngle,
                                                                 const FPoint2D *anchorPoint) {
    // TODO: imitate SDL_RenderTextureRotated:
    // - srcRect specifies the source texture region to copy from (in texels)
    // - dstRect specifies the destination texture region to copy to (in texels)
    // - rotAngle is the clockwise rotation angle (in degrees)
    // - anchorPoint is the rotation anchor point. If null, use the center of the destination rectangle
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::SetPresentMode(PresentMode mode) {
    // TODO: set presentation mode
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::Present() {
    // TODO: present next frame and wait for vertical retrace if enabled
    return util::ErrorMessage{"Unimplemented"};
}

wil::com_ptr_nothrow<ID3D12Device> Direct3D12GraphicsContext::GetDevice() const {
    return m_impl->device;
}

} // namespace app::gfx
