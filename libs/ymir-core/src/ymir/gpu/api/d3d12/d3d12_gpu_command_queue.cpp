#include <ymir/gpu/api/d3d12/d3d12_gpu_command_queue.hpp>

#include <ymir/gpu/api/d3d12/d3d12_gpu_command_list.hpp>
#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

#include <fmt/format.h>

namespace ymir::gpu {

D3D12CommandQueue::D3D12CommandQueue(d3d12::D3D12Device &device, d3d12::D3D12CommandQueue &&queue,
                                     d3d12::D3D12Fence &&fence, CommandQueueType type)
    : IGPUCommandQueue(kBackend, type)
    , m_device(device)
    , m_queue(std::move(queue))
    , m_fence(std::move(fence))
    , m_d3d12Type(util::ToD3D12Value(type)) {}

void D3D12CommandQueue::SetName(std::string_view name) {
    m_queue->SetName(util::StringToWString(name).c_str());
    m_fence->SetName(util::StringToWString(fmt::format("Fence for {}", name)).c_str());
}

GPUObjectResult<IGPUCommandQueue> D3D12CommandQueue::Create(d3d12::D3D12Device &device, CommandQueueType type,
                                                            const util::PropertyBag *props) {
    d3d12::D3D12CommandQueue commandQueue{};

    const D3D12_COMMAND_LIST_TYPE dx12type = util::ToD3D12Value(type);
    const auto nodeMask = util::PropertyBag::NullSafeGetOrDefault<props::D3D12NodeMaskKey>(props);
    const auto flags = util::PropertyBag::NullSafeGetOrDefault<props::D3D12CommandQueueFlagsKey>(props);

    HRESULT hr = commandQueue.Create(device, dx12type, nodeMask, flags);
    if (FAILED(hr)) {
        // TODO: get human-readable error message from HRESULT
        // TODO: get debug messages if available
        return GPUOperationError{fmt::format("Failed to create command queue: error code {:X}", hr)};
    }

    d3d12::D3D12Fence fence{};
    hr = fence.Create(device, 0, D3D12_FENCE_FLAG_NONE);
    if (FAILED(hr)) {
        // TODO: get human-readable error message from HRESULT
        // TODO: get debug messages if available
        return GPUOperationError{fmt::format("Failed to create command queue fence: error code {:X}", hr)};
    }
    return {std::make_unique<D3D12CommandQueue>(device, std::move(commandQueue), std::move(fence), type)};
}

GPUObjectResult<IGPUCommandList> D3D12CommandQueue::CreateCommandList(const util::PropertyBag *props) {
    d3d12::D3D12CommandAllocator allocator{};
    HRESULT hr = allocator.Create(m_device, m_d3d12Type);
    if (FAILED(hr)) {
        return GPUOperationError{fmt::format("Failed to create command allocator: error code {:X}", hr)};
    }

    // TODO: pipeline state?
    const auto nodeMask = util::PropertyBag::NullSafeGetOrDefault<props::D3D12NodeMaskKey>(props);

    d3d12::D3D12GraphicsCommandList commandList{};
    hr = commandList.Create(m_device, allocator, m_d3d12Type, nullptr, nodeMask);
    if (FAILED(hr)) {
        return GPUOperationError{fmt::format("Failed to create command list: error code {:X}", hr)};
    }

    return {std::make_unique<D3D12CommandList>(m_device, *this, std::move(allocator), std::move(commandList))};
}

void D3D12CommandQueue::CommitCommandList(const IGPUCommandList &list) {
    if (auto *dx12List = list.As<D3D12CommandList>()) {
        ID3D12CommandList *lists[] = {dx12List->GetCommandList().GetPointer()};
        m_queue->ExecuteCommandLists(1, lists);
        // TODO: return success
    }
    // TODO: return error: "Provided command list is not a Direct3D 12 object"
}

void D3D12CommandQueue::Wait() {
    const HRESULT hr = m_fence.Signal(m_queue);
    if (FAILED(hr)) {
        return; // TODO: return error
    }
    m_fence.Wait(INFINITE);
}

} // namespace ymir::gpu
