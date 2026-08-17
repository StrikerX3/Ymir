#include <ymir/hw/vdp/renderer/vdp_renderer_hw_d3d12.hpp>

#include <ymir/gpu/d3d12/d3d12_commands.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap_allocator.hpp>
#include <ymir/gpu/d3d12/d3d12_device.hpp>
#include <ymir/gpu/d3d12/d3d12_fence.hpp>
#include <ymir/gpu/d3d12/d3d12_pipeline_state.hpp>
#include <ymir/gpu/d3d12/d3d12_resource.hpp>
#include <ymir/gpu/d3d12/d3d12_root_signature.hpp>

#include <ymir/gpu/shaders/gpu_shaders.hpp>

#include <ymir/util/bit_ops.hpp>

#include <ymir/version.hpp> // TODO: remove once Ymir_LOCAL_BUILD blocks are removed

#include <d3d12.h>

#include <fmt/format.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_core_shaders);

using namespace ymir::gpu::d3d12;

namespace ymir::vdp {

/// @brief A simple bump-allocated upload buffer.
struct UploadBuffer {
    /// @brief Upload buffer resource.
    D3D12Resource resource;
    /// @brief Mapped view of the upload buffer.
    void *data = nullptr;
    /// @brief Current offset into the upload buffer.
    size_t offset = 0;
    /// @brief Maximum capacity of the upload buffer.
    size_t capacity = 0;

    ~UploadBuffer() {
        if (resource) {
            resource->Unmap(0, nullptr);
        }
    }

    /// @brief Creates the upload buffer resource and maps its view.
    /// @param[in] device the device that will own the buffer
    /// @param[in] size buffer capacity, force-aligned to 32 bits
    /// @return nothing on success, an error message on failure
    util::VoidResult<> Create(D3D12Device &device, size_t size) {
        size = bit::align<2>(size);
        auto builder = resource.BufferBuilder(size);
        builder.HeapType(D3D12_HEAP_TYPE_UPLOAD);
        builder.InitialState(D3D12_RESOURCE_STATE_GENERIC_READ);
        if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Could not create upload buffer, error code {:X}", (uint32)hr)};
        }

        const D3D12_RANGE range{0, 0};
        if (HRESULT hr = resource->Map(0, &range, &data); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Could not map upload buffer, error code {:X}", (uint32)hr)};
        }
        capacity = size;
        offset = 0;

        return {};
    }

    /// @brief Requests a chunk of the specified size from this buffer.
    /// @param[in] size size in bytes to request, force-aligned to 32 bits
    /// @return a pointer to the chunk, or `nullptr` if the buffer doesn't have enough space
    void *Allocate(size_t size) {
        size = bit::align<2>(size);
        assert((size & 3) == 0);
        if (size <= FreeSpace()) {
            void *ptr = static_cast<char *>(data) + size;
            offset += size;
            return ptr;
        }
        return nullptr;
    }

    /// @brief Resets the allocated pointer, effectively "freeing" all allocations.
    void Reset() {
        offset = 0;
    }

    /// @brief Determines how much free space there is in this buffer.
    /// @return the free space available (in bytes)
    size_t FreeSpace() const {
        return capacity - offset;
    }
};

cmrc::embedded_filesystem g_fsShaders = cmrc::ymir_core_shaders::get_filesystem();

static constexpr const char *kCSEntrypoint = "CSMain";

template <gpu::ShaderStage stage>
D3D12_SHADER_BYTECODE ToShaderBytecode(const gpu::CompiledShader<stage> &shader) {
    return D3D12_SHADER_BYTECODE{
        .pShaderBytecode = shader.bytecode.data(),
        .BytecodeLength = shader.bytecode.size(),
    };
}

// ---------------------------------------------------------------------------------------------------------------------

struct Direct3D12VDPRenderer::Impl {
    D3D12Device device;

    D3D12CommandQueue cmdQueue;
    D3D12Fence fence;
    UINT64 fenceValue;

    D3D12DescriptorHeap resourceHeap;
    DescriptorHeapAllocator resourceHeapAlloc;

