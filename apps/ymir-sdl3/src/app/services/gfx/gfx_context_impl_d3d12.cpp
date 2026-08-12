#include "gfx_context_impl_d3d12.hpp"

#include "gfx_context_spec_d3d12.hpp"

#include <ymir/gpu/d3d12/d3d12_debug.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap_allocator.hpp>
#include <ymir/gpu/d3d12/d3d12_device.hpp>
#include <ymir/gpu/d3d12/d3d12_fence.hpp>
#include <ymir/gpu/d3d12/d3d12_pipeline_state.hpp>
#include <ymir/gpu/d3d12/d3d12_resource.hpp>
#include <ymir/gpu/d3d12/d3d12_root_signature.hpp>
#include <ymir/gpu/d3d12/d3d12_swap_chain.hpp>

#include <ymir/gpu/shaders/gpu_shaders.hpp>

#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_sdl3.h>

#include <d3d12.h>

#include <fmt/format.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_shaders);

#include <array>
#include <deque>
#include <unordered_map>

using namespace ymir::gpu;
using namespace ymir::gpu::d3d12;

namespace app::gfx {

/// @brief Converts the given UTF-8-encoded string to a wide string.
/// @param[in] str the string to convert
/// @return the string converted to `std::wstring`
static std::wstring StringToWString(std::string_view str) {
    if (str.empty()) {
        return L"";
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size);
    return wstr;
}

static DXGI_FORMAT ToD3D12Value(PixelFormat format) {
    switch (format) {
    case PixelFormat::Unknown: return DXGI_FORMAT_UNKNOWN;
    case PixelFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case PixelFormat::R8G8B8X8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM; // Note: A instead of X
    case PixelFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case PixelFormat::B8G8R8X8_UNORM: return DXGI_FORMAT_B8G8R8X8_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

struct alignas(uint32) Float4 {
    float x, y, z, w;
};

struct alignas(uint32) Float3 {
    float x, y, z;
};

struct alignas(uint32) Float2 {
    float x, y;
};

struct Vertex {
    Float3 position;
    Float2 uv;
};

struct alignas(256) DrawTextureConstants {
    Float4 srcRect;
    Float4 dstRect;
    Float2 anchorPoint;
    float rotAngle;
};

struct Descriptor {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
    UINT index;
};

// -----------------------------------------------------------------------------

struct Direct3D12GraphicsContext::Impl {
    explicit Impl(const Direct3D12GraphicsContextSpec &spec)
        : spec(spec) {}

    static constexpr UINT kFrameCount = 3;

    Direct3D12GraphicsContextSpec spec;

    struct FrameContext {
        D3D12Resource renderTarget;
        D3D12CommandAllocator cmdAlloc;
        UINT64 fenceValue;
    };

    D3D12Device device;
    D3D12CommandQueue cmdQueue;
    D3D12GraphicsCommandList cmdListFrame;
    D3D12GraphicsCommandList cmdListOps;
    D3D12SwapChain swapchain;
    D3D12DescriptorHeap rtvHeap;
    D3D12DescriptorHeap resourceHeap;
    DescriptorHeapAllocator resourceHeapAlloc;
    D3D12DescriptorHeap samplerHeap;
    DescriptorHeapAllocator samplerHeapAlloc;
    D3D12RootSignature rootSignature;
    D3D12PipelineState pipelineState;
    D3D12Fence fence;
    std::array<FrameContext, kFrameCount> frames;

    UINT frameIndex = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect;

    Descriptor smpNearest;
    Descriptor smpLinear;

    D3D12Resource vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

    D3D12Resource constBuffer;
    void *constBufferPtr = nullptr;
    Descriptor cbvQuad;

    static constexpr D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    PresentMode presentMode = PresentMode::VSync;

    struct Features {
        bool enhancedBarriers = false;
    } features;

    struct TextureInstance {
        Texture2DSpec spec;
        D3D12Resource texture;
        std::array<D3D12Resource, kFrameCount> stagingBuffers;
        std::array<void *, kFrameCount> stagingBuffersData;

        Descriptor srvDesc;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        UINT numRows;
        UINT64 rowSizeBytes;
        UINT64 uploadBufferSize;
        size_t rowPitch;
    };

    struct TextureToDelete {
        D3D12Resource texture;
        std::array<D3D12Resource, kFrameCount> stagingBuffers;
        UINT srvIndex;
        UINT64 targetFenceVance;

        void Destroy(DescriptorHeapAllocator &resourceHeapAlloc) {
            for (int i = 0; i < kFrameCount; ++i) {
                stagingBuffers[i]->Unmap(0, nullptr);
                stagingBuffers[i].Destroy();
            }
            texture.Destroy();
            resourceHeapAlloc.Free(srvIndex);
        }
    };

    DrawTextureConstants drawTextureConstants;

    std::unordered_map<TextureID, TextureInstance> textures;
    std::deque<TextureToDelete> texturesToDelete;

    // -------------------------------------------------------------------------

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

        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = windowRect.right;
        viewport.Height = windowRect.bottom;

        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right = windowRect.right;
        scissorRect.bottom = windowRect.bottom;

        DebugLayer &debugLayer = DebugLayer::Get();

        UINT dxgiFactoryFlags = 0;
        if (debugLayer.Init() && debugLayer.IsEnabled()) {
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }

        if (FAILED(device.Create(nullptr, spec.featureLevel))) {
            return util::ErrorMessage{"Failed to create device"};
        }
        debugLayer.BreakOnWarnings(device.GetPointer(), true);
        device->SetName(L"[Ymir-GCtx] D3D12 device");

        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12)))) {
            features.enhancedBarriers = options12.EnhancedBarriersSupported;
        }

        if (FAILED(cmdQueue.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT))) {
            return util::ErrorMessage{"Failed to create command queue"};
        }

        // Create synchronization object
        if (FAILED(fence.Create(device, 0, D3D12_FENCE_FLAG_NONE))) {
            return util::ErrorMessage{"Failed to create fence"};
        }
        fence->SetName(L"[Ymir-GCtx] Fence");

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = kFrameCount;
        swapChainDesc.Width = windowRect.right;
        swapChainDesc.Height = windowRect.bottom;
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
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

            D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc{};
            samplerHeapDesc.NumDescriptors = 2;
            samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            if (FAILED(samplerHeap.Create(device, samplerHeapDesc))) {
                return util::ErrorMessage{"Failed to create sampler heap"};
            }
            samplerHeap->SetName(L"[Ymir-GCtx] Sampler heap");
            samplerHeapAlloc.Bind(samplerHeap);
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
                frames[n].fenceValue = 0;
                rtvHandle.ptr += rtvHeap.GetDescriptorSize();
            }
        }

        // Create command lists
        D3D12CommandAllocator &cmdAlloc = frames[frameIndex].cmdAlloc;
        if (FAILED(cmdListFrame.Create(device, cmdAlloc, D3D12_COMMAND_LIST_TYPE_DIRECT, pipelineState.GetPointer()))) {
            return util::ErrorMessage{"Failed to create frame command list"};
        }
        cmdListFrame->Close();
        cmdListFrame->SetName(L"[Ymir-GCtx] Frame command list");

        if (FAILED(cmdListOps.Create(device, cmdAlloc, D3D12_COMMAND_LIST_TYPE_DIRECT, pipelineState.GetPointer()))) {
            return util::ErrorMessage{"Failed to create operations command list"};
        }
        cmdListOps->Close();
        cmdListOps->SetName(L"[Ymir-GCtx] Operations command list");

        // Create root signature for texture drawing operations with these descriptors tables:
        // [0] one CBV slot for the quad vertex data
        // [1] one SRV slot for the texture to draw
        // [2] one sampler slot to pick between nearest neighbor and linear interpolation
        {
            auto rootSigBuilder = rootSignature.Builder();
            rootSigBuilder.Flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
            rootSigBuilder.AddDescriptorTable(D3D12_SHADER_VISIBILITY_VERTEX).AddCBVs(1, 0);
            rootSigBuilder.AddDescriptorTable(D3D12_SHADER_VISIBILITY_PIXEL).AddSRVs(1, 0);
            rootSigBuilder.AddDescriptorTable(D3D12_SHADER_VISIBILITY_PIXEL).AddSamplers(1, 0);
            if (HRESULT hr = rootSigBuilder.Build(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Failed to create texture operations root signature, error code {:X}", (uint32)hr)};
            }
            rootSignature->SetName(L"[Ymir-GCtx] Texture operations root signature");
        }

        // Create nearest neighbor and linear samplers
        {
            // Allocate descriptors
            if (!samplerHeapAlloc.Allocate(smpNearest.cpuHandle, smpNearest.gpuHandle, smpNearest.index)) {
                return util::ErrorMessage{"Could not allocate nearest neighbor sampler descriptor"};
            }
            if (!samplerHeapAlloc.Allocate(smpLinear.cpuHandle, smpLinear.gpuHandle, smpLinear.index)) {
                return util::ErrorMessage{"Could not allocate linear sampler descriptor"};
            }

            // Common sampler parameters
            D3D12_SAMPLER_DESC samplerDesc{
                .AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                .AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                .AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
                .BorderColor = {0.0f, 0.0f, 0.0f, 0.0f},
                .MinLOD = 0.0f,
                .MaxLOD = D3D12_FLOAT32_MAX,
            };

            // Nearest neighbor
            samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
            device->CreateSampler(&samplerDesc, smpNearest.cpuHandle);

            // Linear
            samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            device->CreateSampler(&samplerDesc, smpLinear.cpuHandle);
        }

        // Load shaders
        VertexShader vertexShader;
        PixelShader pixelShader;
        {
            auto fs = cmrc::ymir_shaders::get_filesystem();
            auto loadShader = [&](const char *path) -> util::ValueResult<std::vector<char>> {
                assert(fs.is_file(path));
                auto shaderFile = fs.open(path);
                return std::vector<char>{shaderFile.begin(), shaderFile.end()};
            };

            // Load vertex shader
            auto vertexShaderBytecodeResult = loadShader("gctx/d3d12/vs_quad.cso");
            if (!vertexShaderBytecodeResult) {
                return util::ErrorMessage{
                    fmt::format("Could not load vertex shader: {}", vertexShaderBytecodeResult.Error().message)};
            }
            vertexShader.format = ShaderBytecodeFormat::DXIL;
            vertexShader.bytecode = vertexShaderBytecodeResult.Value();
            vertexShader.entrypoint = "VSMain";
            if (auto result = ValidateShader(vertexShader); !result) {
                return util::ErrorMessage{fmt::format("Vertex shader validation failed: {}", result.Error().message)};
            }

            // Load pixel shader
            auto pixelShaderBytecodeResult = loadShader("gctx/d3d12/ps_quad.cso");
            if (!pixelShaderBytecodeResult) {
                return util::ErrorMessage{
                    fmt::format("Could not load pixel shader: {}", pixelShaderBytecodeResult.Error().message)};
            }
            pixelShader.format = ShaderBytecodeFormat::DXIL;
            pixelShader.bytecode = pixelShaderBytecodeResult.Value();
            pixelShader.entrypoint = "PSMain";
            if (auto result = ValidateShader(pixelShader); !result) {
                return util::ErrorMessage{fmt::format("Pixel shader validation failed: {}", result.Error().message)};
            }
        }

        // Create the graphics pipeline state object (PSO)
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.InputLayout = {inputElementDescs, std::size(inputElementDescs)};
            psoDesc.pRootSignature = rootSignature.GetPointer();
            psoDesc.VS = {.pShaderBytecode = vertexShader.bytecode.data(),
                          .BytecodeLength = vertexShader.bytecode.size()};
            psoDesc.PS = {.pShaderBytecode = pixelShader.bytecode.data(),
                          .BytecodeLength = pixelShader.bytecode.size()};
            psoDesc.RasterizerState = {
                .FillMode = D3D12_FILL_MODE_SOLID,
                .CullMode = D3D12_CULL_MODE_BACK,
                .FrontCounterClockwise = FALSE,
                .DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
                .DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
                .SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
                .DepthClipEnable = TRUE,
                .MultisampleEnable = FALSE,
                .AntialiasedLineEnable = FALSE,
                .ForcedSampleCount = 0,
                .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
            };
            psoDesc.BlendState = {
                .AlphaToCoverageEnable = FALSE,
                .IndependentBlendEnable = FALSE,
                .RenderTarget = {{
                    .BlendEnable = FALSE,
                    .LogicOpEnable = FALSE,
                    .SrcBlend = D3D12_BLEND_ONE,
                    .DestBlend = D3D12_BLEND_ZERO,
                    .BlendOp = D3D12_BLEND_OP_ADD,
                    .SrcBlendAlpha = D3D12_BLEND_ONE,
                    .DestBlendAlpha = D3D12_BLEND_ZERO,
                    .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                    .LogicOp = D3D12_LOGIC_OP_NOOP,
                    .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
                }},
            };
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.DepthStencilState.StencilEnable = FALSE;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count = 1;
            if (HRESULT hr = pipelineState.CreateGraphics(device, psoDesc); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Failed to create graphics pipeline state object, error code {:X}", (uint32)hr)};
            }
            pipelineState->SetName(L"[Ymir-GCtx] Graphics pipeline");
        }

        // Create the vertex buffer
        D3D12Resource vertexUploadBuffer{};
        {
            // Define the geometry for a quad
            Vertex vertices[] = {
                {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
                {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
                {{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
                {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
            };

            const UINT vertexBufferSize = sizeof(vertices);
            const D3D12_RESOURCE_DESC vertexBufferDesc{
                .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                .Width = vertexBufferSize,
                .Height = 1,
                .DepthOrArraySize = 1,
                .MipLevels = 1,
                .Format = DXGI_FORMAT_UNKNOWN,
                .SampleDesc = {.Count = 1, .Quality = 0},
                .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                .Flags = D3D12_RESOURCE_FLAG_NONE,
            };

            // Create upload buffer
            if (HRESULT hr =
                    vertexUploadBuffer.CreateCommitted(device, {.Type = D3D12_HEAP_TYPE_UPLOAD}, D3D12_HEAP_FLAG_NONE,
                                                       vertexBufferDesc, D3D12_RESOURCE_STATE_COMMON);
                FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Failed to create vertex upload buffer, error code {:X}", (uint32)hr)};
            }
            vertexUploadBuffer->SetName(L"[Ymir-GCtx] Vertex upload buffer");

            // Create vertex buffer
            if (HRESULT hr =
                    vertexBuffer.CreateCommitted(device, {.Type = D3D12_HEAP_TYPE_DEFAULT}, D3D12_HEAP_FLAG_NONE,
                                                 vertexBufferDesc, D3D12_RESOURCE_STATE_COMMON);
                FAILED(hr)) {
                return util::ErrorMessage{fmt::format("Failed to create vertex buffer, error code {:X}", (uint32)hr)};
            }
            vertexBuffer->SetName(L"[Ymir-GCtx] Vertex buffer");

            // Copy the quad data to the upload buffer
            UINT8 *pVertexDataBegin;
            D3D12_RANGE readRange(0, 0);
            if (HRESULT hr = vertexUploadBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin));
                FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not map vertex upload buffer, error code {:X}", (uint32)hr)};
            }
            memcpy(pVertexDataBegin, vertices, sizeof(vertices));
            vertexUploadBuffer->Unmap(0, nullptr);

            // Copy the vertex data to the vertex buffer
            if (HRESULT hr = cmdAlloc->Reset(); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Failed to reset command allocator, error code {:X}", (uint32)hr)};
            }
            if (HRESULT hr = cmdListOps->Reset(cmdAlloc.GetPointer(), pipelineState.GetPointer()); FAILED(hr)) {
                return util::ErrorMessage{fmt::format("Failed to reset command list, error code {:X}", (uint32)hr)};
            }

            if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListOps)) {
                // Indicate that the vertex buffer will be used as copy destination
                D3D12_BUFFER_BARRIER barrier{
                    .SyncBefore = D3D12_BARRIER_SYNC_VERTEX_SHADING,
                    .SyncAfter = D3D12_BARRIER_SYNC_COPY,
                    .AccessBefore = D3D12_BARRIER_ACCESS_COMMON,
                    .AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST,
                    .pResource = vertexBuffer.GetPointer(),
                    .Offset = 0,
                    .Size = vertexBufferSize,
                };
                const D3D12_BARRIER_GROUP group{
                    .Type = D3D12_BARRIER_TYPE_BUFFER,
                    .NumBarriers = 1,
                    .pBufferBarriers = &barrier,
                };
                enhCmdList->Barrier(1, &group);

                cmdListOps->CopyBufferRegion(vertexBuffer.GetPointer(), 0, vertexUploadBuffer.GetPointer(), 0,
                                             vertexBufferSize);

                // Indicate that the vertex buffer will be used for generic reads
                std::swap(barrier.SyncBefore, barrier.SyncAfter);
                std::swap(barrier.AccessBefore, barrier.AccessAfter);
                enhCmdList->Barrier(1, &group);
            } else {
                // Indicate that the vertex buffer will be used as copy destination
                D3D12_RESOURCE_BARRIER barrier{
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = vertexBuffer.GetPointer(),
                            .Subresource = 0,
                            .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                            .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                        },
                };
                cmdListOps->ResourceBarrier(1, &barrier);

                cmdListOps->CopyBufferRegion(vertexBuffer.GetPointer(), 0, vertexUploadBuffer.GetPointer(), 0,
                                             vertexBufferSize);

                // Indicate that the vertex buffer will be used for generic reads
                std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                cmdListOps->ResourceBarrier(1, &barrier);
            }

            // Initialize the vertex buffer view
            vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
            vertexBufferView.StrideInBytes = sizeof(Vertex);
            vertexBufferView.SizeInBytes = vertexBufferSize;
        }

        // Create the constant buffer
        {
            const UINT constantBufferSize = sizeof(DrawTextureConstants);

            const D3D12_RESOURCE_DESC constBufferDesc{
                .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                .Width = constantBufferSize,
                .Height = 1,
                .DepthOrArraySize = 1,
                .MipLevels = 1,
                .Format = DXGI_FORMAT_UNKNOWN,
                .SampleDesc = {.Count = 1, .Quality = 0},
                .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                .Flags = D3D12_RESOURCE_FLAG_NONE,
            };
            if (HRESULT hr = constBuffer.CreateCommitted(device, {.Type = D3D12_HEAP_TYPE_UPLOAD}, D3D12_HEAP_FLAG_NONE,
                                                         constBufferDesc, D3D12_RESOURCE_STATE_COMMON);
                FAILED(hr)) {
                return util::ErrorMessage{fmt::format("Failed to create constant buffer, error code {:X}", (uint32)hr)};
            }
            constBuffer->SetName(L"Constant buffer");

            // Describe and create a constant buffer view.
            resourceHeapAlloc.Allocate(cbvQuad.cpuHandle, cbvQuad.gpuHandle, cbvQuad.index);
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
            cbvDesc.BufferLocation = constBuffer->GetGPUVirtualAddress();
            cbvDesc.SizeInBytes = constantBufferSize;
            device->CreateConstantBufferView(&cbvDesc, resourceHeap.GetCPUStart());

            // Map and initialize the constant buffer. We don't unmap this until the context is shut down.
            // Keeping things mapped for the lifetime of the resource is okay.
            D3D12_RANGE readRange(0, 0);
            if (HRESULT hr = constBuffer->Map(0, &readRange, &constBufferPtr); FAILED(hr)) {
                return util::ErrorMessage{fmt::format("Failed to map constant buffer, error code {:X}", (uint32)hr)};
            }
            memcpy(constBufferPtr, &drawTextureConstants, sizeof(drawTextureConstants));
        }

        // Execute command list
        if (HRESULT hr = cmdListOps->Close(); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Failed to close command list, error code {:X}", (uint32)hr)};
        }
        cmdQueue->ExecuteCommandLists(1, cmdListOps.GetAddressOfBase());

        WaitForGPU();

        return {};
    }

    void Shutdown() {
        DeletePendingTextures(true);
        textures.clear();
        for (UINT n = 0; n < kFrameCount; n++) {
            frames[n].renderTarget.Destroy();
            frames[n].cmdAlloc.Destroy();
        }
        fence.Destroy();
        constBuffer.Destroy();
        constBufferPtr = nullptr;
        vertexBuffer.Destroy();
        vertexBufferView.BufferLocation = {};
        pipelineState.Destroy();
        rootSignature.Destroy();
        cmdListFrame.Destroy();
        cmdListOps.Destroy();
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

    util::VoidResult<> ResizeFramebuffer(uint32 width, uint32 height) {
        // Wait for frames to complete and destroy RTVs
        UINT64 currFenceValue = frames[frameIndex].fenceValue;
        for (UINT n = 0; n < kFrameCount; n++) {
            if (FAILED(fence.Signal(cmdQueue, ++currFenceValue))) {
                return util::ErrorMessage{"Failed to signal fence before resizing swapchain buffers"};
            }
            fence.Wait(INFINITE, currFenceValue);
            frames[n].renderTarget.Destroy();
        }

        // Resize swapchain buffers
        if (FAILED(swapchain.ResizeBuffers(width, height))) {
            return util::ErrorMessage{"Failed to resize swapchain buffers"};
        }
        frameIndex = swapchain->GetCurrentBackBufferIndex();

        // Recreate RTVs
        {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap.GetCPUStart());
            for (UINT n = 0; n < kFrameCount; n++) {
                ID3D12Resource *resource;
                if (FAILED(swapchain->GetBuffer(n, IID_PPV_ARGS(&resource)))) {
                    return util::ErrorMessage{fmt::format("Failed to get swapchain buffer {}", n)};
                }
                device->CreateRenderTargetView(resource, nullptr, rtvHandle);
                resource->SetName(fmt::format(L"[Ymir-GCtx] Swapchain buffer #{}", n).c_str());
                frames[n].renderTarget.Attach(resource);
                frames[n].fenceValue = 0;
                rtvHandle.ptr += rtvHeap.GetDescriptorSize();
            }
        }

        // Update current RTV handle
        rtvHandle = rtvHeap.GetCPUStart();
        rtvHandle.ptr += static_cast<SIZE_T>(frameIndex) * rtvHeap.GetDescriptorSize();

        // Update viewport and scissor rects
        viewport.Width = width;
        viewport.Height = height;
        scissorRect.right = width;
        scissorRect.bottom = height;

        return {};
    }

    util::VoidResult<> BeginFrame() {
        D3D12CommandAllocator &cmdAlloc = frames[frameIndex].cmdAlloc;

        if (HRESULT hr = cmdAlloc->Reset(); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Failed to reset frame command allocator, error code {:X}", (uint32)hr)};
        }
        if (HRESULT hr = cmdListFrame->Reset(cmdAlloc.GetPointer(), pipelineState.GetPointer()); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Failed to reset frame command list, error code {:X}", (uint32)hr)};
        }

        ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer(), samplerHeap.GetPointer()};
        cmdListFrame->SetDescriptorHeaps(std::size(heaps), heaps);

        cmdListFrame->SetGraphicsRootSignature(rootSignature.GetPointer());

        cmdListFrame->RSSetViewports(1, &viewport);
        cmdListFrame->RSSetScissorRects(1, &scissorRect);

        // Indicate that the back buffer will be used as a render target
        if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListFrame)) {
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
            enhCmdList->Barrier(1, &group);
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
            cmdListFrame->ResourceBarrier(1, &barrier);
        }

        rtvHandle = rtvHeap.GetCPUStart();
        rtvHandle.ptr += static_cast<SIZE_T>(frameIndex) * rtvHeap.GetDescriptorSize();
        cmdListFrame->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        cmdListFrame->SetGraphicsRootSignature(rootSignature.GetPointer());

        DeletePendingTextures(false);

        return {};
    }

    util::VoidResult<> EndFrame() {
        // Indicate that the back buffer will be used for frame presentation
        if (auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListFrame)) {
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
            enhCmdList->Barrier(1, &group);
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
            cmdListFrame->ResourceBarrier(1, &barrier);
        }

        if (FAILED(cmdListFrame->Close())) {
            return util::ErrorMessage{"Failed to close frame command list"};
        }

        return {};
    }

    util::VoidResult<> Present() {
        ID3D12CommandList *ppCommandLists[] = {cmdListFrame.GetPointer()};
        cmdQueue->ExecuteCommandLists(std::size(ppCommandLists), ppCommandLists);

        // NOTE: VSync and Mailbox both wait for vertical retrace to present a frame. The difference is that enqueuing
        // frames in Mailbox mode replaces the next pending frame while VSync stores and presents all frames. As a
        // result, Mailbox has smaller perceived input lag.
        //
        // The swap chain is created with the DXGI_SWAP_EFFECT_FLIP_DISCARD flag, enabling Mailbox mode. Switching modes
        // involves destroying and recreating the entire swap chain, which doesn't seem to be worth the effort. Instead,
        // we'll treat VSync and Mailbox as the same mode.

        switch (presentMode) {
        default: [[fallthrough]];
        case PresentMode::VSync: swapchain->Present(1, 0); break;
        case PresentMode::Mailbox: swapchain->Present(1, 0); break;
        case PresentMode::Adaptive:
            swapchain->Present(0, swapchain.IsTearingSupported() ? DXGI_PRESENT_ALLOW_TEARING : 0);
            break;
        case PresentMode::NoSync: swapchain->Present(0, 0); break;
        }

        return MoveToNextFrame();
    }

    util::VoidResult<> WaitForGPU() {
        // Schedule a signal command in the queue
        if (FAILED(fence.Signal(cmdQueue, frames[frameIndex].fenceValue))) {
            return util::ErrorMessage{"Failed to signal fence"};
        }

        // Wait until the fence has been processed
        fence.Wait(INFINITE, frames[frameIndex].fenceValue);

        // Increment the fence value for the current frame
        frames[frameIndex].fenceValue++;

        return {};
    }

    util::VoidResult<> MoveToNextFrame() {
        // Schedule a signal command in the queue
        const UINT64 currentFenceValue = frames[frameIndex].fenceValue;
        if (FAILED(fence.Signal(cmdQueue, currentFenceValue))) {
            return util::ErrorMessage{"Failed to signal fence"};
        }

        // Update the frame index
        frameIndex = swapchain->GetCurrentBackBufferIndex();

        // If the next frame is not ready to be rendered yet, wait until it is ready
        if (fence->GetCompletedValue() < frames[frameIndex].fenceValue) {
            fence.Wait(INFINITE, frames[frameIndex].fenceValue);
        }

        // Set the fence value for the next frame
        frames[frameIndex].fenceValue = currentFenceValue + 1;

        return {};
    }

    util::ValueResult<TextureInstance> CreateTexture(const Texture2DSpec &spec) {
        TextureInstance instance;

        {
            auto builder = instance.texture.Texture2DBuilder(spec.width, spec.height);
            builder.Format(ToD3D12Value(spec.format));
            builder.HeapType(D3D12_HEAP_TYPE_DEFAULT);
            builder.InitialState(D3D12_RESOURCE_STATE_COMMON);
            if (spec.access == TextureAccess::RenderTarget) {
                builder.Flags(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            }
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format("Could not create texture, error code {:X}", (uint32)hr)};
            }
            if (!spec.name.empty()) {
                instance.texture->SetName(fmt::format(L"{} texture", StringToWString(spec.name)).c_str());
            }
        }

        D3D12_RESOURCE_DESC desc = instance.texture->GetDesc();
        device->GetCopyableFootprints(&desc, 0, 1, 0, &instance.footprint, &instance.numRows, &instance.rowSizeBytes,
                                      &instance.uploadBufferSize);
        instance.rowPitch = PixelFormatUnitSize(spec.format) * spec.width;

        // In D3D12, textures cannot be directly written to by the CPU - a staging buffer is always needed.
        // Static and Streaming access modes have identical behavior.
        // We store one buffer per frame to enable parallel updates.
        for (int i = 0; i < kFrameCount; ++i) {
            D3D12Resource &buffer = instance.stagingBuffers[i];
            auto builder = buffer.BufferBuilder(instance.uploadBufferSize);
            builder.HeapType(D3D12_HEAP_TYPE_UPLOAD);
            builder.InitialState(D3D12_RESOURCE_STATE_GENERIC_READ);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create texture staging buffer #{}, error code {:X}", i, (uint32)hr)};
            }
            if (HRESULT hr = buffer->Map(0, nullptr, &instance.stagingBuffersData[i]); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not map texture staging buffer #{}, error code {:X}", i, (uint32)hr)};
            }
            if (!spec.name.empty()) {
                buffer->SetName(fmt::format(L"{} staging buffer #{}", StringToWString(spec.name), i).c_str());
            }
        }

        if (!resourceHeapAlloc.Allocate(instance.srvDesc.cpuHandle, instance.srvDesc.gpuHandle,
                                        instance.srvDesc.index)) {
            return util::ErrorMessage{"Could not allocate SRV for texture"};
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(instance.texture.GetPointer(), &srvDesc, instance.srvDesc.cpuHandle);

        instance.spec = spec;

        return instance;
    }

    util::VoidResult<> ResizeTexture(TextureID id, uint32 width, uint32 height) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return util::ErrorMessage{"Texture does not exist"};
        }
        TextureInstance &instance = it->second;

        // First, try creating new texture using the existing texture's specifications
        Texture2DSpec newSpec = instance.spec;
        newSpec.width = width;
        newSpec.height = height;
        auto createResult = CreateTexture(newSpec);
        if (!createResult) {
            return createResult.Error();
        }

        // Now that we've succeeded, mark the previous texture for deletion and replace it
        SubmitTextureForDeletion(instance);
        instance = createResult.Value();

        return {};
    }

    void DestroyTexture(TextureID id) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return;
        }
        auto &instance = it->second;
        SubmitTextureForDeletion(instance);
        textures.erase(it);
    }

    void SubmitTextureForDeletion(TextureInstance &instance) {
        TextureToDelete &texToDelete = texturesToDelete.emplace_back();
        texToDelete.texture = std::move(instance.texture);
        texToDelete.stagingBuffers.swap(instance.stagingBuffers);
        texToDelete.srvIndex = instance.srvDesc.index;
        texToDelete.targetFenceVance = frames[frameIndex].fenceValue + kFrameCount;
    }

    void DeletePendingTextures(bool force) {
        if (texturesToDelete.empty()) {
            return;
        }

        const UINT64 fenceValue = frames[frameIndex].fenceValue;
        while (!texturesToDelete.empty() && (force || texturesToDelete.front().targetFenceVance <= fenceValue)) {
            texturesToDelete.front().Destroy(resourceHeapAlloc);
            texturesToDelete.pop_front();
        }
    }

    bool IsTextureValid(TextureID id) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return false;
        }
        auto &instance = it->second;
        return instance.texture.IsValid();
    }

    TextureInstance *GetTexture(TextureID id) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return nullptr;
        }
        return &it->second;
    }

    util::VoidResult<> UpdateTexture(TextureID id, const IRect *rect,
                                     const std::function<void(void *data, size_t pitch)> &fnUpdate) {
        auto it = textures.find(id);
        if (it == textures.end()) {
            return util::ErrorMessage{"Invalid texture ID"};
        }
        auto &instance = it->second;
        D3D12Resource &texture = instance.texture;
        D3D12Resource &stagingBuffer = instance.stagingBuffers[frameIndex];
        void *stagingBufferData = instance.stagingBuffersData[frameIndex];

        // Copy data to staging buffer
        fnUpdate(stagingBufferData, instance.rowPitch);

        D3D12CommandAllocator &cmdAlloc = frames[frameIndex].cmdAlloc;
        if (HRESULT hr = cmdListOps->Reset(cmdAlloc.GetPointer(), nullptr); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Failed to reset operations command list, error code {:X}", (uint32)hr)};
        }
        auto *enhCmdList = GetCommandListForEnhancedBarriers(cmdListOps);

        // Indicate that data will be copied to the texture
        if (enhCmdList != nullptr) {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_PIXEL_SHADING,
                .SyncAfter = D3D12_BARRIER_SYNC_COPY,
                .AccessBefore = D3D12_BARRIER_ACCESS_COMMON,
                .AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_DEST,
                .pResource = texture.GetPointer(),
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
            enhCmdList->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = texture.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                        .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    },
            };
            cmdListOps->ResourceBarrier(1, &barrier);
        }

        // Copy buffer to texture
        const D3D12_TEXTURE_COPY_LOCATION src{
            .pResource = stagingBuffer.GetPointer(),
            .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            .PlacedFootprint = instance.footprint,
        };
        const D3D12_TEXTURE_COPY_LOCATION dst{
            .pResource = texture.GetPointer(),
            .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
            .SubresourceIndex = 0,
        };
        cmdListOps->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        // Transition texture back to pixel shading usage
        if (enhCmdList != nullptr) {
            D3D12_TEXTURE_BARRIER barrier{
                .SyncBefore = D3D12_BARRIER_SYNC_COPY,
                .SyncAfter = D3D12_BARRIER_SYNC_PIXEL_SHADING,
                .AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST,
                .AccessAfter = D3D12_BARRIER_ACCESS_COMMON,
                .LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST,
                .LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON,
                .pResource = texture.GetPointer(),
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
            enhCmdList->Barrier(1, &group);
        } else {
            D3D12_RESOURCE_BARRIER barrier{
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                .Transition =
                    {
                        .pResource = texture.GetPointer(),
                        .Subresource = 0,
                        .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                        .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    },
            };
            cmdListOps->ResourceBarrier(1, &barrier);
        }

        cmdListOps->Close();
        cmdQueue->ExecuteCommandLists(1, cmdListOps.GetAddressOfBase());

        return {};
    }

    ID3D12GraphicsCommandList7 *GetCommandListForEnhancedBarriers(D3D12GraphicsCommandList &cmdList) const {
        if (!features.enhancedBarriers) {
            return nullptr;
        }
        return cmdList.As7();
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
    if (m_impl->IsInitialized()) {
        m_impl->WaitForGPU();
        ImGuiShutdown();
        m_impl->Shutdown();
    }
}

