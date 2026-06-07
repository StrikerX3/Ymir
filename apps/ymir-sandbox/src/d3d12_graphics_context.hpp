#pragma once

/**
@file
@brief Defines `GraphicsContext`, an object that manages a Direct3D 12 graphics context with a swap chain, render
targets for each frame, and a command list for submitting frames.
*/

#include <ymir/gpu/api/d3d12/d3d12_types.hpp>

#include <d3d12.h>

namespace ymir::gpu::d3d12 {

/// @brief Holds resources for a single render target in a swap chain.
/// Should not be used or created directly. Use `GraphicsContext` instead.
struct FrameContext {
    D3D12CommandAllocator commandAllocator = {};
    wil::com_ptr_nothrow<ID3D12Resource> renderTargetResource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE renderTargetDescriptor = {};
    UINT64 fenceValue;

    /// @brief Creates a command allocator for this frame.
    /// @param[in] device the device instance that will own the resources
    /// @return the result of the attempt to create the command allocator
    HRESULT CreateCommandAllocator(const D3D12Device &device) {
        return commandAllocator.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    }

    /// @brief Creates an `ID3D12Resource` for this frame. The `ID3D12CommandAllocator` must be created with
    /// `CreateCommandAllocator(const D3D12Device &)` prior to invoking this method.
    /// @param[in] device the device instance that will own the resources
    /// @param[in] swapChain the swap chain to target
    /// @param[in] bufferIndex the index of the buffer in the swap chain
    /// @return `true` if the resources were created successfully, `false` otherwise (TODO: better errors)
    bool CreateRenderTarget(const D3D12Device &device, IDXGISwapChain3 *swapChain, UINT bufferIndex) {
        ID3D12Resource *backBuffer = nullptr;
        if (FAILED(swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer)))) {
            return false;
        }
        device->CreateRenderTargetView(backBuffer, nullptr, renderTargetDescriptor);
        renderTargetResource.attach(backBuffer);
        return true;
    }

    /// @brief Destroys all resources associated with this object.
    void Destroy() {
        commandAllocator.Destroy();
        DestroyRenderTarget();
    }

    /// @brief Destroy the render target resource.
    void DestroyRenderTarget() {
        renderTargetResource.reset();
    }

    /// @brief Determines if the resources contained in this object are valid.
    /// @return `true` if all render target resources are valid, `false` otherwise
    bool IsValid() const {
        return commandAllocator.IsValid() && renderTargetResource;
    }
};

/// @brief Contains resources for graphics output with Direct3D 12.
/// @tparam kFrameCount number of frames in the swap chain
template <UINT kFrameCount>
struct GraphicsContext {
    D3D12Device *pDevice = nullptr;
    D3D12SwapChain swapChain = {};
    D3D12DescriptorHeap rtvHeap = {};
    FrameContext frameContext[kFrameCount] = {};
    D3D12Fence fence = {};
    D3D12CommandQueue commandQueue = {};
    D3D12GraphicsCommandList commandList = {};
    UINT frameIndex = 0;
    bool swapChainOccluded = false;

    /// @brief Creates a graphics context for the given window.
    /// @param[in] device the device instance that will own the resources
    /// @param[in] dxgiFactoryFlags initial DXGI factory flags
    /// @param[in] hwnd the window handle
    /// @param[in] width the framebuffer width
    /// @param[in] height the framebuffer height
    /// @return `true` if the resources were created successfully, `false` otherwise
    bool Create(D3D12Device &device, UINT dxgiFactoryFlags, HWND hwnd, UINT width, UINT height) {
        this->pDevice = &device;

        // Command queue
        if (FAILED(commandQueue.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT))) {
            return false;
        }

        // Fence
        if (FAILED(fence.Create(device, 0, D3D12_FENCE_FLAG_NONE))) {
            return false;
        }
        if (!fence.IsValid()) {
            return false;
        }

        // Swap chain
        {
            DXGI_SWAP_CHAIN_DESC1 scDesc = {};
            scDesc.BufferCount = kFrameCount;
            scDesc.Width = width;
            scDesc.Height = height;
            scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scDesc.SampleDesc.Count = 1;
            scDesc.SampleDesc.Quality = 0;
            scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
            scDesc.Scaling = DXGI_SCALING_NONE;
            scDesc.Stereo = FALSE;
            if (FAILED(swapChain.Create(dxgiFactoryFlags, commandQueue.GetPointer(), scDesc, hwnd, kFrameCount))) {
                return false;
            }
        }