    // =================================================================================================================
    // VDP1 rendering
    //
    // TODO

    struct VDP1Resources {
        /// @brief VDP1 command allocator.
        D3D12CommandAllocator cmdAlloc;
        /// @brief VDP1 command list.
        D3D12GraphicsCommandList cmdList;
    } vdp1;

    // =================================================================================================================
    // VDP2 rendering
    //
    // The VDP2 rendering pipeline is invoked at least once per frame. When VRAM, CRAM and/or register writes happen,
    // the renderer processes scanlines up to the previous VCNT and commits all changes before proceeding.
    //
    // Since the VDP2 rendering process has no visible effect on the rest of the Saturn's components, there is no need
    // for additional synchronization constraints on memory and register reads or writes. The VDP2 state is handled by
    // the VDP controller, and the renderer maintains a local independent copy of the VRAM, CRAM and VDP2 registers for
    // fully asynchronous rendering.
    //
    // The 32-bit constant buffer holds renderer parameters shared across all VDP2 compute shaders, including relevant
    // VDP2 registers, active enhancements and the starting line for continuation of work interrupted by state changes.

    struct VDP2Resources {
        /// @brief VDP2 command allocator.
        D3D12CommandAllocator cmdAlloc;
        /// @brief VDP2 command list.
        D3D12GraphicsCommandList cmdList;

        // VDP2 VRAM is exposed as a ByteAddressBuffer to shaders as they often need to access raw bytes in 8-bit,
        // 16-bit and 32-bit formats.

        /// @brief Raw VRAM data buffer.
        D3D12Resource vramBuffer;
        /// @brief Raw VRAM data buffer SRV.
        Descriptor vramSRV;

        // Size of a VDP2 VRAM upload buffer.
        // Tweak size as needed. The value should be large enough to cover most cases while requiring at most one
        // overflow buffer in extreme cases.
        static constexpr UINT64 kVRAMUploadBufferSize = vdp::kVDP2VRAMSize * 4;
        // Worst case for a single transfer is uploading the whole VRAM at once, which happens on initialization, reset,
        // load state, and potentially during VBlank if the game does absolutely nothing but write to VRAM.
        static_assert(kVRAMUploadBufferSize >= vdp::kVDP2VRAMSize);

        /// @brief VDP2 VRAM upload buffer.
        UploadBuffer vramUploadBuffer;
        /// @brief Temporary VDP2 VRAM upload overflow buffers (dynamically allocated if the main buffer is full).
        std::vector<UploadBuffer> vramUploadOverflowBuffers;

        // VDP2 CRAM is not directly exposed. Instead, shaders get two convenient views:
        // - CRAM converted to R8G8B8A8 colors based on the current color RAM mode
        // - Top half of raw CRAM bytes, for rotation coefficients

        /// @brief CRAM color buffer.
        D3D12Resource cramColorBuffer;
        /// @brief CRAM color buffer SRV.
        Descriptor cramColorSRV;

        /// @brief Raw CRAM rotation coefficients buffer.
        D3D12Resource cramRotCoeffBuffer;
        /// @brief Raw CRAM rotation coefficients buffer SRV.
        Descriptor cramRotCoeffSRV;

        // LayerOut contains the intermediate per-layer outputs of the VDP2 rendering process.

        /// @brief 2D texture array for the outputs of NBG0-3, RBG0-1, sprite and mesh layers (in that order).
        D3D12Resource layerOutTexture;
        /// @brief Layer outputs SRV.
        Descriptor layerOutSRV;
        /// @brief Layer outputs UAV.
        Descriptor layerOutUAV;

        // ---------------------------------------------------------------------

        /// @brief Compute shader for drawing background layers.
        gpu::ComputeShader drawBGsShader;
        /// @brief Root signature for drawing background layers.
        D3D12RootSignature drawBGsRootSig;
        /// @brief Pipeline state object for drawing background layers.
        D3D12PipelineState drawBGsPSO;
    } vdp2;

    // =================================================================================================================
    // Operations

