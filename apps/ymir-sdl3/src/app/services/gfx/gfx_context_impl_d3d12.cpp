#include "gfx_context_impl_d3d12.hpp"

#include "gfx_context_spec_d3d12.hpp"

#include <ymir/gpu/d3d12/d3d12_debug.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap_allocator.hpp>
#include <ymir/gpu/d3d12/d3d12_device.hpp>
#include <ymir/gpu/d3d12/d3d12_fence.hpp>
#include <ymir/gpu/d3d12/d3d12_pipeline_state.hpp>
#include <ymir/gpu/d3d12/d3d12_resource.hpp>
#include <ymir/gpu/d3d12/d3d12_swap_chain.hpp>

#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_sdl3.h>

#include <d3d12.h>

#include <wil/com.h>

#include <fmt/format.h>

#include <array>

using namespace ymir::gpu::d3d12;

namespace app::gfx {

struct Direct3D12GraphicsContext::Impl {
    explicit Impl(const Direct3D12GraphicsContextSpec &spec)
        : spec(spec) {}

    static constexpr UINT kFrameCount = 3;

    Direct3D12GraphicsContextSpec spec;

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
    D3D12DescriptorHeap resourceHeap; // for user-created and ImGui textures
    DescriptorHeapAllocator resourceHeapAlloc;
    D3D12PipelineState pipelineState;
    D3D12Fence fence;
    FenceCounter fenceCounter;
    std::array<FrameContext, kFrameCount> frames;

    UINT frameIndex = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;

    util::VoidResult<> Init() {
        if (spec.window == nullptr) {
            return util::ErrorMessage{"No window provided to Direct3D 12 specification"};
        }

        SDL_PropertiesID windowProps = SDL_GetWindowProperties(spec.window);
        auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (hwnd == nullptr) {
            return util::ErrorMessage{"Could not get window handle"};
        }

        RECT windowRect;
        if (!GetClientRect(hwnd, &windowRect)) {
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
        if (!swapchain.Create(dxgiFactoryFlags, cmdQueue.GetPointer(), swapChainDesc, hwnd, kFrameCount)) {
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
            resourceHeapDesc.NumDescriptors = 131072;
            resourceHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            resourceHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            if (FAILED(resourceHeap.Create(device, resourceHeapDesc))) {
                return util::ErrorMessage{"Failed to create CBV/SRV/UAV heap"};
            }
            resourceHeap->SetName(L"[Ymir-GCtx] CBV/SRV/UAV heap");

            resourceHeapAlloc.Bind(resourceHeap);
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
                frames[n].cmdAlloc->SetName(
                    fmt::format(L"[Ymir-GCtx] Command allocator for swapchain buffer #{}", n).c_str());
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

    void WaitForAllOperations() {
        WaitForGPU();
        if (SUCCEEDED(fenceCounter.Signal(cmdQueue))) {
            fenceCounter.Wait(INFINITE);
        }
        for (UINT n = 0; n < kFrameCount; n++) {
            if (SUCCEEDED(frames[n].fenceCounter.Signal(cmdQueue))) {
                frames[n].fenceCounter.Wait(INFINITE);
            }
        }
    }

    void Shutdown() {
        fenceCounter.Unbind();
        for (UINT n = 0; n < kFrameCount; n++) {
            frames[n].fenceCounter.Unbind();
            frames[n].renderTarget.Destroy();
            frames[n].cmdAlloc.Destroy();
        }
        fence.Destroy();
        pipelineState.Destroy();
        cmdList.Destroy();
        cmdAlloc.Destroy();
        cmdQueue.Destroy();
        resourceHeapAlloc.Unbind();
        resourceHeap.Destroy();
        rtvHeap.Destroy();
        swapchain.Destroy();
        device.Destroy();
    }

    bool IsInitialized() const {
        return device.IsValid();
    }

    util::VoidResult<> BeginFrame() {
        ymir::gpu::d3d12::D3D12CommandAllocator &cmdAlloc = frames[frameIndex].cmdAlloc;

        if (FAILED(cmdAlloc->Reset())) {
            return util::ErrorMessage{"Failed to reset command allocator"};
        }
        if (FAILED(cmdList->Reset(cmdAlloc.GetPointer(), pipelineState.GetPointer()))) {
            return util::ErrorMessage{"Failed to reset command list"};
        }

        ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer()};
        cmdList->SetDescriptorHeaps(std::size(heaps), heaps);

        // Indicate that the back buffer will be used as a render target
        if (auto *list7 = cmdList.As7()) {
            // TODO: check for enhanced barrier support
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_NONE,
                .SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET,
                .AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS,
                .AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                .pResource = frames[frameIndex].renderTarget.GetPointer(),
                .Subresources =
                    {
                        .IndexOrFirstMipLevel = 0,
                        .NumMipLevels = 1,
                        .FirstArraySlice = 0,
                        .NumArraySlices = 1,
                        .FirstPlane = 0,
                        .NumPlanes = 1,
                    },
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_DISCARD,
            };
            const D3D12_BARRIER_GROUP group{
                .Type = D3D12_BARRIER_TYPE_TEXTURE,
                .NumBarriers = 1,
                .pTextureBarriers = &barrier,
            };
            list7->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = frames[frameIndex].renderTarget.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                        .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
                    },
            };
            cmdList->ResourceBarrier(1, &barrier);
        }

