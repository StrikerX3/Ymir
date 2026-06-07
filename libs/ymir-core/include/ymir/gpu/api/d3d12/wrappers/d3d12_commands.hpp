#pragma once

/**
@file
@brief Defines Direct3D 12 command-related wrappers.

Includes:
- `D3D12CommandQueue`, wrapping an `ID3D12CommandQueue`
- `D3D12CommandAllocator`, wrapping an `ID3D12CommandAllocator`
- `D3D12GraphicsCommandList`, wrapping an `ID3D12GraphicsCommandList`
*/

#include "d3d12_device.hpp"
#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>

#include <ymir/util/bitmask_enum.hpp>

namespace ymir::gpu::d3d12 {

enum class D3D12CommandQueueFlags {
    None = 0,

    HighPriority = 1 << 0,
    DisableGPUTimeout = 1 << 1,

    Default = HighPriority,
};

}
ENABLE_BITMASK_OPERATORS(ymir::gpu::d3d12::D3D12CommandQueueFlags);

namespace ymir::gpu::d3d12 {

/// @brief Manages an `ID3D12CommandQueue`.
class D3D12CommandQueue final : public D3D12ObjectWrapper<ID3D12CommandQueue> {
public:
    /// @brief Creates a command queue of the specified type.
    /// @param[in] device the device that will own the command queue
    /// @param[in] type the command queue type
    /// @param[in] nodeMask which nodes to bind the queue to
    /// @param[in] flags command queue flags (bitwise ORed together)
    /// @return the result of the attempt to create the command queue
    HRESULT Create(const D3D12Device &device, D3D12_COMMAND_LIST_TYPE type, UINT nodeMask = 0,
                   D3D12CommandQueueFlags flags = D3D12CommandQueueFlags::Default) {
        const auto bmFlags = BitmaskEnum{flags};
        const bool highPriority = bmFlags.AnyOf(D3D12CommandQueueFlags::HighPriority);
        const bool disableGPUTimeout = bmFlags.AnyOf(D3D12CommandQueueFlags::DisableGPUTimeout);
        D3D12_COMMAND_QUEUE_DESC queueDesc = {
            .Type = type,
            .Priority = highPriority ? D3D12_COMMAND_QUEUE_PRIORITY_HIGH : D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
            .Flags = disableGPUTimeout ? D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT : D3D12_COMMAND_QUEUE_FLAG_NONE,
            .NodeMask = nodeMask,
        };
        return device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_object.put()));
    }
};

/// @brief Manages an `ID3D12CommandAllocator`.
class D3D12CommandAllocator final : public D3D12ObjectWrapper<ID3D12CommandAllocator> {
public:
    /// @brief Creates a command allocator of the specified type.
    /// @param[in] device the device instance that will own the command allocator
    /// @param[in] type the command allocator type
    /// @return the result of the attempt to create the command queue
    HRESULT Create(const D3D12Device &device, D3D12_COMMAND_LIST_TYPE type) {
        return device->CreateCommandAllocator(type, IID_PPV_ARGS(m_object.put()));
    }
};

/// @brief Manages an `ID3D12GraphicsCommandList`.
class D3D12GraphicsCommandList final : public D3D12ObjectWrapper<ID3D12GraphicsCommandList> {
public:
    /// @brief Creates a command list of the specified type using the given allocator.
    /// @param[in] device the device that will own the command list
    /// @param[in] allocator the command allocator
    /// @param[in] type the command list type
    /// @param[in,opt] pipelineState the initial pipeline state
    /// @param[in] nodeMask which nodes to bind the list to
    /// @return the result of the attempt to create the command list
    HRESULT Create(const D3D12Device &device, const D3D12CommandAllocator &allocator, D3D12_COMMAND_LIST_TYPE type,
                   ID3D12PipelineState *pipelineState = nullptr, UINT nodeMask = 0) {
        return device->CreateCommandList(nodeMask, type, allocator.GetPointer(), pipelineState,
                                         IID_PPV_ARGS(m_object.put()));
    }

    /// @brief Retrieves the address to the pointer to the command list cast to `ID3D12CommandList`.
    /// @return a pointer to the base command list type
    ID3D12CommandList *const *GetAddressOfBase() const {
        return (ID3D12CommandList *const *)GetAddressOf();
    }
};

} // namespace ymir::gpu::d3d12