    util::VoidResult<> Initialize(ID3D12Device *pDevice) {
        device.Assign(pDevice);

        // Main command queue
        if (HRESULT hr = cmdQueue.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP renderer command queue, error code {:X}", (uint32)hr)};
        }
        cmdQueue->SetName(L"[Ymir-VDP] Command queue");

        // Main fence
        if (HRESULT hr = fence.Create(device, 0, D3D12_FENCE_FLAG_NONE); FAILED(hr)) {
            return util::ErrorMessage{fmt::format("Could not create VDP renderer fence, error code {:X}", (uint32)hr)};
        }
        fence->SetName(L"[Ymir-VDP] Fence");
        fenceValue = 1;

        // Resource heap
        {
            const D3D12_DESCRIPTOR_HEAP_DESC desc{
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                .NumDescriptors = 64,
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
            };
            if (HRESULT hr = resourceHeap.Create(device, desc); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP renderer CBV/SRV/UAV heap, error code {:X}", (uint32)hr)};
            }
            resourceHeap->SetName(L"[Ymir-VDP] CBV/SRV/UAV heap");
            resourceHeapAlloc.Bind(resourceHeap);
        }

        // -------------------------------------------------------------------------------------------------------------

        // VDP1 command allocator and list
        if (HRESULT hr = vdp1.cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP1 renderer command allocator, error code {:X}", (uint32)hr)};
        }
        vdp1.cmdAlloc->SetName(L"[Ymir-VDP1] Command allocator");
        if (HRESULT hr = vdp1.cmdList.Create(device, vdp1.cmdAlloc, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP1 renderer command list, error code {:X}", (uint32)hr)};
        }
        vdp1.cmdList->SetName(L"[Ymir-VDP1] Command list");
        vdp1.cmdList->Close();

        // -------------------------------------------------------------------------------------------------------------

        // VDP2 command allocator and list
        if (HRESULT hr = vdp2.cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP2 renderer command allocator, error code {:X}", (uint32)hr)};
        }
        vdp2.cmdAlloc->SetName(L"[Ymir-VDP2] Command allocator");
        if (HRESULT hr = vdp2.cmdList.Create(device, vdp2.cmdAlloc, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP2 renderer command list, error code {:X}", (uint32)hr)};
        }
        vdp2.cmdList->SetName(L"[Ymir-VDP2] Command list");
        vdp2.cmdList->Close();

        // VDP2 VRAM buffer
        {
            auto builder = vdp2.vramBuffer.BufferBuilder(vdp::kVDP2VRAMSize);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create raw VDP2 VRAM buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.vramBuffer->SetName(L"[Ymir-VDP2] Raw VRAM buffer");

            if (!resourceHeapAlloc.Allocate(vdp2.vramSRV)) {
                return util::ErrorMessage{"Could not allocate raw VDP2 VRAM buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_R32_TYPELESS,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = vdp::kVDP2VRAMSize / sizeof(uint32),
                        .StructureByteStride = 0,
                        .Flags = D3D12_BUFFER_SRV_FLAG_RAW,
                    },
            };
            device->CreateShaderResourceView(vdp2.vramBuffer.GetPointer(), &srvDesc, vdp2.vramSRV.cpuHandle);
        }

        // VDP2 VRAM upload buffer
        {
            if (auto result = vdp2.vramUploadBuffer.Create(device, VDP2Resources::kVRAMUploadBufferSize); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 VRAM upload buffer: {}", result.Error().message)};
            }
            vdp2.vramUploadBuffer.resource->SetName(L"[Ymir-VDP2] VDP2 VRAM upload buffer");
        }

        // VDP2 CRAM color buffer
        {
            // As a reminder, here are the supported color RAM modes and formats:
            //   0 = RGB 5:5:5, 1024 words
            //   1 = RGB 5:5:5, 2048 words
            //   2 = RGB 8:8:8, 1024 words
            //   3 = RGB 8:8:8, 1024 words  (same as mode 2, undocumented)
            static constexpr UINT kMaxNumColors = vdp::kVDP2CRAMSize / sizeof(uint16);

            auto builder = vdp2.cramColorBuffer.BufferBuilder(kMaxNumColors * sizeof(uint32));
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 CRAM color buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.cramColorBuffer->SetName(L"[Ymir-VDP2] CRAM color buffer");

            if (!resourceHeapAlloc.Allocate(vdp2.cramColorSRV)) {
                return util::ErrorMessage{"Could not allocate VDP2 CRAM color buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_R8G8B8A8_UINT,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = kMaxNumColors,
                        .StructureByteStride = 0,
                        .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                    },
            };
            device->CreateShaderResourceView(vdp2.cramColorBuffer.GetPointer(), &srvDesc, vdp2.cramColorSRV.cpuHandle);
        }

        // VDP2 CRAM rotation coefficients buffer
        {
            // The second half of CRAM can be used as rotation coefficients.
            static constexpr UINT kCRAMRotCoeffSize = vdp::kVDP2CRAMSize / 2;

            auto builder = vdp2.cramRotCoeffBuffer.BufferBuilder(kCRAMRotCoeffSize * sizeof(uint32));
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP2 CRAM rotation coefficients buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.cramRotCoeffBuffer->SetName(L"[Ymir-VDP2] CRAM rotation coefficients buffer");

            if (!resourceHeapAlloc.Allocate(vdp2.cramRotCoeffSRV)) {
                return util::ErrorMessage{"Could not allocate VDP2 CRAM rotation coefficients buffer SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_R32_TYPELESS,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = kCRAMRotCoeffSize,
                        .StructureByteStride = 0,
                        .Flags = D3D12_BUFFER_SRV_FLAG_RAW,
                    },
            };
            device->CreateShaderResourceView(vdp2.cramRotCoeffBuffer.GetPointer(), &srvDesc,
                                             vdp2.cramRotCoeffSRV.cpuHandle);
        }

        // Layer outputs 2D texture array
        {
            // The array contains:
            //   [0..3] NBG0-3
            //   [4..5] RBG0-1
            //      [6] Sprite
            //      [7] Transparent meshes
            // The alpha channel is used for pixel attributes:
            //   [0..2] Priority
            //      [3] (Sprite only) Color MSB
            //      [4] (Sprite only) Shadow/window flag - sprite data SD = 1
            //      [5] (Sprite only) Normal shadow flag - sprite data DC = ...111110
            //      [6] Special color calculation flag
            //      [7] Transparent flag (0=opaque, 1=transparent)
            static constexpr UINT16 kNumLayers = 4 + 2 + 1 + 1;
            static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UINT;

            auto builder = vdp2.layerOutTexture.Texture2DBuilder(vdp::kMaxResH, vdp::kMaxResV, kNumLayers);
            builder.Format(kFormat);
            builder.Flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create layer outputs texture array, error code {:X}", (uint32)hr)};
            }
            vdp2.layerOutTexture->SetName(L"[Ymir-VDP2] Layer outputs array");

            if (!resourceHeapAlloc.Allocate(vdp2.layerOutSRV)) {
                return util::ErrorMessage{"Could not allocate layer outputs texture array SRV"};
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2DArray =
                    {
                        .MostDetailedMip = 0,
                        .MipLevels = 1,
                        .FirstArraySlice = 0,
                        .ArraySize = kNumLayers,
                        .PlaneSlice = 0,
                        .ResourceMinLODClamp = 0.0f,
                    },
            };
            device->CreateShaderResourceView(vdp2.layerOutTexture.GetPointer(), &srvDesc, vdp2.layerOutSRV.cpuHandle);

            if (!resourceHeapAlloc.Allocate(vdp2.layerOutUAV)) {
                return util::ErrorMessage{"Could not allocate layer outputs texture array UAV"};
            }
            const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
                .Format = kFormat,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
                .Texture2DArray =
                    {
                        .MipSlice = 0,
                        .FirstArraySlice = 0,
                        .ArraySize = kNumLayers,
                        .PlaneSlice = 0,
                    },
            };
            device->CreateUnorderedAccessView(vdp2.layerOutTexture.GetPointer(), nullptr, &uavDesc,
                                              vdp2.layerOutUAV.cpuHandle);
        }

        // Draw background layers compute shader, root signature and pipeline state object
        {
            auto shaderBlobResult = LoadShader("src/vdp/vdp2_render_bgs_cs.cso");
            if (!shaderBlobResult) {
                return util::ErrorMessage{
                    fmt::format("Could not load VDP2 background layer rendering compute shader: {}",
                                shaderBlobResult.Error().message)};
            }
            vdp2.drawBGsShader.format = gpu::ShaderBytecodeFormat::DXIL;
            vdp2.drawBGsShader.bytecode = shaderBlobResult.Value();
            vdp2.drawBGsShader.entrypoint = kCSEntrypoint;
            auto result = gpu::ValidateShader(vdp2.drawBGsShader);
            if (!result) {
                return util::ErrorMessage{fmt::format(
                    "VDP2 background layer rendering compute shader validation failed: {}", result.Error().message)};
            }

            auto rootSigBuilder = vdp2.drawBGsRootSig.Builder();
            // TODO: add parameters as needed
            rootSigBuilder.Add32BitConstants(0, 1); // TODO: rendering parameters
            rootSigBuilder.AddDescriptorTable().AddUAVs(1, 0);
            if (HRESULT hr = rootSigBuilder.Build(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not build VDP2 background layer rendering root signature, error code {:X}", (uint32)hr)};
            }
            vdp2.drawBGsRootSig->SetName(L"[Ymir-VDP2] Background layer rendering root signature");

            const D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
                .pRootSignature = vdp2.drawBGsRootSig.GetPointer(),
                .CS = ToShaderBytecode(vdp2.drawBGsShader),
            };
            if (HRESULT hr = vdp2.drawBGsPSO.CreateCompute(device, psoDesc); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not build VDP2 background layer rendering pipeline state object, error code {:X}",
                    (uint32)hr)};
            }
            vdp2.drawBGsPSO->SetName(L"[Ymir-VDP2] Background layer rendering pipeline state object");
        }

        // TODO: upload full VDP1 and VDP2 states