bool Direct3D12GraphicsContext::IsInitialized() const {
    return m_impl->IsInitialized();
}

util::VoidResult<> Direct3D12GraphicsContext::ResizeFramebuffer(uint32 width, uint32 height) {
    return m_impl->ResizeFramebuffer(width, height);
}

util::VoidResult<> Direct3D12GraphicsContext::BeginFrame() {
    return m_impl->BeginFrame();
}

util::VoidResult<> Direct3D12GraphicsContext::EndFrame() {
    return m_impl->EndFrame();
}

void Direct3D12GraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    const float clearColor[] = {color.r, color.g, color.b, color.a};
    m_impl->cmdListFrame->ClearRenderTargetView(m_impl->rtvHandle, clearColor, 0, nullptr);
}

bool Direct3D12GraphicsContext::ImGuiInit() {
    if (m_imguiInitialized) {
        return true;
    }

    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = m_impl->device.GetPointer();
    initInfo.CommandQueue = m_impl->cmdQueue.GetPointer();
    initInfo.NumFramesInFlight = Impl::kFrameCount;
    initInfo.RTVFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
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
    if (m_imguiInitialized) {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplSDL3_NewFrame();
    }
}

void Direct3D12GraphicsContext::ImGuiRenderFrame() {
    if (m_imguiInitialized) {
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_impl->cmdListFrame.GetPointer());
    }
}

