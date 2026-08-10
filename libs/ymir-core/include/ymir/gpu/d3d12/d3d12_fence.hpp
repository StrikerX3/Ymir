#pragma once

/**
@file
@brief Defines `D3D12Fence`, a wrapper for `ID3D12Fence` objects.
*/

#include "d3d12_commands.hpp"
#include "d3d12_device.hpp"
#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>

namespace ymir::gpu::d3d12 {

/// @brief Manages an `ID3D12Fence` and includes a wait handle and the last signaled value for convenience.
class D3D12Fence final : public D3D12ObjectWrapper<ID3D12Fence> {
public:
    D3D12Fence() = default;
    D3D12Fence(wil::com_ptr_nothrow<ID3D12Fence> &&ptr)
        : D3D12ObjectWrapper(std::move(ptr)) {}
    D3D12Fence(ID3D12Fence *ptr)
        : D3D12ObjectWrapper(ptr) {}

    /// @brief Creates an `ID3D12Fence` object using the given device and parameters.
    /// @param[in] device the device instance that will own the fence
    /// @param[in] initialValue the initial fence value
    /// @param[in] flags fence flags
    /// @return the result of the attempt to create the fence
    HRESULT Create(const D3D12Device &device, UINT64 initialValue, D3D12_FENCE_FLAGS flags) {
        const HRESULT hr = device->CreateFence(initialValue, flags, IID_PPV_ARGS(m_object.put()));
        if (SUCCEEDED(hr)) {
            QueryUpgrades();
        }
        return hr;
    }

    /// @brief Enqueues a fence signal into the given command queue.
    /// Requires the fence to be created.
    /// @param[in] commandQueue the command queue to use
    /// @param[in] value the fence value
    /// @return the result of the attempt to signal the fence
    HRESULT Signal(const D3D12CommandQueue &commandQueue, UINT64 value) {
        return commandQueue->Signal(m_object.get(), value);
    }

    /// @brief Sets up a completion event on the fence using the given value.
    /// This is typically used with `WaitForMultipleObjects`.
    /// If you wish to wait for just this fence, consider using `Wait(DWORD)` or `Wait(DWORD, UINT64)`.
    /// @param[in] value the fence value
    /// @return a handle to the waitable event
    HANDLE SetupWait(UINT64 value) const {
        HANDLE hEvent = m_waitEvent.get();
        m_object->SetEventOnCompletion(value, hEvent);
        return hEvent;
    }

    /// @brief Waits for the fence to be signaled using the given value.
    /// @param[in] timeout maximum time to wait for the signal (in milliseconds)
    /// @param[in] value the fence value
    void Wait(DWORD timeout, UINT64 value) const {
        HANDLE hEvent = SetupWait(value);
        ::WaitForSingleObject(hEvent, timeout);
    }

    /// @brief Get the last completed value.
    /// @return the last completed value
    UINT64 GetCompletedValue() const {
        return m_object->GetCompletedValue();
    }

    // clang-format off
    ID3D12Fence1 *As1() const { return m_fence1.get(); }
    // clang-format on

private:
    wil::unique_handle m_waitEvent{CreateEvent(nullptr, FALSE, FALSE, nullptr)};

    void QueryUpgrades() {
        m_fence1 = m_object.try_query<ID3D12Fence1>();
    }

    void DestroyExt() override {
        m_waitEvent.reset();
        m_fence1.reset();
    }

    bool IsValidExt() const override {
        return (bool)m_waitEvent;
    }

    wil::com_ptr_nothrow<ID3D12Fence1> m_fence1;
};

} // namespace ymir::gpu::d3d12
