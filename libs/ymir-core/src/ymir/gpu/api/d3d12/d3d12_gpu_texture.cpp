#include <ymir/gpu/api/d3d12/d3d12_gpu_texture.hpp>

#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

#include <ymir/util/unreachable.hpp>

namespace ymir::gpu {

D3D12Texture::D3D12Texture(d3d12::D3D12Device &device, const TextureSpec &spec, d3d12::D3D12Resource resource)
    : IGPUTexture(kBackend, spec)
    , m_device(device)
    , m_resource(std::move(resource)) {}

GPUObjectResult<IGPUTexture> D3D12Texture::Create(d3d12::D3D12Device &device, const TextureSpec &spec) {
    switch (spec.dimensions) {
    case TextureDimensions::Tex1D: break;
    case TextureDimensions::Tex2D: break;
    case TextureDimensions::Tex3D: break;
    default: return GPUOperationError{"Illegal texture dimensions value"};
    }

    d3d12::D3D12Resource resource{};
    auto builder = [&] {
        switch (spec.dimensions) {
        case TextureDimensions::Tex1D: return resource.Texture1DBuilder(spec.width, spec.depthOrArrayLength);
        case TextureDimensions::Tex2D:
            return resource.Texture2DBuilder(spec.width, spec.height, spec.depthOrArrayLength);
        case TextureDimensions::Tex3D:
            return resource.Texture3DBuilder(spec.width, spec.height, spec.depthOrArrayLength);
        }

        // This is legitimately unreachable; we've ensured spec.dimensions is one of the valid values above
        util::unreachable();
    }();

    const BitmaskEnum bmUsage{spec.usage};
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (bmUsage.AnyOf(TextureUsage::RenderTarget)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (bmUsage.AnyOf(TextureUsage::Storage)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    if (bmUsage.AnyOf(TextureUsage::DepthTarget)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }

    builder.MipLevels(spec.mipLevels);
    builder.Format(util::ToD3D12Value(spec.format));
    builder.Alignment(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    builder.Layout(D3D12_TEXTURE_LAYOUT_UNKNOWN);
    builder.SampleDesc(1, 0);
    builder.Flags(flags);
    builder.HeapType(D3D12_HEAP_TYPE_DEFAULT);
    builder.HeapFlags(D3D12_HEAP_FLAG_NONE);
    builder.InitialState(D3D12_RESOURCE_STATE_COMMON);
    HRESULT hr = builder.BuildCommitted(device);
    if (FAILED(hr)) {
        return GPUOperationError{fmt::format("Failed to create D3D12 texture: error code {:X}", hr)};
    }
    return {std::make_unique<D3D12Texture>(device, spec, std::move(resource))};
}

void D3D12Texture::SetName(std::string_view name) {
    m_resource->SetName(util::StringToWString(name).c_str());
}

} // namespace ymir::gpu