        // RTV descriptor heap
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = kFrameCount;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 1;
            if (FAILED(rtvHeap.Create(device, desc))) {
                return false;
            }

            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap.GetCPUStart();
            for (auto &ctx : frameContext) {
                ctx.renderTargetDescriptor = rtvHandle;
                rtvHandle.ptr += rtvHeap.GetDescriptorSize();

                if (FAILED(ctx.CreateCommandAllocator(device))) {
                    return false;
                }
            }
        }

        // Command list
        if (FAILED(commandList.Create(device, frameContext[0].commandAllocator, D3D12_COMMAND_LIST_TYPE_DIRECT))) {
            return false;
        }
        if (FAILED(commandList->Close())) {
            return false;
        }

        // Render targets
        if (!CreateRenderTargets()) {
            return false;
        }

        return true;
    }

    /// @brief Destroys all resources associated with this object.
    void Destroy() {
        WaitForPendingOperations();
        for (auto &ctx : frameContext) {
            ctx.Destroy();
        }
        swapChain.Destroy();
        commandList.Destroy();
        commandQueue.Destroy();
        rtvHeap.Destroy();
        fence.Destroy();
    }

    /// @brief Determines if the resources contained in this object are valid.
    /// @return `true` if all render target resources are valid, `false` otherwise
    bool IsValid() const {
        if (!swapChain.IsValid()) {
            return false;
        }
        if (!rtvHeap.IsValid()) {
            return false;
        }
        for (auto &ctx : frameContext) {
            if (!ctx.IsValid()) {
                return false;
            }
        }
        if (!fence.IsValid()) {
            return false;
        }
        if (!commandQueue.IsValid()) {
            return false;
        }
        if (!commandList.IsValid()) {
            return false;
        }
        return true;
    }

    /// @brief Resizes the swap chain buffers to the specified dimensions.
    /// @param[in] width the new framebuffer width
    /// @param[in] height the new framebuffer height
    void ResizeSwapChainBuffers(UINT width, UINT height) {
        CleanupRenderTarget();
        swapChain.ResizeBuffers(width, height);
        CreateRenderTargets();
    }

    /// @brief Renders the next frame.
    /// @tparam TFnRender the render function type, an invocable object taking `FrameContext &backBufferCtx` and
    /// `D3D12GraphicsCommandList &commandList` arguments
    /// @tparam TFnPostRender the post-render function type, an invocable object taking no arguments
    /// @param[in] vsync whether to present with vertical synchronization
    /// @param[in] fnRender a function that will render graphics to this frame
    /// @param[in] fnPostRender a function that performs additional work between submitting the command list and
    /// presenting the frame
    template <typename TFnRender, typename TFnPostRender>
        requires(std::invocable<TFnRender, FrameContext &, D3D12GraphicsCommandList &> && std::invocable<TFnPostRender>)
    void Render(bool vsync, TFnRender &&fnRender, TFnPostRender fnPostRender) {
        FrameContext &frameCtx = WaitForNextFrameContext();
        UINT backBufferIdx = swapChain->GetCurrentBackBufferIndex();
        FrameContext &backBufferCtx = frameContext[backBufferIdx];
        frameCtx.commandAllocator->Reset();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = backBufferCtx.renderTargetResource.get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->Reset(frameCtx.commandAllocator.GetPointer(), nullptr);
        commandList->ResourceBarrier(1, &barrier);

        fnRender(backBufferCtx, commandList);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commandList->ResourceBarrier(1, &barrier);
        commandList->Close();

        commandQueue->ExecuteCommandLists(1, commandList.GetAddressOfBase());

        fnPostRender();

        fence.Signal(commandQueue);
        frameCtx.fenceValue = fence.GetLastSignaledValue();

        HRESULT hr;
        if (vsync) {
            hr = swapChain.Present(1, 0);
        } else {
            hr = swapChain.Present(0, swapChain.IsTearingSupported() ? DXGI_PRESENT_ALLOW_TEARING : 0);
        }
        swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
        frameIndex++;
    }

    /// @brief Waits until all pending operations have been executed by the GPU.
    void WaitForPendingOperations() {
        fence.Signal(commandQueue);
        fence.Wait(INFINITE);
    }

private:
    /// @brief Creates the render target resources
    /// @return `true` if the resources were created successfully, `false` otherwise
    bool CreateRenderTargets() {
        frameIndex = swapChain->GetCurrentBackBufferIndex();

        for (UINT i = 0; i < kFrameCount; i++) {
            if (!frameContext[i].CreateRenderTarget(*pDevice, swapChain.GetPointer(), i)) {
                return false;
            }
        }

        return true;
    }

    /// @brief Destroys the render target resources.
    void CleanupRenderTarget() {
        WaitForPendingOperations();
        for (auto &ctx : frameContext) {
            ctx.DestroyRenderTarget();
        }
    }

    /// @brief Waits until the current frame is rendered and retrieves the next frame context.
    /// @return a reference to the next frame context
    FrameContext &WaitForNextFrameContext() {
        FrameContext &ctx = frameContext[frameIndex % kFrameCount];
        if (fence->GetCompletedValue() < ctx.fenceValue) {
            HANDLE waitableObjects[] = {swapChain.WaitableObject(), fence.SetupWait(ctx.fenceValue)};
            ::WaitForMultipleObjects(2, waitableObjects, TRUE, INFINITE);
        } else {
            ::WaitForSingleObject(swapChain.WaitableObject(), INFINITE);
        }

        return ctx;
    }
};

} // namespace ymir::gpu::d3d12
