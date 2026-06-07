#include <ymir/gpu/api/d3d12/d3d12_descriptor_manager.hpp>

#include <ymir/gpu/api/d3d12/d3d12_gpu_buffer.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_texture.hpp>

#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

#include <ymir/util/dev_assert.hpp>

#include <cassert>

namespace ymir::gpu {

D3D12DescriptorManager::D3D12DescriptorManager(d3d12::D3D12Device &device, d3d12::D3D12DescriptorHeap &&resourceHeap,
                                               d3d12::D3D12DescriptorHeap &&samplerHeap,
                                               d3d12::D3D12DescriptorHeap &&rtvHeap,
                                               d3d12::D3D12DescriptorHeap &&dsvHeap)
    : m_device(device)
    , m_resourceHeap(std::move(resourceHeap))
    , m_samplerHeap(std::move(samplerHeap))
    , m_rtvHeap(std::move(rtvHeap))
    , m_dsvHeap(std::move(dsvHeap)) {

    m_resourceHeapAlloc.Bind(m_resourceHeap);
    m_samplerHeapAlloc.Bind(m_samplerHeap);
    m_rtvHeapAlloc.Bind(m_rtvHeap);
    m_dsvHeapAlloc.Bind(m_dsvHeap);
}

GPUObjectResult<D3D12DescriptorManager> D3D12DescriptorManager::Create(d3d12::D3D12Device &device, uint32 maxResources,
                                                                       uint32 maxSamplers, uint32 maxRTVs,
                                                                       uint32 maxDSVs) {
    d3d12::D3D12DescriptorHeap resourceHeap{};
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = maxResources;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        const HRESULT hr = resourceHeap.Create(device, desc);
        if (FAILED(hr)) {
            return GPUOperationError{fmt::format("Coould not create CBV/SRV/UAV descriptor heap: error code {:X}", hr)};
        }
    }

    d3d12::D3D12DescriptorHeap samplerHeap{};
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        desc.NumDescriptors = maxSamplers;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        const HRESULT hr = samplerHeap.Create(device, desc);
        if (FAILED(hr)) {
            return GPUOperationError{fmt::format("Coould not create sampler descriptor heap: error code {:X}", hr)};
        }
    }

    d3d12::D3D12DescriptorHeap rtvHeap{};
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = maxRTVs;
        const HRESULT hr = rtvHeap.Create(device, desc);
        if (FAILED(hr)) {
            return GPUOperationError{fmt::format("Coould not create RTV descriptor heap: error code {:X}", hr)};
        }
    }

    d3d12::D3D12DescriptorHeap dsvHeap{};
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        desc.NumDescriptors = maxDSVs;
        const HRESULT hr = dsvHeap.Create(device, desc);
        if (FAILED(hr)) {
            return GPUOperationError{fmt::format("Coould not create DSV descriptor heap: error code {:X}", hr)};
        }
    }

    return {std::make_unique<D3D12DescriptorManager>(device, std::move(resourceHeap), std::move(samplerHeap),
                                                     std::move(rtvHeap), std::move(dsvHeap))};
}

void D3D12DescriptorManager::SetResourceHeapName(std::string_view name) {
    m_resourceHeap->SetName(util::StringToWString(name).c_str());
}

void D3D12DescriptorManager::SetSamplerHeapName(std::string_view name) {
    m_samplerHeap->SetName(util::StringToWString(name).c_str());
}

void D3D12DescriptorManager::SetRTVHeapName(std::string_view name) {
    m_rtvHeap->SetName(util::StringToWString(name).c_str());
}

void D3D12DescriptorManager::SetDSVHeapName(std::string_view name) {
    m_dsvHeap->SetName(util::StringToWString(name).c_str());
}