#ifdef Ymir_LOCAL_BUILD // Allow initialization to succeed so we can develop this stuff
        return {};
#else
        return util::ErrorMessage{"Unimplemented"};
#endif
    }

    util::ValueResult<std::vector<char>> LoadShader(const char *path) {
        if (!g_fsShaders.is_file(path)) {
            return util::ErrorMessage{fmt::format("Embedded file not found: {}", path)};
        }
        auto file = g_fsShaders.open(path);
        return std::vector<char>{file.begin(), file.end()};
    }

    /// @brief Locates an upload buffer with enough free space to hold data of the specified size.
    /// Search is done in reverse, with the most recent overflow buffers queried first and the primary buffer queried
    /// last, which should minimize time spent searching for free space.
    /// Returns `nullptr` if none of the buffers have enough space.
    /// @param[in] primary the primary upload buffer
    /// @param[in] overflow overflow buffers, if any
    /// @param[in] size the requested size, force-aligned to 32 bits
    /// @return a pointer to an upload buffer with enough room for the requested size, `nullptr` otherwise
    UploadBuffer *FindUploadBuffer(UploadBuffer &primary, std::span<UploadBuffer> overflow, size_t size) {
        size = bit::align<2>(size);
        for (auto it = overflow.rbegin(); it != overflow.rend(); ++it) {
            if (it->FreeSpace() >= size) {
                return &*it;
            }
        }
        if (primary.FreeSpace() >= size) {
            return &primary;
        }
        return nullptr; // NOTE: if we're consistently hitting this case, consider increasing the buffer size
    }

    // TODO: mark VRAM words (or chunks) as dirty on writes
    // - coalesce dirty regions
    //   - also merge dirty regions spaced out by a few words
    // TODO: implement VDP2FlushVRAM():
    // - iterate over dirty regions
    // - find upload buffer with enough free space
    //   - if all buffers are full, allocate and map additional buffer
    // - copy regions to upload buffer
    // - submit CopyBufferRegion commands from upload to VRAM buffer
};

