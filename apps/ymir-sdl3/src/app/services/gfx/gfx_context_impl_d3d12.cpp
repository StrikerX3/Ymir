#include "gfx_context_impl_d3d12.hpp"

#include "gfx_context_spec_d3d12.hpp"

#include <ymir/gpu/d3d12/d3d12_debug.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap.hpp>
#include <ymir/gpu/d3d12/d3d12_device.hpp>
#include <ymir/gpu/d3d12/d3d12_fence.hpp>
#include <ymir/gpu/d3d12/d3d12_pipeline_state.hpp>
#include <ymir/gpu/d3d12/d3d12_resource.hpp>
#include <ymir/gpu/d3d12/d3d12_swap_chain.hpp>

#include <d3d12.h>

#include <wil/com.h>

#include <fmt/format.h>

#include <array>

using namespace ymir::gpu::d3d12;

namespace app::gfx {

struct Direct3D12GraphicsContext::Impl {
    static constexpr UINT kFrameCount = 3;

    struct FrameContext {
        D3D12Resource renderTarget;
        D3D12CommandAllocator cmdAlloc;
        FenceCounter fenceCounter;
    };

    D3D12Device device;
    D3D12CommandQueue cmdQueue;
    D3D12CommandAllocator cmdAlloc;
    D3D12GraphicsCommandList cmdList;
    D3D12SwapChain swapchain;
    D3D12DescriptorHeap rtvHeap;
    D3D12DescriptorHeap resourceHeap; // for user-created textures
    D3D12PipelineState pipelineState;
    D3D12Fence fence;
    FenceCounter fenceCounter;
    std::array<FrameContext, kFrameCount> frames;

    UINT frameIndex = 0;

    util::VoidResult<> Create(const Direct3D12GraphicsContextSpec &spec) {
        if (spec.hwnd == nullptr) {
            return util::ErrorMessage{"No window handle provided to Direct3D 12 specification"};
        }

        RECT windowRect;
        if (!GetClientRect(spec.hwnd, &windowRect)) {
            return util::ErrorMessage{"Could not get window client area size"};
        }

        DebugLayer &debugLayer = DebugLayer::Get();

        UINT dxgiFactoryFlags = 0;
        if (debugLayer.Init() && debugLayer.IsEnabled()) {
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }

        if (FAILED(device.Create(nullptr, spec.featureLevel))) {
            return util::ErrorMessage{"Failed to create device"};
        }
        debugLayer.BreakOnWarnings(device.GetPointer(), true);
        device->SetName(L"[Ymir] D3D12 device");

        if (FAILED(cmdQueue.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT))) {
            return util::ErrorMessage{"Failed to create command queue"};
        }

        // Create synchronization object
        if (FAILED(fence.Create(device, 0, D3D12_FENCE_FLAG_NONE))) {
            return util::ErrorMessage{"Failed to create fence"};
        }
        fence->SetName(L"[Ymir-GCtx] Fence");
        fenceCounter.Bind(fence);

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = kFrameCount;
        swapChainDesc.Width = windowRect.right;
        swapChainDesc.Height = windowRect.bottom;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (!swapchain.Create(dxgiFactoryFlags, cmdQueue.GetPointer(), swapChainDesc, spec.hwnd, kFrameCount)) {
            return util::ErrorMessage{"Failed to create swapchain"};
        }
        frameIndex = swapchain->GetCurrentBackBufferIndex();

        // Create descriptor heaps
        {
            D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
            rtvHeapDesc.NumDescriptors = kFrameCount;
            rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(rtvHeap.Create(device, rtvHeapDesc))) {
                return util::ErrorMessage{"Failed to create RTV descriptor heap"};
            }
            rtvHeap->SetName(L"[Ymir-GCtx] RTV heap");

            D3D12_DESCRIPTOR_HEAP_DESC resourceHeapDesc{};
            resourceHeapDesc.NumDescriptors = 2;
            resourceHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            resourceHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            if (FAILED(resourceHeap.Create(device, resourceHeapDesc))) {
                return util::ErrorMessage{"Failed to create CBV/SRV/UAV heap"};
            }
            resourceHeap->SetName(L"[Ymir-GCtx] CBV/SRV/UAV heap");
        }

        // Create frame resources
        {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap.GetCPUStart());

