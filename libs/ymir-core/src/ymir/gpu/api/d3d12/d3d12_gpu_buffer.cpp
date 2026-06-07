#include <ymir/gpu/api/d3d12/d3d12_gpu_buffer.hpp>

#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

namespace ymir::gpu {

D3D12Buffer::D3D12Buffer(d3d12::D3D12Device &device, const BufferSpec &spec, d3d12::D3D12Resource resource)
    : IGPUBuffer(kBackend, spec)
    , m_device(device)
    , m_resource(std::move(resource)) {}

GPUObjectResult<IGPUBuffer> D3D12Buffer::Create(d3d12::D3D12Device &device, const BufferSpec &spec) {
    const BitmaskEnum bmUsage{spec.usage};
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (bmUsage.AnyOf(BufferUsage::ShaderWrite)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    const bool readback = bmUsage.AnyOf(BufferUsage::Download);
    const bool upload = bmUsage.AnyOf(BufferUsage::Upload | BufferUsage::Constant);
    if (readback && upload) {
        return GPUOperationError{"A buffer cannot be simultaneously used for GPU uploads and downloads"};
    }

    const D3D12_HEAP_TYPE heapType = upload     ? D3D12_HEAP_TYPE_UPLOAD
                                     : readback ? D3D12_HEAP_TYPE_READBACK
                                                : D3D12_HEAP_TYPE_DEFAULT;

    d3d12::D3D12Resource resource{};
    auto builder = resource.BufferBuilder(spec.size);
    builder.Flags(flags);
    builder.HeapType(heapType);
    HRESULT hr = builder.BuildCommitted(device);
    if (FAILED(hr)) {
        return GPUOperationError{fmt::format("Failed to create D3D12 buffer: error code {:X}", hr)};
    }
    return {std::make_unique<D3D12Buffer>(device, spec, std::move(resource))};
}

void D3D12Buffer::SetName(std::string_view name) {
    m_resource->SetName(util::StringToWString(name).c_str());
}

void *D3D12Buffer::Map(uint64 start, uint64 end) {
    const D3D12_RANGE range = MakeRange(start, end);
    void *data = nullptr;
    m_resource->Map(0, &range, &data);
    // TODO: check for errors
    return data;
}

void D3D12Buffer::Unmap(uint64 start, uint64 end) {
    if (BitmaskEnum{GetSpec().usage}.AnyOf(BufferUsage::Download)) {
        m_resource->Unmap(0, nullptr);
    } else {
        const D3D12_RANGE range = MakeRange(start, end);
        m_resource->Unmap(0, &range);
    }
}

uint64 D3D12Buffer::GetAddress() {
    return m_resource->GetGPUVirtualAddress();
}

D3D12_RANGE D3D12Buffer::MakeRange(uint64 start, uint64 end) {
    const BufferSpec &spec = GetSpec();
    if (start == 0 && end == 0) {
        return {.Begin = 0, .End = spec.size * spec.count};
    }
    return {.Begin = start, .End = end};
}

} // namespace ymir::gpu