        rtvHandle = rtvHeap.GetCPUStart();
        rtvHandle.ptr += static_cast<SIZE_T>(frameIndex) * rtvHeap.GetDescriptorSize();
        cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        return {};
    }

    util::VoidResult<> EndFrame() {
        // Indicate that the back buffer will be used for frame presentation
        if (auto *list7 = cmdList.As7()) {
            // TODO: check for enhanced barrier support
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET,
                .SyncAfter = D3D12_BARRIER_SYNC_NONE,
                .AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET,
                .AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON,
                .pResource = frames[frameIndex].renderTarget.GetPointer(),
                .Subresources =
                    {
                        .IndexOrFirstMipLevel = 0,
                        .NumMipLevels = 1,
                        .FirstArraySlice = 0,
                        .NumArraySlices = 1,
                        .FirstPlane = 0,
                        .NumPlanes = 1,
                    },
                .Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE,
            };
            const D3D12_BARRIER_GROUP group{
                .Type = D3D12_BARRIER_TYPE_TEXTURE,
                .NumBarriers = 1,
                .pTextureBarriers = &barrier,
            };
            list7->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = frames[frameIndex].renderTarget.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                        .StateAfter = D3D12_RESOURCE_STATE_PRESENT,
                    },
            };
            cmdList->ResourceBarrier(1, &barrier);
        }

        if (FAILED(cmdList->Close())) {
            return util::ErrorMessage{"Failed to close command list"};
        }

        return {};
    }

    util::VoidResult<> Present() {
        ID3D12CommandList *ppCommandLists[] = {cmdList.GetPointer()};
        cmdQueue->ExecuteCommandLists(std::size(ppCommandLists), ppCommandLists);

        // TODO: honor selected presentation mode
        swapchain->Present(1, 0);

        return MoveToNextFrame();
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

Direct3D12GraphicsContext::Direct3D12GraphicsContext(const Direct3D12GraphicsContextSpec &spec)
    : IGraphicsContext(kBackend)
    , m_impl(std::make_unique<Impl>(spec)) {}

Direct3D12GraphicsContext::~Direct3D12GraphicsContext() {
    Shutdown();
}

util::ObjectResult<Direct3D12GraphicsContext>
Direct3D12GraphicsContext::Create(const Direct3D12GraphicsContextSpec &spec) {
    auto context = std::make_unique<Direct3D12GraphicsContext>(spec);
    auto result = context->Initialize();
    if (!result) {
        return result.Error();
    }
    return std::move(context);
}

util::VoidResult<> Direct3D12GraphicsContext::Initialize() {
    return m_impl->Init();
}

void Direct3D12GraphicsContext::Shutdown() {
    m_impl->WaitForAllOperations();
    ImGuiShutdown();
    m_impl->Shutdown();
}

bool Direct3D12GraphicsContext::IsInitialized() const {
    return m_impl->IsInitialized();
}

util::VoidResult<> Direct3D12GraphicsContext::ResizeFramebuffer(uint32 width, uint32 height) {
    // TODO: destroy and recreate swap chain resources
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::BeginFrame() {
    return m_impl->BeginFrame();
}

util::VoidResult<> Direct3D12GraphicsContext::EndFrame() {
    return m_impl->EndFrame();
}

void Direct3D12GraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    const float clearColor[] = {color.r, color.g, color.b, color.a};
    m_impl->cmdList->ClearRenderTargetView(m_impl->rtvHandle, clearColor, 0, nullptr);
}

bool Direct3D12GraphicsContext::ImGuiInit() {
    if (!m_imguiInitialized) {
        ImGui_ImplDX12_InitInfo initInfo{};
        initInfo.Device = m_impl->device.GetPointer();
        initInfo.CommandQueue = m_impl->cmdQueue.GetPointer();
        initInfo.NumFramesInFlight = Impl::kFrameCount;
        initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.UserData = m_impl.get();
        initInfo.SrvDescriptorHeap = m_impl->resourceHeap.GetPointer();
        initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE *out_cpu_handle,
                                           D3D12_GPU_DESCRIPTOR_HANDLE *out_gpu_handle) {
            auto &impl = *static_cast<Impl *>(info->UserData);
            UINT index;
            impl.resourceHeapAlloc.Allocate(*out_cpu_handle, *out_gpu_handle, index);
        };
        initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                                          D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
            auto &impl = *static_cast<Impl *>(info->UserData);
            UINT index = impl.resourceHeapAlloc.GetIndex(cpu_handle);
            impl.resourceHeapAlloc.Free(index);
        };
        m_imguiInitialized =                                  //
            ImGui_ImplSDL3_InitForD3D(m_impl->spec.window) && //
            ImGui_ImplDX12_Init(&initInfo);
    }
    return m_imguiInitialized;
}

void Direct3D12GraphicsContext::ImGuiShutdown() {
    if (m_imguiInitialized) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        m_imguiInitialized = false;
    }
}

void Direct3D12GraphicsContext::ImGuiNewFrame() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

void Direct3D12GraphicsContext::ImGuiRenderFrame() {
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_impl->cmdList.GetPointer());
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
    return m_impl->Present();
}

wil::com_ptr_nothrow<ID3D12Device> Direct3D12GraphicsContext::GetDevice() const {
    return m_impl->device.GetPointer();
}

} // namespace app::gfx