            // Create a RTV for each frame
            for (UINT n = 0; n < kFrameCount; n++) {
                ID3D12Resource *resource;
                if (FAILED(swapchain->GetBuffer(n, IID_PPV_ARGS(&resource)))) {
                    return util::ErrorMessage{fmt::format("Failed to get swapchain buffer {}", n)};
                }
                if (FAILED(frames[n].cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT))) {
                    return util::ErrorMessage{
                        fmt::format("Failed to create command allocator for swapchain frame #{}", n)};
                }
                device->CreateRenderTargetView(resource, nullptr, rtvHandle);
                resource->SetName(fmt::format(L"[Ymir-GCtx] Swapchain buffer #{}", n).c_str());
                frames[n].renderTarget.Attach(resource);
                frames[n].fenceCounter.Bind(fence);
                rtvHandle.ptr += rtvHeap.GetDescriptorSize();
            }
        }

        // Create command allocator and list
        if (FAILED(cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT))) {
            return util::ErrorMessage{"Failed to create main command allocator"};
        }
        if (FAILED(cmdList.Create(device, cmdAlloc, D3D12_COMMAND_LIST_TYPE_DIRECT, pipelineState.GetPointer()))) {
            return util::ErrorMessage{"Failed to create command list"};
        }
        cmdList->Close();
        cmdAlloc->SetName(L"[Ymir-GCtx] Command allocator");
        cmdList->SetName(L"[Ymir-GCtx] Command list");

        return {};
    }

    util::VoidResult<> WaitForGPU() {
        FenceCounter &fenceCounter = frames[frameIndex].fenceCounter;

        // Schedule a signal command in the queue and wait for it
        if (FAILED(fenceCounter.Signal(cmdQueue))) {
            return util::ErrorMessage{"Failed to signal fence"};
        }
        fenceCounter.Wait(INFINITE);

        return {};
    }

    util::VoidResult<> MoveToNextFrame() {
        FenceCounter &fenceCounter = frames[frameIndex].fenceCounter;

        // Schedule a signal command in the queue
        if (FAILED(fenceCounter.Signal(cmdQueue))) {
            return util::ErrorMessage{"Failed to signal fence"};
        }

        // If the next frame is not ready to be rendered yet, wait until it is ready
        if (fence.GetCompletedValue() < fenceCounter.GetCounter()) {
            fenceCounter.Wait(INFINITE);
        }

        frameIndex = swapchain->GetCurrentBackBufferIndex();

        return {};
    }
};

// -----------------------------------------------------------------------------

Direct3D12GraphicsContext::Direct3D12GraphicsContext(std::unique_ptr<Impl> &&impl)
    : IGraphicsContext(kBackend)
    , m_impl(std::move(impl)) {}

Direct3D12GraphicsContext::~Direct3D12GraphicsContext() = default;

util::ObjectResult<Direct3D12GraphicsContext>
Direct3D12GraphicsContext::Create(const Direct3D12GraphicsContextSpec &spec) {
    auto impl = std::make_unique<Impl>();
    if (!impl) {
        return util::ErrorMessage{"Could not allocate memory for Direct3D 12 graphics context"};
    }

    auto result = impl->Create(spec);
    if (!result) {
        return result.Error();
    }

    return std::make_unique<Direct3D12GraphicsContext>(std::move(impl));
}

void Direct3D12GraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    // TODO: enqueue command to clear screen
}

bool Direct3D12GraphicsContext::ImGuiInit() {
    // TODO: invoke the appropriate ImGui_Impl*_Init* functions
    return false;
}

void Direct3D12GraphicsContext::ImGuiShutdown() {
    // TODO: invoke the appropriate ImGui_Impl*_Shutdown* functions
}

void Direct3D12GraphicsContext::ImGuiNewFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_NewFrame functions
}

void Direct3D12GraphicsContext::ImGuiRenderFrame() {
    // TODO: invoke the appropriate ImGui_Impl*_RenderDrawData function
}

util::ValueResult<TextureID> Direct3D12GraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    // TODO: create and store texture object in a hash map
    // The texture ID will be the hash map key, not the native object pointer, because resizing the texture requires
    // creating a new object and these IDs must be immutable for the lifetime of the logical texture.
    return util::ErrorMessage{"Unimplemented"};
}

void Direct3D12GraphicsContext::DestroyTexture(TextureID id) {
    // TODO: delete texture
}

bool Direct3D12GraphicsContext::IsTextureValid(TextureID id) const {
    // TODO: check if the texture is still live
    return true;
}

ImTextureID Direct3D12GraphicsContext::GetImGuiTextureID(TextureID id) const {
    // TODO: get and return texture ID
    return 0;
}

util::VoidResult<> Direct3D12GraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    // TODO: destroy and recreate texture with new dimensions
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<>
Direct3D12GraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                         const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    // TODO: map texture, invoke fnUpdate with contents, unmap texture; handle errors
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                              const FRect &dstRect) {
    // TODO: set render target to dst texture, draw texture, restore render target
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect,
                                                                 const FRect &dstRect, double rotAngle,
                                                                 const FPoint2D *anchorPoint) {
    // TODO: imitate SDL_RenderTextureRotated:
    // - srcRect specifies the source texture region to copy from (in texels)
    // - dstRect specifies the destination texture region to copy to (in texels)
    // - rotAngle is the clockwise rotation angle (in degrees)
    // - anchorPoint is the rotation anchor point. If null, use the center of the destination rectangle
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::SetPresentMode(PresentMode mode) {
    // TODO: set presentation mode
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::Present() {
    // TODO: present next frame and wait for vertical retrace if enabled
    return util::ErrorMessage{"Unimplemented"};
}

wil::com_ptr_nothrow<ID3D12Device> Direct3D12GraphicsContext::GetDevice() const {
    return m_impl->device.GetPointer();
}

} // namespace app::gfx