// ---------------------------------------------------------------------------------------------------------------------

Direct3D12VDPRenderer::Direct3D12VDPRenderer(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                                             const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig,
                                             core::Configuration::HardwareRenderer &hwRenderConfig)
    : HardwareVDPRendererBase(VDPRendererType::Direct3D12, hwRenderConfig)
    , m_impl(std::make_unique<Impl>())
    , m_vdp2DebugRenderOptions(vdp2DebugRenderOptions)
    , m_vdp2AccessPatternsConfig(vdp2AccessPatternsConfig)
    , m_hwRenderConfig(hwRenderConfig) {}

Direct3D12VDPRenderer::~Direct3D12VDPRenderer() = default;

util::VoidResult<> Direct3D12VDPRenderer::Initialize(ID3D12Device *device) {
    return m_impl->Initialize(device);
}

util::ObjectResult<Direct3D12VDPRenderer>
Direct3D12VDPRenderer::Create(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                              const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig,
                              core::Configuration::HardwareRenderer &hwRenderConfig, ID3D12Device *device) {
    if (device == nullptr) {
        return util::ErrorMessage{"No Direct3D 12 device instance provided"};
    }
    std::unique_ptr<Direct3D12VDPRenderer> renderer{
        new Direct3D12VDPRenderer(state, vdp2DebugRenderOptions, vdp2AccessPatternsConfig, hwRenderConfig)};
    util::VoidResult<> result = renderer->Initialize(device);
    if (!result) {
        return result.Error();
    }
    return renderer;
}

