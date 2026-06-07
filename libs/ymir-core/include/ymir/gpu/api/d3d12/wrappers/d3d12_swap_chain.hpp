#pragma once

/**
@file
@brief Defines `D3D12SwapChain`, a wrapper for `IDXGISwapChain3` objects.
*/

#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>
#include <dxgi1_6.h>

namespace ymir::gpu::d3d12 {

/// @brief Manages an `IDXGISwapChain3` and a waitable object for frame synchronization.
class D3D12SwapChain final : public D3D12ObjectWrapper<IDXGISwapChain3> {
public:
    /// @brief Creates an `IDXGISwapChain3` with the specified parameters
    /// @param[in] dxgiFactoryFlags base flags to apply to the `IDXGIFactory6` instance
    /// @param[in] commandQueue the direct command queue to bind to the swap chain
    /// @param[in] scDesc the swap chain descriptor
    /// @param[in] hwnd the window handle to bind the swap chain to
    /// @param[in] maxLatench the maximum number of frames of presentation latency
    /// @return `true` if the swap chain was created successfully, `false` otherwise (TODO: better errors)
    bool Create(UINT dxgiFactoryFlags, ID3D12CommandQueue *commandQueue, DXGI_SWAP_CHAIN_DESC1 scDesc, HWND hwnd,
                UINT maxLatency) {
        wil::com_ptr_nothrow<IDXGIFactory6> factory;
        wil::com_ptr_nothrow<IDXGISwapChain1> swapChain1;

        if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(factory.put())))) {
            return false;
        }

        BOOL allowTearing = FALSE;
        factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
        m_tearingSupport = allowTearing == TRUE;
        if (m_tearingSupport) {
            scDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }

        if (FAILED(factory->CreateSwapChainForHwnd(commandQueue, hwnd, &scDesc, nullptr, nullptr, swapChain1.put()))) {
            return false;
        }

        if (FAILED(swapChain1.copy_to(m_object.put()))) {
            return false;
        }
        if (m_tearingSupport) {
            factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        }
        m_object->SetMaximumFrameLatency(maxLatency);
        m_waitableObject.reset(m_object->GetFrameLatencyWaitableObject());

        return true;
    }

    /// @brief Resizes the swap chain buffers to the specified dimensions.
    /// Callers must ensure the render targets are not referenced anywhere before calling this function.
    /// @param[in] width the new framebuffer width
    /// @param[in] height the new framebuffer height
    void ResizeBuffers(UINT width, UINT height) const {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        m_object->GetDesc1(&desc);
        HRESULT result = m_object->ResizeBuffers(0, width, height, desc.Format, desc.Flags);
        assert(SUCCEEDED(result) && "Failed to resize swapchain.");
    }

    /// @brief Presents the next frame.
    /// @param[in] syncInterval the frame synchronization interval
    /// @param[in,opt] flags frame presentation options
    /// @return
    HRESULT Present(UINT syncInterval, UINT flags) const {
        return m_object->Present(syncInterval, flags);
    }

    /// @brief Retrieves the waitable object for this swap chain.
    /// @return the waitable object
    HANDLE WaitableObject() const {
        return m_waitableObject.get();
    }

    /// @brief Returns a pointer to the `IDXGISwapChain3` instance for convenience.
    /// @return a pointer to the `IDXGISwapChain3` instance
    IDXGISwapChain3 *operator->() {
        return m_object.get();
    }

    /// @brief Returns a pointer to the `IDXGISwapChain3` instance for convenience.
    /// @return a pointer to the `IDXGISwapChain3` instance
    IDXGISwapChain3 *operator->() const {
        return m_object.get();
    }

    bool IsTearingSupported() const {
        return m_tearingSupport;
    }

private:
    wil::unique_handle m_waitableObject = nullptr;
    bool m_tearingSupport = false;

    void DestroyExt() {
        if (m_object) {
            m_object->SetFullscreenState(false, nullptr);
        }
        m_waitableObject.reset();
    }

    bool IsValidExt() const {
        return (bool)m_waitableObject;
    }
};

} // namespace ymir::gpu::d3d12
