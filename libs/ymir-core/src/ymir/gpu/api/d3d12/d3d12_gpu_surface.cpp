#include <ymir/gpu/api/d3d12/d3d12_gpu_surface.hpp>

#include <ymir/gpu/api/d3d12/d3d12_gpu_command_queue.hpp>

namespace ymir::gpu {

D3D12Surface::D3D12Surface(d3d12::D3D12Device &device, d3d12::D3D12SwapChain swapchain)
    : IGPUSurface(kBackend)
    , m_device(device)
    , m_swapchain(std::move(swapchain)) {}

GPUObjectResult<IGPUSurface> D3D12Surface::Create(d3d12::D3D12Device &device, const WindowParams &windowParams,
                                                  const IGPUCommandQueue &queue, uint32 maxFramesInFlight, bool debug,
                                                  const util::PropertyBag *props) {
    auto *dx12Queue = queue.As<D3D12CommandQueue>();
    if (dx12Queue == nullptr) {
        return GPUOperationError{"Provided command queue is not a Direct3D 12 object"};
    }

    const HWND hwnd = static_cast<HWND>(windowParams.windowHandle);
    if (hwnd == nullptr) {
        return GPUOperationError{"HWND not specified"};
    }

    UINT dxgiFactoryFlags = util::PropertyBag::NullSafeGetOrDefault<props::D3D12DXGIFactoryFlags>(props);
    if (debug) {
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = windowParams.width;
    desc.Height = windowParams.height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = maxFramesInFlight;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    d3d12::D3D12SwapChain swapchain{};
    if (!swapchain.Create(dxgiFactoryFlags, dx12Queue->GetCommandQueue().GetPointer(), desc, hwnd, maxFramesInFlight)) {
        return GPUOperationError{fmt::format("Failed to create swapchain")};
    }

    // TODO: create textures and views

    return {std::make_unique<D3D12Surface>(device, std::move(swapchain))};
}

void D3D12Surface::SetName(std::string_view name) {
    // TODO: rename textures and views
}

} // namespace ymir::gpu