// -----------------------------------------------------------------------------
// Configuration

bool Direct3D12VDPRenderer::IsValid() const {
    return true;
}

void Direct3D12VDPRenderer::Reset(bool hard) {
    // TODO: reset to initial state (clear VRAM, reset registers, etc.)
}

// -----------------------------------------------------------------------------
// Save states

void Direct3D12VDPRenderer::PreSaveStateSync() {}

void Direct3D12VDPRenderer::PostLoadStateSync() {
    // TODO: sync
}

void Direct3D12VDPRenderer::SaveState(savestate::VDPSaveState::VDPRendererSaveState &state) {}

bool Direct3D12VDPRenderer::ValidateState(const savestate::VDPSaveState::VDPRendererSaveState &state) const {
    return true;
}

void Direct3D12VDPRenderer::LoadState(const savestate::VDPSaveState::VDPRendererSaveState &state) {}

// -----------------------------------------------------------------------------
// VDP1 memory and register writes

void Direct3D12VDPRenderer::VDP1WriteVRAM(uint32 address, uint8 value) {
    // TODO: mark as dirty
}

void Direct3D12VDPRenderer::VDP1WriteVRAM(uint32 address, uint16 value) {
    // TODO: mark as dirty
}

void Direct3D12VDPRenderer::VDP1SyncFB() {
    // TODO: wait until VDP1 rendering has caught up
}

void Direct3D12VDPRenderer::VDP1DebugSyncFB() {
    // TODO: loosely wait until VDP1 rendering has caught up, maybe
}

void Direct3D12VDPRenderer::VDP1WriteFB(uint32 address, uint8 value) {
    // TODO: mark as dirty
}

void Direct3D12VDPRenderer::VDP1WriteFB(uint32 address, uint16 value) {
    // TODO: mark as dirty
}

void Direct3D12VDPRenderer::VDP1WriteReg(uint32 address, uint16 value) {
    // TODO: mark as dirty
}

// -------------------------------------------------------------------------
// VDP2 memory and register writes

void Direct3D12VDPRenderer::VDP2WriteVRAM(uint32 address, uint8 value) {
    // TODO: mark as dirty
}

void Direct3D12VDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {
    // TODO: mark as dirty
}

void Direct3D12VDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {
    // TODO: mark as dirty; cache converted colors
}

void Direct3D12VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {
    // TODO: mark as dirty; cache converted colors
}