util::ValueResult<TextureID> Direct3D12GraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    auto result = m_impl->CreateTexture(spec);
    if (!result) {
        return result.Error();
    }

    const TextureID id = GetNextTextureID();
    m_impl->textures[id] = std::move(result.Value());

    return id;
}

void Direct3D12GraphicsContext::DestroyTexture(TextureID id) {
    m_impl->DestroyTexture(id);
}

bool Direct3D12GraphicsContext::IsTextureValid(TextureID id) const {
    return m_impl->IsTextureValid(id);
}

ImTextureID Direct3D12GraphicsContext::GetImGuiTextureID(TextureID id) const {
    // ImTextureIDs for D3D12 are the D3D12_GPU_DESCRIPTOR_HANDLE for the texture's SRV
    Impl::TextureInstance *instance = m_impl->GetTexture(id);
    return instance ? instance->srvDesc.gpuHandle.ptr : 0;
}

util::VoidResult<> Direct3D12GraphicsContext::ResizeTexture(TextureID id, uint32 width, uint32 height) {
    return m_impl->ResizeTexture(id, width, height);
}

util::VoidResult<>
Direct3D12GraphicsContext::UpdateTexture(TextureID id, const IRect *rect,
                                         const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    return m_impl->UpdateTexture(id, rect, fnUpdate);
}

