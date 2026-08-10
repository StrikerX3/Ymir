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
        const HRESULT hr = device->CreateCommandList(nodeMask, type, allocator.GetPointer(), pipelineState,
                                                     IID_PPV_ARGS(m_object.put()));
        if (SUCCEEDED(hr)) {
            QueryUpgrades();
        }
        return hr;
    }

    /// @brief Retrieves the address to the pointer to the command list cast to `ID3D12CommandList`.
    /// @return a pointer to the base command list type
    ID3D12CommandList *const *GetAddressOfBase() const {
        return (ID3D12CommandList *const *)GetAddressOf();
    }

    // clang-format off
    ID3D12GraphicsCommandList1  *As1 () const { return m_list1 .get(); }
    ID3D12GraphicsCommandList2  *As2 () const { return m_list2 .get(); }
    ID3D12GraphicsCommandList3  *As3 () const { return m_list3 .get(); }
    ID3D12GraphicsCommandList4  *As4 () const { return m_list4 .get(); }
    ID3D12GraphicsCommandList5  *As5 () const { return m_list5 .get(); }
    ID3D12GraphicsCommandList6  *As6 () const { return m_list6 .get(); }
    ID3D12GraphicsCommandList7  *As7 () const { return m_list7 .get(); }
    ID3D12GraphicsCommandList8  *As8 () const { return m_list8 .get(); }
    ID3D12GraphicsCommandList9  *As9 () const { return m_list9 .get(); }
    ID3D12GraphicsCommandList10 *As10() const { return m_list10.get(); }
    // clang-format on

private:
    void QueryUpgrades() {
        // clang-format off
        m_list1  = m_object.try_query<ID3D12GraphicsCommandList1 >(); if (!m_list1) { return; }
        m_list2  = m_object.try_query<ID3D12GraphicsCommandList2 >(); if (!m_list2) { return; }
        m_list3  = m_object.try_query<ID3D12GraphicsCommandList3 >(); if (!m_list3) { return; }
        m_list4  = m_object.try_query<ID3D12GraphicsCommandList4 >(); if (!m_list4) { return; }
        m_list5  = m_object.try_query<ID3D12GraphicsCommandList5 >(); if (!m_list5) { return; }
        m_list6  = m_object.try_query<ID3D12GraphicsCommandList6 >(); if (!m_list6) { return; }
        m_list7  = m_object.try_query<ID3D12GraphicsCommandList7 >(); if (!m_list7) { return; }
        m_list8  = m_object.try_query<ID3D12GraphicsCommandList8 >(); if (!m_list8) { return; }
        m_list9  = m_object.try_query<ID3D12GraphicsCommandList9 >(); if (!m_list9) { return; }
        m_list10 = m_object.try_query<ID3D12GraphicsCommandList10>();
        // clang-format on
    }

    void DestroyExt() override {
        m_list10.reset();
        m_list9.reset();
        m_list8.reset();
        m_list7.reset();
        m_list6.reset();
        m_list5.reset();
        m_list4.reset();
        m_list3.reset();
        m_list2.reset();
        m_list1.reset();
    }

    wil::com_ptr_nothrow<ID3D12GraphicsCommandList1> m_list1;
    wil::com_ptr_nothrow<ID3D12GraphicsCommandList2> m_list2;
    wil::com_ptr_nothrow<ID3D12GraphicsCommandList3> m_list3;
    wil::com_ptr_nothrow<ID3D12GraphicsCommandList4> m_list4;
    wil::com_ptr_nothrow<ID3D12GraphicsCommandList5> m_list5;
    wil::com_ptr_nothrow<ID3D12GraphicsCommandList6> m_list6;
    wil::com_ptr_nothrow<ID3D12GraphicsCommandList7> m_list7;
    wil::com_ptr_nothrow<ID3D12GraphicsCommandList8> m_list8;
    wil::com_ptr_nothrow<ID3D12GraphicsCommandList9> m_list9;
    wil::com_ptr_nothrow<ID3D12GraphicsCommandList10> m_list10;
};

} // namespace ymir::gpu::d3d12