void Direct3D12VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    // TODO: mark as dirty depending on register; recompute cached CRAM colors if needed
}

// -----------------------------------------------------------------------------
// Debugger

void Direct3D12VDPRenderer::UpdateEnabledLayers() {
    // TODO: VDP2UpdateEnabledBGs(); --> redirect to m_state.state2.UpdateEnabledBGs(...)
}

// -----------------------------------------------------------------------------
// Utilities

void Direct3D12VDPRenderer::DumpExtraVDP1Framebuffers(std::ostream &out) const {
    // TODO: pause the world, download mesh buffers, copy to output
}

// -----------------------------------------------------------------------------
// Rendering process

void Direct3D12VDPRenderer::VDP1EraseFramebuffer(uint64 cycles) {
    // TODO: execute operation
}

void Direct3D12VDPRenderer::VDP1SwapFramebuffer() {
    // TODO: execute operation
    Callbacks.VDP1FramebufferSwap();
}

void Direct3D12VDPRenderer::VDP1BeginFrame() {
    // TODO: prepare new VDP1 frame
}

void Direct3D12VDPRenderer::VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
    // TODO: execute operation
}

void Direct3D12VDPRenderer::VDP1EndFrame() {
    // TODO: finish VDP1 frame
    Callbacks.VDP1DrawFinished();
}

void Direct3D12VDPRenderer::VDP2SetResolution(uint32 h, uint32 v, bool exclusive) {
    Callbacks.VDP2ResolutionChanged(h, v);
}

void Direct3D12VDPRenderer::VDP2SetField(bool odd) {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D12VDPRenderer::VDP2LatchTVMD() {
    // Nothing to do. We're using the main VDP2 state for this.
}

// TODO: rendering process
// - VDP2BeginFrame():
//   - VDP2FlushVRAM() right away
// - VDP2RenderLine():
//   - if there are pending VRAM writes:
//     - draw lines from segmentStartY to y-1 (if possible)
//     - VDP2FlushVRAM()
//     - set segmentStartY = y
// - VDP2EndFrame():
//   - draw lines from segmentStartY to bottom of framebuffer

void Direct3D12VDPRenderer::VDP2BeginFrame() {
    // TODO: prepare new VDP2 frame
    auto &vdp2 = m_impl->vdp2;
    auto &cmdList = vdp2.cmdList;
    vdp2.cmdAlloc->Reset();
    cmdList->Reset(vdp2.cmdAlloc.GetPointer(), vdp2.drawBGsPSO.GetPointer());

    ID3D12DescriptorHeap *heaps[] = {m_impl->resourceHeap.GetPointer()};
    cmdList->SetDescriptorHeaps(std::size(heaps), heaps);

    cmdList->SetComputeRootSignature(vdp2.drawBGsRootSig.GetPointer());
    // TODO: cmdList->SetComputeRoot32BitConstants(0, sizeof(RenderParams), &vdp2.cpuRenderParams, 0);
    cmdList->SetComputeRootDescriptorTable(1, vdp2.layerOutUAV.gpuHandle);
}

void Direct3D12VDPRenderer::VDP2RenderLine(uint32 y) {
    // TODO: prepare next line, render and compose lines; optimize by batching lines without state changes
}

void Direct3D12VDPRenderer::VDP2EndFrame() {
    // TODO: this is just for testing; remove it
    auto &vdp2 = m_impl->vdp2;
    auto &cmdList = vdp2.cmdList;
    cmdList->Dispatch(vdp::kMaxResH / 32, vdp::kMaxResV, 6);
    cmdList->Close();

    m_impl->cmdQueue->ExecuteCommandLists(1, cmdList.GetAddressOfBase());
    m_impl->fence.Signal(m_impl->cmdQueue, m_impl->fenceValue);
    m_impl->fence.Wait(INFINITE, m_impl->fenceValue);
    ++m_impl->fenceValue;

    // TODO: finish VDP2 frame
    Callbacks.VDP2DrawFinished();
}

} // namespace ymir::vdp
