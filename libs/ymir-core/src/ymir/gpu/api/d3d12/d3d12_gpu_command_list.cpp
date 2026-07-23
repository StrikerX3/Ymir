#include <ymir/gpu/api/d3d12/d3d12_gpu_command_list.hpp>

#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

namespace ymir::gpu {

D3D12CommandList::D3D12CommandList(d3d12::D3D12Device &parentDevice, D3D12CommandQueue &parentQueue,
                                   d3d12::D3D12CommandAllocator &&allocator, d3d12::D3D12GraphicsCommandList &&list)
    : IGPUCommandList(kBackend)
    , m_parentDevice(parentDevice)
    , m_parentQueue(parentQueue)
    , m_allocator(std::move(allocator))
    , m_list(std::move(list)) {

    m_list->Close();
}

void D3D12CommandList::SetName(std::string_view name) {
    m_list->SetName(util::StringToWString(name).c_str());
}

void D3D12CommandList::Reset() {
    // TODO: initial state?
    // TODO: handle errors
    HRESULT hr = m_allocator->Reset();
    hr = m_list->Reset(m_allocator.GetPointer(), nullptr);
}

void D3D12CommandList::Begin() {
    // TODO: heaps
    // m_list->SetDescriptorHeaps(2, heaps);
}

void D3D12CommandList::End() {
    // TODO: handle errors
    HRESULT hr = m_list->Close();
}

GPUResult D3D12CommandList::SetComputePipeline(const IGPUComputePipeline &pipeline) {
    // TODO: implement
    return GPUOperationError{"Unimplemented"};
}

GPUResult D3D12CommandList::SetBindings(uint32 index, const IGPUBindingSet &bindings) {
    // TODO: implement
    return GPUOperationError{"Unimplemented"};
}

GPUResult D3D12CommandList::SetConstants(uint32 index, const void *data, size_t size) {
    // TODO: implement
    return GPUOperationError{"Unimplemented"};
}

} // namespace ymir::gpu