GPUValueResult<D3D12DescriptorAllocation> D3D12DescriptorManager::AllocateRTV(const TextureViewSpec &viewSpec) {
    if (viewSpec.texture == nullptr) {
        return GPUOperationError{"View does not have a valid texture pointer"};
    }

    const TextureSpec &texSpec = viewSpec.texture->GetSpec();

    auto *dx12Texture = viewSpec.texture->As<D3D12Texture>();
    if (dx12Texture == nullptr) {
        return GPUOperationError{"Texture referenced by view is not a D3D12 texture"};
    }
    ID3D12Resource2 *resource = dx12Texture->GetResource().GetPointer();
    assert(resource != nullptr);

    const UINT mipSlice = viewSpec.mipLevelBase;
    const UINT baseArraySize = texSpec.depthOrArrayLength - viewSpec.arrayIndex;
    const UINT arraySize = viewSpec.arraySize == 0 ? baseArraySize : std::min<UINT>(viewSpec.arraySize, baseArraySize);

    TextureFormat viewFormat = viewSpec.format;
    if (viewFormat == TextureFormat::Undefined) {
        viewFormat = texSpec.format;
    }

    D3D12_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = util::ToD3D12Value(viewFormat);
    switch (texSpec.dimensions) {
    case TextureDimensions::Tex1D:
        if (texSpec.depthOrArrayLength <= 1) {
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
            desc.Texture1D.MipSlice = mipSlice;
        } else {
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
            desc.Texture1DArray.MipSlice = mipSlice;
            desc.Texture1DArray.FirstArraySlice = viewSpec.arrayIndex;
            desc.Texture1DArray.ArraySize = arraySize;
        }
        break;

    case TextureDimensions::Tex2D:
        if (viewSpec.viewMode2D == Texture2DViewMode::Normal) {
            if (texSpec.depthOrArrayLength <= 1) {
                desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = mipSlice;
                desc.Texture2D.PlaneSlice = viewSpec.planeSlice;
            } else {
                desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MipSlice = mipSlice;
                desc.Texture2DArray.FirstArraySlice = viewSpec.arrayIndex;
                desc.Texture2DArray.ArraySize = arraySize;
                desc.Texture2DArray.PlaneSlice = viewSpec.planeSlice;
            }
        } else if (viewSpec.viewMode2D == Texture2DViewMode::Multisampled) {
            if (texSpec.depthOrArrayLength <= 1) {
                desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            } else {
                desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
                desc.Texture2DMSArray.FirstArraySlice = viewSpec.arrayIndex;
                desc.Texture2DMSArray.ArraySize = arraySize;
            }
        } else if (viewSpec.viewMode2D == Texture2DViewMode::Cube) {
            return GPUOperationError{"Cannot create RTV on cube textures"};
        }
        break;

    case TextureDimensions::Tex3D:
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = mipSlice;
        desc.Texture3D.FirstWSlice = viewSpec.arrayIndex;
        desc.Texture3D.WSize = viewSpec.arraySize == 0 ? -1 : viewSpec.arraySize;
        break;

    default: return GPUOperationError{"Illegal texture dimension in referenced texture"};
    }

    D3D12DescriptorAllocation alloc{D3D12DescriptorType::RTV};
    if (!m_rtvHeapAlloc.Allocate(alloc.cpuHandle, alloc.gpuHandle)) {
        return GPUOperationError{"No free space available for texture RTV"};
    }

    m_device->CreateRenderTargetView(resource, &desc, alloc.cpuHandle);

    return alloc;
}

