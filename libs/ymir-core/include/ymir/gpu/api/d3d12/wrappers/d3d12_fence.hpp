#pragma once

/**
@file
@brief Defines `D3D12Fence`, a wrapper for `ID3D12Fence1` objects.
*/

#include "d3d12_device.hpp"
#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>

namespace ymir::gpu::d3d12 {

/// @brief Manages an `ID3D12Fence1` and includes a wait handle and the last signaled value for convenience.
class D3D12Fence final : public D3D12ObjectWrapper<ID3D12Fence> {
public:
    /// @brief Creates an `ID3D12Fence` object using the given device and parameters.
    /// @param[in] device the device instance that will own the fence
    /// @param[in] initialValue the initial fence value
    /// @param[in] flags fence flags
    /// @return the result of the attempt to create the fence
    HRESULT Create(const D3D12Device &device, UINT64 initialValue, D3D12_FENCE_FLAGS flags) {
        m_lastSignaledValue = initialValue;
        return device->CreateFence(initialValue, flags, IID_PPV_ARGS(m_object.put()));
    }

    /// @brief Enqueues a fence signal into the given command queue.
    /// Requires the fence to be created.
    /// @param[in] commandQueue the command queue to use
    /// @return the result of the attempt to signal the fence
    HRESULT Signal(const D3D12CommandQueue &commandQueue) {
        return commandQueue->Signal(m_object.get(), ++m_lastSignaledValue);
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

    /// @brief Waits for the fence to be signaled using the last signaled value.
    /// @param[in] timeout maximum time to wait for the signal (in milliseconds)
    void Wait(DWORD timeout) const {
        Wait(timeout, m_lastSignaledValue);
    }

    /// @brief Waits for the fence to be signaled using the given value.
    /// @param[in] timeout maximum time to wait for the signal (in milliseconds)
    /// @param[in] value the fence value
    void Wait(DWORD timeout, UINT64 value) const {
        HANDLE hEvent = SetupWait(value);
        ::WaitForSingleObject(hEvent, timeout);
    }

    /// @brief Returns the last signaled value.
    /// This is incremented on calls to `Signal(const D3D12CommandQueue &)`.
    /// @return the last signaled value
    UINT64 GetLastSignaledValue() const {
        return m_lastSignaledValue;
    }

private:
    wil::unique_handle m_waitEvent{CreateEvent(nullptr, FALSE, FALSE, nullptr)};
    UINT64 m_lastSignaledValue = 0;

    void DestroyExt() override {
        m_waitEvent.reset();
    }

    bool IsValidExt() const override {
        return (bool)m_waitEvent;
    }
};

} // namespace ymir::gpu::d3d12