util::VoidResult<> Direct3D12GraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                              const FRect &dstRect) {
    // TODO: use TextureFilterMode to select sampler
    // TODO: use the current frame's fence value to enqueue commands
    // TODO: update constant buffer with rect parameters

    // TODO: set render target to dst texture

    // cmdListFrame->SetGraphicsRootDescriptorTable(0, cbvQuad.gpuHandle);
    // cmdListFrame->SetGraphicsRootDescriptorTable(1, <texture SRV GPU handle>);
    // cmdListFrame->SetGraphicsRootDescriptorTable(2, <chosen sampler GPU handle>);
    // cmdListFrame->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // cmdListFrame->IASetVertexBuffers(0, 1, &vertexBufferView);
    // cmdListFrame->DrawInstanced(4, 1, 0, 0);

    // TODO: set render target to swap chain buffer

    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::DrawTextureRotated(TextureID id, const FRect &srcRect,
                                                                 const FRect &dstRect, double rotAngle,
                                                                 const FPoint2D *anchorPoint) {
    // TODO: use TextureFilterMode to select sampler
    // TODO: use the current frame's fence value to enqueue commands
    // TODO: update constant buffer with rect parameters, rotation angle and anchor point

    // TODO: imitate SDL_RenderTextureRotated:
    // - srcRect specifies the source texture region to copy from (in texels)
    // - dstRect specifies the destination texture region to copy to (in texels)
    // - rotAngle is the clockwise rotation angle (in degrees)
    // - anchorPoint is the rotation anchor point. If null, use the center of the destination rectangle
    return util::ErrorMessage{"Unimplemented"};
}

util::VoidResult<> Direct3D12GraphicsContext::SetPresentMode(PresentMode mode) {
    m_impl->presentMode = mode;
    return {};
}

util::VoidResult<> Direct3D12GraphicsContext::Present() {
    return m_impl->Present();
}

wil::com_ptr_nothrow<ID3D12Device> Direct3D12GraphicsContext::GetDevice() const {
    return m_impl->device.GetPointer();
}

} // namespace app::gfx