GPUValueResult<D3D12DescriptorAllocation> D3D12DescriptorManager::AllocateDSV(const TextureViewSpec &viewSpec) {
    if (viewSpec.texture == nullptr) {
        return GPUOperationError{"View does not have a valid texture pointer"};
    }

    const TextureSpec &texSpec = viewSpec.texture->GetSpec();

    auto *dx12Texture = viewSpec.texture->As<D3D12Texture>();
    if (dx12Texture == nullptr) {
        return GPUOperationError{"Texture referenced by view is not a D3D12 texture"};
    }
    ID3D12Resource2 *resource = dx12Texture->GetResource().GetPointer();
    assert(resource != nullptr);

    const UINT mipSlice = viewSpec.mipLevelBase;
    const UINT baseArraySize = texSpec.depthOrArrayLength - viewSpec.arrayIndex;
    const UINT arraySize = viewSpec.arraySize == 0 ? baseArraySize : std::min<UINT>(viewSpec.arraySize, baseArraySize);

    TextureFormat viewFormat = viewSpec.format;
    if (viewFormat == TextureFormat::Undefined) {
        viewFormat = texSpec.format;
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
    desc.Format = util::ToD3D12Value(viewFormat);
    switch (texSpec.dimensions) {
    case TextureDimensions::Tex1D:
        if (texSpec.depthOrArrayLength <= 1) {
            desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
            desc.Texture1D.MipSlice = mipSlice;
        } else {
            desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
            desc.Texture1DArray.MipSlice = mipSlice;
            desc.Texture1DArray.FirstArraySlice = viewSpec.arrayIndex;
            desc.Texture1DArray.ArraySize = arraySize;
        }
        break;

    case TextureDimensions::Tex2D:
        if (viewSpec.viewMode2D == Texture2DViewMode::Normal) {
            if (texSpec.depthOrArrayLength <= 1) {
                desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = mipSlice;
            } else {
                desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MipSlice = mipSlice;
                desc.Texture2DArray.FirstArraySlice = viewSpec.arrayIndex;
                desc.Texture2DArray.ArraySize = arraySize;
            }
        } else if (viewSpec.viewMode2D == Texture2DViewMode::Multisampled) {
            if (texSpec.depthOrArrayLength <= 1) {
                desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
            } else {
                desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
                desc.Texture2DMSArray.FirstArraySlice = viewSpec.arrayIndex;
                desc.Texture2DMSArray.ArraySize = arraySize;
            }
        } else if (viewSpec.viewMode2D == Texture2DViewMode::Cube) {
            return GPUOperationError{"Cannot create DSV on cube textures"};
        }
        break;

    case TextureDimensions::Tex3D: return GPUOperationError{"Cannot create DSV on 3D textures"};

    default: return GPUOperationError{"Illegal texture dimension in referenced texture"};
    }

    D3D12DescriptorAllocation alloc{D3D12DescriptorType::DSV};
    if (!m_dsvHeapAlloc.Allocate(alloc.cpuHandle, alloc.gpuHandle)) {
        return GPUOperationError{"No free space available for texture DSV"};
    }

    m_device->CreateDepthStencilView(resource, &desc, alloc.cpuHandle);

    return alloc;
}

GPUValueResult<D3D12DescriptorAllocation> D3D12DescriptorManager::AllocateSRV(const TextureViewSpec &viewSpec) {
    if (viewSpec.texture == nullptr) {
        return GPUOperationError{"View does not have a valid texture pointer"};
    }

    const TextureSpec &texSpec = viewSpec.texture->GetSpec();

    auto *dx12Texture = viewSpec.texture->As<D3D12Texture>();
    if (dx12Texture == nullptr) {
        return GPUOperationError{"Texture referenced by view is not a D3D12 texture"};
    }
    ID3D12Resource2 *resource = dx12Texture->GetResource().GetPointer();
    assert(resource != nullptr);

    const UINT mostDetailedMip = viewSpec.mipLevelBase;
    const UINT mipLevels = viewSpec.mipLevelCount == 0 ? -1 : viewSpec.mipLevelCount;
    const UINT baseArraySize = texSpec.depthOrArrayLength - viewSpec.arrayIndex;
    const UINT arraySize = viewSpec.arraySize == 0 ? baseArraySize : std::min<UINT>(viewSpec.arraySize, baseArraySize);

    TextureFormat viewFormat = viewSpec.format;
    if (viewFormat == TextureFormat::Undefined) {
        viewFormat = texSpec.format;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = util::ToD3D12Value(viewFormat);
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    switch (texSpec.dimensions) {
    case TextureDimensions::Tex1D:
        if (texSpec.depthOrArrayLength <= 1) {
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            desc.Texture1D.MostDetailedMip = mostDetailedMip;
            desc.Texture1D.MipLevels = mipLevels;
        } else {
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            desc.Texture1DArray.MostDetailedMip = mostDetailedMip;
            desc.Texture1DArray.MipLevels = mipLevels;
            desc.Texture1DArray.FirstArraySlice = viewSpec.arrayIndex;
            desc.Texture1DArray.ArraySize = arraySize;
        }
        break;

    case TextureDimensions::Tex2D:
        if (viewSpec.viewMode2D == Texture2DViewMode::Normal) {
            if (texSpec.depthOrArrayLength <= 1) {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MostDetailedMip = mostDetailedMip;
                desc.Texture2D.MipLevels = mipLevels;
                desc.Texture2D.PlaneSlice = viewSpec.planeSlice;
            } else {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MostDetailedMip = mostDetailedMip;
                desc.Texture2DArray.MipLevels = mipLevels;
                desc.Texture2DArray.FirstArraySlice = viewSpec.arrayIndex;
                desc.Texture2DArray.ArraySize = arraySize;
                desc.Texture2DArray.PlaneSlice = viewSpec.planeSlice;
            }
        } else if (viewSpec.viewMode2D == Texture2DViewMode::Multisampled) {
            if (texSpec.depthOrArrayLength <= 1) {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            } else {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
                desc.Texture2DMSArray.FirstArraySlice = viewSpec.arrayIndex;
                desc.Texture2DMSArray.ArraySize = arraySize;
            }
        } else if (viewSpec.viewMode2D == Texture2DViewMode::Cube) {
            if (texSpec.depthOrArrayLength <= 6) {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                desc.TextureCube.MostDetailedMip = mostDetailedMip;
                desc.TextureCube.MipLevels = mipLevels;
            } else {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                desc.TextureCubeArray.MostDetailedMip = mostDetailedMip;
                desc.TextureCubeArray.MipLevels = mipLevels;
                desc.TextureCubeArray.First2DArrayFace = viewSpec.arrayIndex;
                desc.TextureCubeArray.NumCubes =
                    viewSpec.arraySize == 0 ? baseArraySize / 6 : std::min<UINT>(viewSpec.arraySize, baseArraySize / 6);
            }
        }
        break;

    case TextureDimensions::Tex3D:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MostDetailedMip = mostDetailedMip;
        desc.Texture3D.MipLevels = mipLevels;
        break;

    default: return GPUOperationError{"Illegal texture dimension in referenced texture"};
    }

    D3D12DescriptorAllocation alloc{D3D12DescriptorType::Resource};
    if (!m_resourceHeapAlloc.Allocate(alloc.cpuHandle, alloc.gpuHandle)) {
        return GPUOperationError{"No free space available for texture SRV"};
    }

    m_device->CreateShaderResourceView(resource, &desc, alloc.cpuHandle);

    return alloc;
}

GPUValueResult<D3D12DescriptorAllocation> D3D12DescriptorManager::AllocateUAV(const TextureViewSpec &viewSpec) {
    if (viewSpec.texture == nullptr) {
        return GPUOperationError{"View does not have a valid texture pointer"};
    }

    const TextureSpec &texSpec = viewSpec.texture->GetSpec();

    auto *dx12Texture = viewSpec.texture->As<D3D12Texture>();
    if (dx12Texture == nullptr) {
        return GPUOperationError{"Texture referenced by view is not a D3D12 texture"};
    }
    ID3D12Resource2 *resource = dx12Texture->GetResource().GetPointer();
    assert(resource != nullptr);

    const UINT mipSlice = viewSpec.mipLevelBase;
    const UINT baseArraySize = texSpec.depthOrArrayLength - viewSpec.arrayIndex;
    const UINT arraySize = viewSpec.arraySize == 0 ? baseArraySize : std::min<UINT>(viewSpec.arraySize, baseArraySize);

    TextureFormat viewFormat = viewSpec.format;
    if (viewFormat == TextureFormat::Undefined) {
        viewFormat = texSpec.format;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = util::ToD3D12Value(viewFormat);
    switch (texSpec.dimensions) {
    case TextureDimensions::Tex1D:
        if (texSpec.depthOrArrayLength <= 1) {
            desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            desc.Texture1D.MipSlice = mipSlice;
        } else {
            desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            desc.Texture1DArray.MipSlice = mipSlice;
            desc.Texture1DArray.FirstArraySlice = viewSpec.arrayIndex;
            desc.Texture1DArray.ArraySize = arraySize;
        }
        break;

    case TextureDimensions::Tex2D:
        if (viewSpec.viewMode2D == Texture2DViewMode::Normal) {
            if (texSpec.depthOrArrayLength <= 1) {
                desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = mipSlice;
                desc.Texture2D.PlaneSlice = viewSpec.planeSlice;
            } else {
                desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MipSlice = mipSlice;
                desc.Texture2DArray.FirstArraySlice = viewSpec.arrayIndex;
                desc.Texture2DArray.ArraySize = arraySize;
                desc.Texture2DArray.PlaneSlice = viewSpec.planeSlice;
            }
        } else if (viewSpec.viewMode2D == Texture2DViewMode::Multisampled) {
            if (texSpec.depthOrArrayLength <= 1) {
                desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DMS;
            } else {
                desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY;
                desc.Texture2DMSArray.FirstArraySlice = viewSpec.arrayIndex;
                desc.Texture2DMSArray.ArraySize = arraySize;
            }
        } else if (viewSpec.viewMode2D == Texture2DViewMode::Cube) {
            return GPUOperationError{"Cannot create UAV on cube textures"};
        }
        break;

    case TextureDimensions::Tex3D:
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = mipSlice;
        desc.Texture3D.FirstWSlice = viewSpec.arrayIndex;
        desc.Texture3D.WSize = viewSpec.arraySize == 0 ? -1 : viewSpec.arraySize;
        break;

    default: return GPUOperationError{"Illegal texture dimension in referenced texture"};
    }

    D3D12DescriptorAllocation alloc{D3D12DescriptorType::Resource};
    if (!m_resourceHeapAlloc.Allocate(alloc.cpuHandle, alloc.gpuHandle)) {
        return GPUOperationError{"No free space available for texture UAV"};
    }

    // TODO: support counter resources
    m_device->CreateUnorderedAccessView(resource, nullptr, &desc, alloc.cpuHandle);

    return alloc;
}

GPUValueResult<D3D12DescriptorAllocation> D3D12DescriptorManager::AllocateCBV(const BufferViewSpec &viewSpec) {
    if (viewSpec.buffer == nullptr) {
        return GPUOperationError{"View does not have a valid buffer pointer"};
    }

    const BufferSpec &bufSpec = viewSpec.buffer->GetSpec();

    auto *dx12Buffer = viewSpec.buffer->As<D3D12Buffer>();
    if (dx12Buffer == nullptr) {
        return GPUOperationError{"Buffer referenced by view is not a D3D12 buffer"};
    }
    ID3D12Resource2 *resource = dx12Buffer->GetResource().GetPointer();
    assert(resource != nullptr);

    D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
    desc.BufferLocation = viewSpec.buffer->GetAddress();
    desc.SizeInBytes = bufSpec.size * bufSpec.count;

    D3D12DescriptorAllocation alloc{D3D12DescriptorType::Resource};
    if (!m_resourceHeapAlloc.Allocate(alloc.cpuHandle, alloc.gpuHandle)) {
        return GPUOperationError{"No free space available for buffer CBV"};
    }

    // TODO: support counter resources
    m_device->CreateConstantBufferView(&desc, alloc.cpuHandle);

    return alloc;
}

GPUValueResult<D3D12DescriptorAllocation> D3D12DescriptorManager::AllocateSRV(const BufferViewSpec &viewSpec) {
    if (viewSpec.buffer == nullptr) {
        return GPUOperationError{"View does not have a valid buffer pointer"};
    }

    const BufferSpec &bufSpec = viewSpec.buffer->GetSpec();

    auto *dx12Buffer = viewSpec.buffer->As<D3D12Buffer>();
    if (dx12Buffer == nullptr) {
        return GPUOperationError{"Buffer referenced by view is not a D3D12 buffer"};
    }
    ID3D12Resource2 *resource = dx12Buffer->GetResource().GetPointer();
    assert(resource != nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.FirstElement = 0u;
    desc.Buffer.NumElements = bufSpec.count;
    desc.Buffer.StructureByteStride = bufSpec.size;
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    D3D12DescriptorAllocation alloc{D3D12DescriptorType::Resource};
    if (!m_resourceHeapAlloc.Allocate(alloc.cpuHandle, alloc.gpuHandle)) {
        return GPUOperationError{"No free space available for buffer SRV"};
    }

    // TODO: support counter resources
    m_device->CreateShaderResourceView(resource, &desc, alloc.cpuHandle);

    return alloc;
}

GPUValueResult<D3D12DescriptorAllocation> D3D12DescriptorManager::AllocateUAV(const BufferViewSpec &viewSpec) {
    if (viewSpec.buffer == nullptr) {
        return GPUOperationError{"View does not have a valid buffer pointer"};
    }

    const BufferSpec &bufSpec = viewSpec.buffer->GetSpec();

    auto *dx12Buffer = viewSpec.buffer->As<D3D12Buffer>();
    if (dx12Buffer == nullptr) {
        return GPUOperationError{"Buffer referenced by view is not a D3D12 buffer"};
    }
    ID3D12Resource2 *resource = dx12Buffer->GetResource().GetPointer();
    assert(resource != nullptr);

    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0u;
    desc.Buffer.NumElements = bufSpec.count;
    desc.Buffer.StructureByteStride = bufSpec.size;
    desc.Buffer.CounterOffsetInBytes = 0u;
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    D3D12DescriptorAllocation alloc{D3D12DescriptorType::Resource};
    if (!m_resourceHeapAlloc.Allocate(alloc.cpuHandle, alloc.gpuHandle)) {
        return GPUOperationError{"No free space available for buffer UAV"};
    }

    // TODO: support counter resources
    m_device->CreateUnorderedAccessView(resource, nullptr, &desc, alloc.cpuHandle);

    return alloc;
}

void D3D12DescriptorManager::Free(D3D12DescriptorAllocation alloc) {
    switch (alloc.type) {
    case D3D12DescriptorType::Resource: m_resourceHeapAlloc.Free(alloc.cpuHandle, alloc.gpuHandle); break;
    case D3D12DescriptorType::Sampler: m_samplerHeapAlloc.Free(alloc.cpuHandle, alloc.gpuHandle); break;
    case D3D12DescriptorType::RTV: m_rtvHeapAlloc.Free(alloc.cpuHandle, alloc.gpuHandle); break;
    case D3D12DescriptorType::DSV: m_dsvHeapAlloc.Free(alloc.cpuHandle, alloc.gpuHandle); break;
    default: YMIR_DEV_CHECK(); break;
    }
}

} // namespace ymir::gpu
