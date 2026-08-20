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
#include <ymir/util/dirty_bitmap.hpp>

#include <ymir/version.hpp> // TODO: remove once Ymir_LOCAL_BUILD blocks are removed

#include <d3d12.h>

#include <fmt/format.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_core_shaders);

#include <concepts>
#include <unordered_map>
#include <vector>

using namespace ymir::gpu::d3d12;
using namespace ymir::core::config::hw_vdp;

namespace ymir::vdp {

// ---------------------------------------------------------------------------------------------------------------------
// TODO: most of these are likely to be shared across all backends. Move to a shared header.

/// @brief Name of the entrypoint function for all compute shaders.
static constexpr const char *kCSEntrypoint = "CSMain";

/// @brief Contains the compiled shader files from res/shaders/src.
cmrc::embedded_filesystem g_fsShaders = cmrc::ymir_core_shaders::get_filesystem();

struct ColorR8G8B8A8 {
    uint8 r, g, b, a;
};
static_assert(sizeof(ColorR8G8B8A8) == sizeof(uint32));

// Base Xst, Yst, KA for params A and B relative to startY
struct alignas(16) VDP2RotParamBase {
    uint32 tableAddress;
    sint32 Xst, Yst;
    uint32 KA;
};
static_assert(sizeof(VDP2RotParamBase) == sizeof(uint32) * 4);

// ---------------------------------------------------------------------------------------------------------------------
// TODO: these could be useful in multiple backends. Make them generic and reusable, and move to a shared header.

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
    /// @param[out] outOffset the offset of the allocated data chunk in the buffer
    /// @param[out] outPtr pointer to the allocated data
    /// @return `true` if allocated, `false` if there is not enough space
    bool Allocate(size_t size, size_t &outOffset, void *&outPtr) {
        size = bit::align<2>(size);
        if (size <= FreeSpace()) {
            outOffset = offset;
            outPtr = static_cast<char *>(data) + offset;
            offset += size;
            return true;
        }
        return false;
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

// ---------------------------------------------------------------------------------------------------------------------

/// @brief Converts the given shader into a `D3D12_SHADER_BYTECODE` structure.
/// @tparam stage the shader stage
/// @param[in] shader the compiler shader
/// @return a `D3D12_SHADER_BYTECODE` with a reference to the shader's bytecode
template <gpu::ShaderStage stage>
D3D12_SHADER_BYTECODE ToShaderBytecode(const gpu::CompiledShader<stage> &shader) {
    return D3D12_SHADER_BYTECODE{
        .pShaderBytecode = shader.bytecode.data(),
        .BytecodeLength = shader.bytecode.size(),
    };
}

// ---------------------------------------------------------------------------------------------------------------------

struct Direct3D12VDPRenderer::Impl {
    Impl(VDPState &state, const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig,
         const config::VDP2DebugRender &vdp2DebugRenderOptions, core::Configuration::HardwareRenderer &hwRenderConfig)
        : vdpState(state)
        , hwRenderConfig(hwRenderConfig)
        , vdp2(vdp2AccessPatternsConfig, vdp2DebugRenderOptions) {}

    VDPState &vdpState;
    core::Configuration::HardwareRenderer &hwRenderConfig;

    D3D12Device device;

    struct Features {
        bool enhancedBarriers = false;
    } features;

    D3D12CommandQueue cmdQueue;
    D3D12Fence fence;

    D3D12DescriptorHeap resourceHeap;
    DescriptorHeapAllocator resourceHeapAlloc;

    /// @brief Resources for a single frame.
    struct FrameContext {
        D3D12CommandAllocator cmdAlloc;
        UINT64 fenceValue = 1;
    };

    /// @brief Ring buffer of frame resources.
    /// @tparam count number of frames
    /// @tparam TFrameContext frame context type. Extend FrameContext to store additional per-frame resources
    template <size_t count, typename TFrameContext = FrameContext>
        requires std::derived_from<TFrameContext, FrameContext>
    struct FrameSet {
        std::array<TFrameContext, count> frames;
        size_t frameIndex = 0;

        TFrameContext &GetCurrentFrame() {
            return frames[frameIndex];
        }
        const TFrameContext &GetCurrentFrame() const {
            return frames[frameIndex];
        }

        util::VoidResult<> MoveToNextFrame(D3D12Fence &fence, D3D12CommandQueue &cmdQueue) {
            // Schedule a signal command in the queue
            const FrameContext &currFrame = GetCurrentFrame();
            const UINT64 currentFenceValue = currFrame.fenceValue;
            if (FAILED(fence.Signal(cmdQueue, currentFenceValue))) {
                return util::ErrorMessage{"Failed to signal fence"};
            }

            // Update the frame index
            ++frameIndex;
            if (frameIndex >= count) {
                frameIndex = 0;
            }

            // Wait for next frame
            FrameContext &nextFrame = GetCurrentFrame();
            if (fence->GetCompletedValue() < nextFrame.fenceValue) {
                fence.Wait(INFINITE, nextFrame.fenceValue);
            }

            // Set the fence value for the next frame
            nextFrame.fenceValue = currentFenceValue + 1;

            return {};
        }

        util::VoidResult<> WaitForGPU(D3D12Fence &fence, D3D12CommandQueue &cmdQueue) {
            FrameContext &currFrame = GetCurrentFrame();

            // Schedule a signal command in the queue
            if (FAILED(fence.Signal(cmdQueue, currFrame.fenceValue))) {
                return util::ErrorMessage{"Failed to signal fence"};
            }

            // Wait until the fence has been processed
            fence.Wait(INFINITE, currFrame.fenceValue);

            // Increment the fence value for the current frame
            ++currFrame.fenceValue;

            return {};
        }

        TFrameContext &operator[](size_t index) {
            return frames[index];
        }
        const TFrameContext &operator[](size_t index) const {
            return frames[index];
        }

        constexpr size_t Count() const {
            return count;
        }
    };

    // =================================================================================================================
    // VDP1 rendering
    //
    // TODO

    struct VDP1FrameContext : public FrameContext {
        // TODO
    };

    struct VDP1Resources {
        /// @brief VDP1 per-frame resources.
        FrameSet<4, VDP1FrameContext> frames;

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
    // Root 32-bit constants hold renderer parameters shared across all VDP2 compute shaders such as the starting line
    // for continuation of work interrupted by state changes, relevant registers and active enhancements.

    /// @brief Common VDP2 rendering parameters shared by all shaders.
    struct VDP2CommonRenderParams {
        // Top Y coordinate of target rendering area.
        uint32 startY;

        // TODO: bit-pack more common parameters to minimize DWORDs spent with this
    };

    /// @brief VDP2 NBG/RBG layer rendering parameters.
    struct VDP2BGLayerParams {
        // TODO: add layer parameters
        // - no need to bit-pack, they're passed in as structured buffers
    };

    /// @brief VDP2 compositor parameters.
    struct VDP2ComposeParams {
        // TODO: add compositor parameters
        // - no need to bit-pack, they're passed in as structured buffers
    };

    /// @brief Size of a VDP2 VRAM upload buffer.
    static constexpr UINT64 kVDP2VRAMUploadBufferSize = vdp::kVDP2VRAMSize * 4;
    // Note to developers: tweak size as needed. The value should be large enough to cover most cases while requiring at
    // most one overflow buffer in extreme cases. Worst case for a single transfer is uploading the whole VRAM at once,
    // which happens on initialization, reset, load state, and potentially during VBlank if the game does absolutely
    // nothing but write to VRAM.
    static_assert(kVDP2VRAMUploadBufferSize >= vdp::kVDP2VRAMSize);

    /// @brief Number of entries in the VDP2 CRAM color cache.
    /// VDP2 CRAM can have at most 2048 colors (in mode 1 - RGB 5:5:5 with access to full CRAM).
    static constexpr size_t kVDP2CRAMColorCacheEntries = vdp::kVDP2CRAMSize / sizeof(uint16);
    /// @brief VDP2 CRAM converted color cache array.
    using CRAMColorCache = std::array<ColorR8G8B8A8, kVDP2CRAMColorCacheEntries>;
    /// @brief Size of the VDP2 CRAM color buffer, in bytes.
    static constexpr UINT kVDP2CRAMColorBufferSize = sizeof(CRAMColorCache);

    /// @brief Size of the VDP2 CRAM rotation coefficients buffer, in bytes.
    /// The second half of CRAM can be used for that purpose.
    static constexpr UINT kVDP2CRAMRotCoeffSize = vdp::kVDP2CRAMSize / 2;

    struct VDP2FrameContext : public FrameContext {
        /// @brief VDP2 VRAM upload buffer.
        UploadBuffer vramUploadBuffer;
        /// @brief Temporary VDP2 VRAM upload overflow buffers (dynamically allocated if the main buffer is full).
        std::vector<UploadBuffer> vramUploadOverflowBuffers;

        /// @brief CRAM color upload buffer.
        UploadBuffer cramColorUploadBuffer;
        /// @brief Raw CRAM rotation coefficients upload buffer.
        UploadBuffer cramRotCoeffUploadBuffer;

        void Reset() {
            cmdAlloc->Reset();
            vramUploadBuffer.Reset();
            for (UploadBuffer &buffer : vramUploadOverflowBuffers) {
                buffer.Reset();
            }
            cramColorUploadBuffer.Reset();
            cramRotCoeffUploadBuffer.Reset();
        }
    };

    struct VDP2Resources {
        VDP2Resources(const config::VDP2AccessPatternsConfig &accessPatternsConfig,
                      const config::VDP2DebugRender &debugRenderOptions)
            : accessPatternsConfig(accessPatternsConfig)
            , debugRenderOptions(debugRenderOptions) {}

        /// @brief VDP2 per-frame resources.
        FrameSet<4, VDP2FrameContext> frames;

        /// @brief VDP2 command list.
        D3D12GraphicsCommandList cmdList;

        // VDP2 VRAM is exposed as a ByteAddressBuffer to shaders as they often need to access raw bytes in 8-bit,
        // 16-bit and 32-bit formats.

        /// @brief VRAM data buffer.
        D3D12Resource vramBuffer;
        /// @brief VRAM data buffer SRV.
        Descriptor vramSRV;

        /// @brief Bit shift for the granularity for VRAM dirty bitmap chunks.
        static constexpr size_t kVRAMDirtyBitmapChunkSizeShift = 8;
        /// @brief Granularity for VRAM dirty bitmap chunks, in bytes.
        static constexpr size_t kVRAMDirtyBitmapChunkSize = static_cast<size_t>(1) << kVRAMDirtyBitmapChunkSizeShift;
        /// @brief Number of bits in the VRAM dirty bitmap.
        static constexpr size_t kVRAMDirtyBitmapSize = vdp::kVDP2VRAMSize / kVRAMDirtyBitmapChunkSize;
        // D3D12 buffer transfers must be done in multiples of 4 bytes.
        // The chunk must not be larger than VDP2 VRAM itself. In fact, it shouldn't be too large as it wastes memory
        // and time with unnecessary copies of VRAM data.
        static_assert(kVRAMDirtyBitmapChunkSize >= sizeof(uint32) && kVRAMDirtyBitmapChunkSize <= vdp::kVDP2VRAMSize,
                      "VDP2 VRAM upload chunk size is out of range");

        /// @brief VDP2 VRAM dirty bitmap.
        util::DirtyBitmap<kVRAMDirtyBitmapSize> vramDirty;

        // VDP2 CRAM is not directly exposed. Instead, shaders get two convenient views:
        // - CRAM converted to R8G8B8A8 colors based on the current color RAM mode
        // - Top half of raw CRAM bytes, for rotation coefficients

        /// @brief CRAM color buffer.
        D3D12Resource cramColorBuffer;
        /// @brief CRAM color buffer SRV.
        Descriptor cramColorSRV;
        /// @brief CPU-side CRAM color buffer.
        CRAMColorCache cpuCRAMColorCache;

        /// @brief Raw CRAM rotation coefficients buffer.
        D3D12Resource cramRotCoeffBuffer;
        /// @brief Raw CRAM rotation coefficients buffer SRV.
        Descriptor cramRotCoeffSRV;

        /// @brief VDP2 CRAM dirty flag.
        bool cramDirty;

        // LayerOut contains the intermediate per-layer outputs of the VDP2 rendering process.

        /// @brief 2D texture array for the outputs of NBG0-3, RBG0-1, sprite and mesh layers (in that order).
        D3D12Resource layerOutTexture;
        /// @brief Layer outputs SRV.
        Descriptor layerOutSRV;
        /// @brief Layer outputs UAV.
        Descriptor layerOutUAV;

        // TODO: LNCL/BACK screen texture + upload buffer

        /// @brief CPU-side LNCL/BACK screen texture (0,y=LNCL; 1,y=BACK).
        std::array<std::array<ColorR8G8B8A8, 2>, kMaxResV> cpuLineColors;

        // TODO: Rotation parameter base values structured buffer array (2 entries, A and B)

        /// @brief CPU-side VDP2 rotation parameter base values.
        std::array<VDP2RotParamBase, kMaxNormalResV * 2> cpuRotParamBases;

        // ---------------------------------------------------------------------

        /// @brief Common rendering parameters.
        VDP2CommonRenderParams cpuCommonRenderParams;

        // ---------------------------------------------------------------------

        /// @brief Compute shader for drawing background layers.
        gpu::ComputeShader drawBGsShader;
        /// @brief Root signature for drawing background layers.
        D3D12RootSignature drawBGsRootSig;
        /// @brief Pipeline state object for drawing background layers.
        D3D12PipelineState drawBGsPSO;

        // ---------------------------------------------------------------------
        // Rendering state

        uint32 nextBGLine = 0;
        uint32 nextComposeLine = 0;

        bool rotRegsDirty = false;
        bool bgRenderParamsDirty = false;
        bool composeParamsDirty = false;

        const config::VDP2AccessPatternsConfig &accessPatternsConfig;
        const config::VDP2DebugRender &debugRenderOptions;
    } vdp2;

    // =================================================================================================================
    // Operations

    util::VoidResult<> Initialize(ID3D12Device *pDevice) {
        device.Assign(pDevice);

        // Check features
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12)))) {
            features.enhancedBarriers = options12.EnhancedBarriersSupported;
        } else {
            features.enhancedBarriers = false;
        }

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

        // VDP1 command allocators and list
        for (int i = 0; i < vdp1.frames.Count(); ++i) {
            FrameContext &frame = vdp1.frames[i];
            if (HRESULT hr = frame.cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP1 renderer command allocator #{}, error code {:X}", i, (uint32)hr)};
            }
            frame.cmdAlloc->SetName(fmt::format(L"[Ymir-VDP1] Command allocator #{}", i).c_str());
        }
        if (HRESULT hr =
                vdp1.cmdList.Create(device, vdp1.frames.GetCurrentFrame().cmdAlloc, D3D12_COMMAND_LIST_TYPE_COMPUTE);
            FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP1 renderer command list, error code {:X}", (uint32)hr)};
        }
        vdp1.cmdList->SetName(L"[Ymir-VDP1] Command list");

        // -------------------------------------------------------------------------------------------------------------

        // VDP2 command allocators and list
        for (int i = 0; i < vdp2.frames.Count(); ++i) {
            FrameContext &frame = vdp2.frames[i];
            if (HRESULT hr = frame.cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP2 renderer command allocator #{}, error code {:X}", i, (uint32)hr)};
            }
            frame.cmdAlloc->SetName(fmt::format(L"[Ymir-VDP2] Command allocator #{}", i).c_str());
        }
        if (HRESULT hr =
                vdp2.cmdList.Create(device, vdp2.frames.GetCurrentFrame().cmdAlloc, D3D12_COMMAND_LIST_TYPE_COMPUTE);
            FAILED(hr)) {
            return util::ErrorMessage{
                fmt::format("Could not create VDP2 renderer command list, error code {:X}", (uint32)hr)};
        }
        vdp2.cmdList->SetName(L"[Ymir-VDP2] Command list");

        // VDP2 VRAM buffer
        {
            auto builder = vdp2.vramBuffer.BufferBuilder(vdp::kVDP2VRAMSize);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 VRAM buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.vramBuffer->SetName(L"[Ymir-VDP2] VRAM buffer");

            if (!resourceHeapAlloc.Allocate(vdp2.vramSRV)) {
                return util::ErrorMessage{"Could not allocate VDP2 VRAM buffer SRV"};
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

        // VDP2 VRAM upload buffers
        for (int i = 0; i < vdp2.frames.Count(); ++i) {
            VDP2FrameContext &frame = vdp2.frames[i];
            if (auto result = frame.vramUploadBuffer.Create(device, kVDP2VRAMUploadBufferSize); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 VRAM upload buffer #{}: {}", i, result.Error().message)};
            }
            frame.vramUploadBuffer.resource->SetName(fmt::format(L"[Ymir-VDP2] VRAM upload buffer #{}", i).c_str());
        }

        // VDP2 CRAM color buffer
        {
            auto builder = vdp2.cramColorBuffer.BufferBuilder(kVDP2CRAMColorBufferSize);
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
                        .NumElements = kVDP2CRAMColorBufferSize / sizeof(uint32),
                        .StructureByteStride = 0,
                        .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                    },
            };
            device->CreateShaderResourceView(vdp2.cramColorBuffer.GetPointer(), &srvDesc, vdp2.cramColorSRV.cpuHandle);
        }

        // VDP2 CRAM rotation coefficients buffer
        {

            auto builder = vdp2.cramRotCoeffBuffer.BufferBuilder(kVDP2CRAMRotCoeffSize);
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
                        .NumElements = kVDP2CRAMRotCoeffSize / sizeof(uint32),
                        .StructureByteStride = 0,
                        .Flags = D3D12_BUFFER_SRV_FLAG_RAW,
                    },
            };
            device->CreateShaderResourceView(vdp2.cramRotCoeffBuffer.GetPointer(), &srvDesc,
                                             vdp2.cramRotCoeffSRV.cpuHandle);
        }

        // VDP2 CRAM color and rotation coefficients upload buffers
        for (int i = 0; i < vdp2.frames.Count(); ++i) {
            VDP2FrameContext &frame = vdp2.frames[i];
            if (auto result = frame.cramColorUploadBuffer.Create(device, kVDP2CRAMColorBufferSize * 256); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 CRAM color upload buffer #{}: {}", i, result.Error().message)};
            }
            frame.cramColorUploadBuffer.resource->SetName(
                fmt::format(L"[Ymir-VDP2] CRAM color upload buffer #{}", i).c_str());

            if (auto result = frame.cramRotCoeffUploadBuffer.Create(device, kVDP2CRAMRotCoeffSize * 256); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 CRAM rotation coefficients upload buffer #{}: {}", i,
                                result.Error().message)};
            }
            frame.cramRotCoeffUploadBuffer.resource->SetName(
                fmt::format(L"[Ymir-VDP2] CRAM rotation coefficients upload buffer #{}", i).c_str());
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
            rootSigBuilder.Add32BitConstants(0, sizeof(VDP2CommonRenderParams) / sizeof(uint32));
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

        Reset();

        {
            ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer()};
            vdp2.cmdList->SetDescriptorHeaps(std::size(heaps), heaps);
        }

        // TODO: upload full VDP1 and VDP2 states

#ifdef Ymir_LOCAL_BUILD // Allow initialization to succeed so we can develop this stuff
        return {};
#else
        return util::ErrorMessage{"Unimplemented"};
#endif
    }

    void Shutdown() {
        vdp2.frames.WaitForGPU(fence, cmdQueue);
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

    /// @brief Retrieves a pointer to the specified command list if enhanced barriers are supported.
    /// @param[in] cmdList the command list
    /// @return a pointer to the command list converted to `ID3D12GraphicsCommandList7` for enhanced barriers
    /// operations, or `nullptr` if the feature is not supported by the device
    ID3D12GraphicsCommandList7 *GetCommandListForEnhancedBarriers(D3D12GraphicsCommandList &cmdList) const {
        if (!features.enhancedBarriers) {
            return nullptr;
        }
        return cmdList.As7();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // State

    void Reset() {
        vdp2.vramDirty.SetAll();
        vdp2.cramDirty = true;
        vdp2.nextBGLine = 0;
        vdp2.nextComposeLine = 0;
        vdp2.rotRegsDirty = true;
        vdp2.bgRenderParamsDirty = true;
        vdp2.composeParamsDirty = true;

        VDP2UpdateEnabledLayers();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // VDP2 rendering

    uint32 HRes = vdp::kDefaultResH;
    uint32 VRes = vdp::kDefaultResV;
    bool exclusiveMonitor = false;

    void VDP2CacheCRAMColor(uint32 address) {
        // NOTE: 32-bit colors are mapped to even indices to simplify address calculations in shaders

        CRAMColorCache &colorCache = vdp2.cpuCRAMColorCache;
        ColorR8G8B8A8 &color = colorCache[address >> 1u];
        switch (vdpState.regs2.vramControl.colorRAMMode) {
        case 0: { // RGB 5:5:5, half CRAM
            const auto value = vdpState.mem2.ReadCRAM<uint16>(address & ~1u);
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            color.r = color8.r;
            color.g = color8.g;
            color.b = color8.b;
            color.a = color8.msb;
            break;
        }
        case 1: { // RGB 5:5:5, full CRAM
            const auto value = vdpState.mem2.ReadCRAM<uint16>(address & ~1u);
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            color.r = color8.r;
            color.g = color8.g;
            color.b = color8.b;
            color.a = color8.msb;
            break;
        }
        case 2: [[fallthrough]]; // RGB 8:8:8, full CRAM
        case 3: [[fallthrough]]; // RGB 8:8:8, full CRAM
        default: {
            const auto value = vdpState.mem2.ReadCRAM<uint32>(address & ~3u);
            const Color888 color8{.u32 = value};
            color.r = color8.r;
            color.g = color8.g;
            color.b = color8.b;
            color.a = color8.msb;
            break;
        }
        }
    }

    void VDP2CacheAllCRAMColors() {
        // NOTE: 32-bit colors are mapped to even indices to simplify address calculations in shaders

        CRAMColorCache &colorCache = vdp2.cpuCRAMColorCache;
        switch (vdpState.regs2.vramControl.colorRAMMode) {
        case 0: // RGB 5:5:5, half CRAM
            for (uint32 i = 0; i < 1024; ++i) {
                const auto value = vdpState.mem2.ReadCRAM<uint16>(i * sizeof(uint16));
                const Color555 color5{.u16 = value};
                const Color888 color8 = ConvertRGB555to888(color5);
                colorCache[i].r = color8.r;
                colorCache[i].g = color8.g;
                colorCache[i].b = color8.b;
                colorCache[i].a = color8.msb;
            }
            break;
        case 1: // RGB 5:5:5, full CRAM
            for (uint32 i = 0; i < 2048; ++i) {
                const auto value = vdpState.mem2.ReadCRAM<uint16>(i * sizeof(uint16));
                const Color555 color5{.u16 = value};
                const Color888 color8 = ConvertRGB555to888(color5);
                colorCache[i].r = color8.r;
                colorCache[i].g = color8.g;
                colorCache[i].b = color8.b;
                colorCache[i].a = color8.msb;
            }
            break;
        case 2: [[fallthrough]]; // RGB 8:8:8, full CRAM
        case 3: [[fallthrough]]; // RGB 8:8:8, full CRAM
        default:
            for (uint32 i = 0; i < 1024; ++i) {
                const auto value = vdpState.mem2.ReadCRAM<uint32>(i * sizeof(uint32));
                const Color888 color8{.u32 = value};
                colorCache[i << 1u].r = color8.r;
                colorCache[i << 1u].g = color8.g;
                colorCache[i << 1u].b = color8.b;
                colorCache[i << 1u].a = color8.msb;
            }
            break;
        }
    }

    void VDP2WriteVRAM(uint32 address) {
        vdp2.vramDirty.Set(address >> VDP2Resources::kVRAMDirtyBitmapChunkSizeShift);
    }

    void VDP2WriteCRAM(uint32 address) {
        vdp2.cramDirty = true;
        VDP2CacheCRAMColor(address);
    }

    void VDP2WriteReg(uint32 address, uint16 value) {
        struct DirtyFlags {
            bool render = false;
            bool compose = false;
            bool rotRegs = false;
            bool enabledLayers = false;
            bool cram = false;
        };
        static constexpr auto kDirtyFlags = [] {
            std::array<DirtyFlags, 0x11E / sizeof(uint16) + 1> arr{};

            for (uint32 addr : {
                     0x000 /*TVMD*/,   0x002 /*EXTEN*/,  0x006 /*VRSIZE*/, 0x00E /*RAMCTL*/, 0x010 /*CYCA0L*/,
                     0x012 /*CYCA0U*/, 0x014 /*CYCA1L*/, 0x016 /*CYCA1U*/, 0x018 /*CYCB0L*/, 0x01A /*CYCB0U*/,
                     0x01C /*CYCB1L*/, 0x01E /*CYCB1U*/, 0x020 /*BGON*/,   0x022 /*MZCTL*/,  0x024 /*SFSEL*/,
                     0x026 /*SFCODE*/, 0x028 /*CHCTLA*/, 0x02A /*CHCTLB*/, 0x02C /*BMPNA*/,  0x02E /*BMPNB*/,
                     0x030 /*PNCNA*/,  0x032 /*PNCNB*/,  0x034 /*PNCNC*/,  0x036 /*PNCND*/,  0x038 /*PNCR*/,
                     0x03A /*PLSZ*/,   0x03C /*MPOFN*/,  0x03E /*MPOFR*/,  0x040 /*MPABN0*/, 0x042 /*MPCDN0*/,
                     0x044 /*MPABN1*/, 0x046 /*MPCDN1*/, 0x048 /*MPABN2*/, 0x04A /*MPCDN2*/, 0x04C /*MPABN3*/,
                     0x04E /*MPCDN3*/, 0x050 /*MPABRA*/, 0x052 /*MPCDRA*/, 0x054 /*MPEFRA*/, 0x056 /*MPGHRA*/,
                     0x058 /*MPIJRA*/, 0x05A /*MPKLRA*/, 0x05C /*MPMNRA*/, 0x05E /*MPOPRA*/, 0x060 /*MPABRB*/,
                     0x062 /*MPCDRB*/, 0x064 /*MPEFRB*/, 0x066 /*MPGHRB*/, 0x068 /*MPIJRB*/, 0x06A /*MPKLRB*/,
                     0x06C /*MPMNRB*/, 0x06E /*MPOPRB*/, 0x070 /*SCXIN0*/, 0x072 /*SCXDN0*/, 0x074 /*SCYIN0*/,
                     0x076 /*SCYDN0*/, 0x078 /*ZMXIN0*/, 0x07A /*ZMXDN0*/, 0x07C /*ZMYIN0*/, 0x07E /*ZMYDN0*/,
                     0x080 /*SCXIN1*/, 0x082 /*SCXDN1*/, 0x084 /*SCYIN1*/, 0x086 /*SCYDN1*/, 0x088 /*ZMXIN1*/,
                     0x08A /*ZMXDN1*/, 0x08C /*ZMYIN1*/, 0x08E /*ZMYDN1*/, 0x090 /*SCXN2*/,  0x092 /*SCYN2*/,
                     0x094 /*SCXN3*/,  0x096 /*SCYN3*/,  0x098 /*ZMCTL*/,  0x09A /*SCRCTL*/, 0x09C /*VCSTAU*/,
                     0x09E /*VCSTAL*/, 0x0A0 /*LSTA0U*/, 0x0A2 /*LSTA0L*/, 0x0A4 /*LSTA1U*/, 0x0A6 /*LSTA1L*/,
                     0x0A8 /*LCTAU*/,  0x0AA /*LCTAL*/,  0x0AC /*BKTAU*/,  0x0AE /*BKTAL*/,  0x0B0 /*RPMD*/,
                     0x0B8 /*OVPNRA*/, 0x0BA /*OVPNRB*/, 0x0C0 /*WPSX0*/,  0x0C2 /*WPSY0*/,  0x0C4 /*WPEX0*/,
                     0x0C6 /*WPEY0*/,  0x0C8 /*WPSX1*/,  0x0CA /*WPSY1*/,  0x0CC /*WPEX1*/,  0x0CE /*WPEY1*/,
                     0x0D0 /*WCTLA*/,  0x0D2 /*WCTLB*/,  0x0D4 /*WCTLC*/,  0x0D6 /*WCTLD*/,  0x0D8 /*LWTA0U*/,
                     0x0DA /*LWTA0L*/, 0x0DC /*LWTA1U*/, 0x0DE /*LWTA1L*/, 0x0E0 /*SPCTL*/,  0x0E2 /*SDCTL*/,
                     0x0E4 /*CRAOFA*/, 0x0E6 /*CRAOFB*/, 0x0E8 /*LNCLEN*/, 0x0EA /*SFPRMD*/, 0x0EC /*CCCTL*/,
                     0x0EE /*SFCCMD*/, 0x0F0 /*PRISA*/,  0x0F2 /*PRISB*/,  0x0F4 /*PRISC*/,  0x0F6 /*PRISD*/,
                     0x0F8 /*PRINA*/,  0x0FA /*PRINB*/,  0x0FC /*PRIR*/,
                 }) {
                arr[addr / sizeof(uint16)].render = true;
            }

            for (uint32 addr : {
                     0x000 /*TVMD*/,   0x006 /*VRSIZE*/, 0x020 /*BGON*/,  0x0E0 /*SPCTL*/, 0x0E2 /*SDCTL*/,
                     0x0E8 /*LNCLEN*/, 0x0EC /*CCCTL*/,  0x100 /*CCRSA*/, 0x102 /*CCRSB*/, 0x104 /*CCRSC*/,
                     0x106 /*CCRSD*/,  0x108 /*CCRNA*/,  0x10A /*CCRNB*/, 0x10C /*CCRR*/,  0x10E /*CCRLB*/,
                     0x110 /*CLOFEN*/, 0x112 /*CLOFSL*/, 0x114 /*COAR*/,  0x116 /*COAG*/,  0x118 /*COAB*/,
                     0x11A /*COBR*/,   0x11C /*COBG*/,   0x11E /*COBB*/,
                 }) {
                arr[addr / sizeof(uint16)].compose = true;
            }

            for (uint32 addr : {0x00E /*RAMCTL*/, 0x020 /*BGON*/, 0x0B2 /*RPRCTL*/, 0x0B4 /*KTCTL*/, 0x0B6 /*KTAOF*/,
                                0x0BC /*RPTAU*/, 0x0BE /*RPTAL*/}) {
                arr[addr / sizeof(uint16)].rotRegs = true;
            }

            for (uint32 addr : {0x020 /*BGON*/, 0x028 /*CHCTLA*/, 0x02A /*CHCTLB*/}) {
                arr[addr / sizeof(uint16)].enabledLayers = true;
            }

            arr[0x00E / sizeof(uint16) /*RAMCTL*/].cram = true;

            return arr;
        }();

        if (address <= 0x11E) {
            const auto &dirtyFlags = kDirtyFlags[address / sizeof(uint16)];
            vdp2.rotRegsDirty |= dirtyFlags.rotRegs;
            vdp2.bgRenderParamsDirty |= dirtyFlags.render;
            vdp2.composeParamsDirty |= dirtyFlags.compose;

            if (dirtyFlags.enabledLayers) {
                VDP2UpdateEnabledLayers();
            }

            if (dirtyFlags.cram) {
                vdp2.cramDirty = true;
                VDP2CacheAllCRAMColors();
            }
        }
    }

    util::VoidResult<> VDP2FlushVRAM() {
        if (!vdp2.vramDirty) {
            return {};
        }

        VDP2FrameContext &frame = vdp2.frames.GetCurrentFrame();
        const size_t frameIndex = vdp2.frames.frameIndex;

        struct Transfer {
            UINT64 srcOffset;
            UINT64 dstOffset;
            UINT64 length;
        };

        // Upload buffer resource pointer -> transfer info.
        // Enables us to optimize transfer commands later on.
        std::unordered_map<ID3D12Resource *, std::vector<Transfer>> transfers{};

        // Pointer to most recently used upload buffer
        UploadBuffer *buffer = nullptr;

        size_t pos, count = 0;
        for (pos = vdp2.vramDirty.FindNext(count); pos < vdp2.vramDirty.Size();
             pos = vdp2.vramDirty.FindNext(count, pos + count)) {
            const uint32 vramOffset = pos << VDP2Resources::kVRAMDirtyBitmapChunkSizeShift;
            const uint32 size = count << VDP2Resources::kVRAMDirtyBitmapChunkSizeShift;

            // Get or allocate upload buffer with enough space for this chunk
            size_t uploadOffset = 0;
            void *uploadData = nullptr;
            if (buffer != nullptr && !buffer->Allocate(size, uploadOffset, uploadData)) {
                // Current buffer doesn't have enough space.
                // Reset buffer pointer to force allocation below.
                buffer = nullptr;
            }

            // If we don't have a buffer, it's either because this is the first allocation attempt or the previous
            // buffer ran out of space. Look for a buffer to allocate and remember it for all subsequent allocations.
            if (buffer == nullptr) {
                buffer = FindUploadBuffer(frame.vramUploadBuffer, frame.vramUploadOverflowBuffers, size);
                if (buffer == nullptr) {
                    buffer = &frame.vramUploadOverflowBuffers.emplace_back();
                    const size_t index = frame.vramUploadOverflowBuffers.size();
                    if (auto result = buffer->Create(device, kVDP2VRAMUploadBufferSize); !result) {
                        return util::ErrorMessage{fmt::format("Could not create VDP2 VRAM upload buffer #{}-{}: {}",
                                                              frameIndex, index, result.Error().message)};
                    }
                    buffer->resource->SetName(
                        fmt::format(L"[Ymir-VDP2] VRAM upload buffer #{}-{}", frameIndex, index).c_str());
                }

                // This should succeed, but let's not crash if it fails
                if (!buffer->Allocate(size, uploadOffset, uploadData)) {
                    return util::ErrorMessage{fmt::format("Ran out of memory for VDP2 VRAM upload buffers")};
                }
            }

            // Copy data to upload buffer
            memcpy(uploadData, &vdpState.mem2.VRAM[vramOffset], size);

            // Store command info so we can sort them by buffer to minimize barrier transitions
            ID3D12Resource *key = buffer->resource.GetPointer();
            transfers[key].push_back({
                .srcOffset = uploadOffset,
                .dstOffset = vramOffset,
                .length = size,
            });
        }

        vdp2.vramDirty.ClearAll();

        // Enqueue all transfers
        for (auto &[srcBuffer, transferList] : transfers) {
            // Indicate that the VDP2 VRAM buffer will be used as copy destination
            if (auto *enhCmdList = GetCommandListForEnhancedBarriers(vdp2.cmdList)) {
                D3D12_BUFFER_BARRIER barrier{
                    .SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                    .SyncAfter = D3D12_BARRIER_SYNC_COPY,
                    .AccessBefore = D3D12_BARRIER_ACCESS_COMMON,
                    .AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST,
                    .pResource = srcBuffer,
                    .Offset = 0,
                    .Size = kVDP2VRAMUploadBufferSize,
                };
                const D3D12_BARRIER_GROUP group{
                    .Type = D3D12_BARRIER_TYPE_BUFFER,
                    .NumBarriers = 1,
                    .pBufferBarriers = &barrier,
                };
                enhCmdList->Barrier(1, &group);
            } else {
                D3D12_RESOURCE_BARRIER barrier{
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = srcBuffer,
                            .Subresource = 0,
                            .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                            .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                        },
                };
                vdp2.cmdList->ResourceBarrier(1, &barrier);
            }

            for (Transfer &transfer : transferList) {
                vdp2.cmdList->CopyBufferRegion(vdp2.vramBuffer.GetPointer(), transfer.dstOffset, srcBuffer,
                                               transfer.srcOffset, transfer.length);
            }

            // Indicate that the VDP2 VRAM buffer will be used with compute shaders
            if (auto *enhCmdList = GetCommandListForEnhancedBarriers(vdp2.cmdList)) {
                D3D12_BUFFER_BARRIER barrier{
                    .SyncBefore = D3D12_BARRIER_SYNC_COPY,
                    .SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                    .AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST,
                    .AccessAfter = D3D12_BARRIER_ACCESS_COMMON,
                    .pResource = srcBuffer,
                    .Offset = 0,
                    .Size = kVDP2VRAMUploadBufferSize,
                };
                const D3D12_BARRIER_GROUP group{
                    .Type = D3D12_BARRIER_TYPE_BUFFER,
                    .NumBarriers = 1,
                    .pBufferBarriers = &barrier,
                };
                enhCmdList->Barrier(1, &group);
            } else {
                D3D12_RESOURCE_BARRIER barrier{
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = srcBuffer,
                            .Subresource = 0,
                            .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                            .StateAfter = D3D12_RESOURCE_STATE_COMMON,
                        },
                };
                vdp2.cmdList->ResourceBarrier(1, &barrier);
            }
        };

        return {};
    }

    void VDP2FlushCRAM() {
        if (!vdp2.cramDirty) {
            return;
        }

        VDP2FrameContext &frame = vdp2.frames.GetCurrentFrame();

        // ---------------------------------------------------------------------
        // Update color cache

        {
            size_t offset = 0;
            void *colorMap = nullptr;
            const bool result = frame.cramColorUploadBuffer.Allocate(sizeof(CRAMColorCache), offset, colorMap);
            // This should never fail unless the VDP2 is producing more than 256 lines
            // TODO: this is actually happening during resolution changes somehow :(
            assert(result);
            memcpy(colorMap, vdp2.cpuCRAMColorCache.data(), sizeof(CRAMColorCache));

            if (auto *enhCmdList = GetCommandListForEnhancedBarriers(vdp2.cmdList)) {
                D3D12_BUFFER_BARRIER barrier{
                    .SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                    .SyncAfter = D3D12_BARRIER_SYNC_COPY,
                    .AccessBefore = D3D12_BARRIER_ACCESS_COMMON,
                    .AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST,
                    .pResource = frame.cramColorUploadBuffer.resource.GetPointer(),
                    .Offset = 0,
                    .Size = kVDP2CRAMColorBufferSize * 256,
                };
                const D3D12_BARRIER_GROUP group{
                    .Type = D3D12_BARRIER_TYPE_BUFFER,
                    .NumBarriers = 1,
                    .pBufferBarriers = &barrier,
                };
                enhCmdList->Barrier(1, &group);
            } else {
                D3D12_RESOURCE_BARRIER barrier{
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = frame.cramColorUploadBuffer.resource.GetPointer(),
                            .Subresource = 0,
                            .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                            .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                        },
                };
                vdp2.cmdList->ResourceBarrier(1, &barrier);
            }

            vdp2.cmdList->CopyBufferRegion(vdp2.cramColorBuffer.GetPointer(), 0,
                                           frame.cramColorUploadBuffer.resource.GetPointer(), offset,
                                           sizeof(CRAMColorCache));

            if (auto *enhCmdList = GetCommandListForEnhancedBarriers(vdp2.cmdList)) {
                D3D12_BUFFER_BARRIER barrier{
                    .SyncBefore = D3D12_BARRIER_SYNC_COPY,
                    .SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                    .AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST,
                    .AccessAfter = D3D12_BARRIER_ACCESS_COMMON,
                    .pResource = frame.cramColorUploadBuffer.resource.GetPointer(),
                    .Offset = 0,
                    .Size = kVDP2CRAMColorBufferSize * 256,
                };
                const D3D12_BARRIER_GROUP group{
                    .Type = D3D12_BARRIER_TYPE_BUFFER,
                    .NumBarriers = 1,
                    .pBufferBarriers = &barrier,
                };
                enhCmdList->Barrier(1, &group);
            } else {
                D3D12_RESOURCE_BARRIER barrier{
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = frame.cramColorUploadBuffer.resource.GetPointer(),
                            .Subresource = 0,
                            .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                            .StateAfter = D3D12_RESOURCE_STATE_COMMON,
                        },
                };
                vdp2.cmdList->ResourceBarrier(1, &barrier);
            }
        }

        // ---------------------------------------------------------------------
        // Update rotation coefficients view

        const VDP2Regs &regs2 = vdpState.regs2;
        if ((regs2.bgEnabled[4] || regs2.bgEnabled[5]) && regs2.vramControl.colorRAMCoeffTableEnable) {
            size_t offset = 0;
            void *rotCoeffMap = nullptr;
            const bool result = frame.cramRotCoeffUploadBuffer.Allocate(kVDP2CRAMRotCoeffSize, offset, rotCoeffMap);
            // This should never fail unless the VDP2 is producing more than 256 lines
            assert(result);
            memcpy(rotCoeffMap, &vdpState.mem2.CRAM[kVDP2CRAMSize / 2], kVDP2CRAMRotCoeffSize);

            if (auto *enhCmdList = GetCommandListForEnhancedBarriers(vdp2.cmdList)) {
                D3D12_BUFFER_BARRIER barrier{
                    .SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                    .SyncAfter = D3D12_BARRIER_SYNC_COPY,
                    .AccessBefore = D3D12_BARRIER_ACCESS_COMMON,
                    .AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST,
                    .pResource = frame.cramRotCoeffUploadBuffer.resource.GetPointer(),
                    .Offset = 0,
                    .Size = kVDP2CRAMRotCoeffSize * 256,
                };
                const D3D12_BARRIER_GROUP group{
                    .Type = D3D12_BARRIER_TYPE_BUFFER,
                    .NumBarriers = 1,
                    .pBufferBarriers = &barrier,
                };
                enhCmdList->Barrier(1, &group);
            } else {
                D3D12_RESOURCE_BARRIER barrier{
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = frame.cramRotCoeffUploadBuffer.resource.GetPointer(),
                            .Subresource = 0,
                            .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                            .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                        },
                };
                vdp2.cmdList->ResourceBarrier(1, &barrier);
            }

            vdp2.cmdList->CopyBufferRegion(vdp2.cramRotCoeffBuffer.GetPointer(), 0,
                                           frame.cramRotCoeffUploadBuffer.resource.GetPointer(), offset,
                                           kVDP2CRAMRotCoeffSize);

            if (auto *enhCmdList = GetCommandListForEnhancedBarriers(vdp2.cmdList)) {
                D3D12_BUFFER_BARRIER barrier{
                    .SyncBefore = D3D12_BARRIER_SYNC_COPY,
                    .SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                    .AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST,
                    .AccessAfter = D3D12_BARRIER_ACCESS_COMMON,
                    .pResource = frame.cramRotCoeffUploadBuffer.resource.GetPointer(),
                    .Offset = 0,
                    .Size = kVDP2CRAMRotCoeffSize * 256,
                };
                const D3D12_BARRIER_GROUP group{
                    .Type = D3D12_BARRIER_TYPE_BUFFER,
                    .NumBarriers = 1,
                    .pBufferBarriers = &barrier,
                };
                enhCmdList->Barrier(1, &group);
            } else {
                D3D12_RESOURCE_BARRIER barrier{
                    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                    .Transition =
                        {
                            .pResource = frame.cramRotCoeffUploadBuffer.resource.GetPointer(),
                            .Subresource = 0,
                            .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                            .StateAfter = D3D12_RESOURCE_STATE_COMMON,
                        },
                };
                vdp2.cmdList->ResourceBarrier(1, &barrier);
            }
        }
    }

    void VDP2CalcAccessPatterns() {
        vdp2.bgRenderParamsDirty |= vdpState.regs2.accessPatternsDirty;
        vdpState.state2.CalcAccessPatterns(vdpState.regs2, vdp2.accessPatternsConfig);
    }

    void VDP2InitNBGs() {
        const VDP2Regs &regs2 = vdpState.regs2;

        for (uint32 i = 0; i < 4; ++i) {
            const BGParams &bgParams = regs2.bgParams[i + 1];
            NBGLayerState &nbgState = vdpState.state2.nbgLayerStates[i];

            // NOTE: fracScrollX/Y are computed from scratch in the shader
            nbgState.scrollIncH = bgParams.scrollIncH;

            if (i < 2) {
                nbgState.lineScrollTableAddress = bgParams.lineScrollTableAddress;
            }
        }

        vdp2.bgRenderParamsDirty = true;
    }

    void VDP2UpdateEnabledLayers() {
        vdpState.state2.UpdateEnabledBGs(vdpState.regs2, vdp2.debugRenderOptions);
    }

    void VDP2CalcVCellScrollDelay() {
        vdp2.bgRenderParamsDirty |= vdpState.regs2.accessPatternsDirty;
        vdpState.state2.CalcVCellScrollDelay(vdpState.regs2);
    }

    void VDP2DrawLineColorBackScreens(uint32 y) {
        const VDP2Regs &regs = vdpState.regs2;

        // Read line color screen color
        {
            const LineBackScreenParams &lineParams = regs.lineScreenParams;
            const uint32 lnclY = lineParams.perLine ? y : 0;
            const uint32 address = lineParams.baseAddress + lnclY * sizeof(uint16);
            const uint32 cramAddress = vdpState.mem2.ReadVRAM<uint16>(address);
            vdp2.cpuLineColors[y][0] = vdp2.cpuCRAMColorCache[cramAddress & 0x7FF];
        }

        // Read back screen color
        {
            const LineBackScreenParams &backParams = regs.backScreenParams;
            const uint32 backY = backParams.perLine ? y : 0;
            const uint32 address = backParams.baseAddress + backY * sizeof(Color555);
            const Color555 color5{.u16 = vdpState.mem2.ReadVRAM<uint16>(address)};
            const Color888 color8 = ConvertRGB555to888(color5);
            vdp2.cpuLineColors[y][1].r = color8.r;
            vdp2.cpuLineColors[y][1].g = color8.g;
            vdp2.cpuLineColors[y][1].b = color8.b;
            vdp2.cpuLineColors[y][1].a = color8.msb;
        }
    }

    void VDP2UpdateRotationParameterBases(uint16 y) {
        VDP2Regs &regs2 = vdpState.regs2;
        if (!regs2.bgEnabled[4] && !regs2.bgEnabled[5]) {
            // Skip if no RBGs are enabled
            return;
        }

        const bool readAll = y == 0;

        const uint32 baseAddress = regs2.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
        for (uint32 i = 0; i < 2; ++i) {
            VDP2RotParamBase &base = vdp2.cpuRotParamBases[i * kMaxNormalResV + y];
            RotationParams &src = regs2.rotParams[i];

            const uint32 address = baseAddress + i * 0x80;

            base.tableAddress = address;

            if (readAll || src.readXst) {
                base.Xst = bit::extract_signed<6, 28, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x00));
                src.readXst = false;
            } else {
                const VDP2RotParamBase &prevBase = vdp2.cpuRotParamBases[i * kMaxNormalResV + y - 1];
                base.Xst =
                    prevBase.Xst + bit::extract_signed<6, 18, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x0C));
            }

            if (readAll || src.readYst) {
                base.Yst = bit::extract_signed<6, 28, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x04));
                src.readYst = false;
            } else {
                const VDP2RotParamBase &prevBase = vdp2.cpuRotParamBases[i * kMaxNormalResV + y - 1];
                base.Yst =
                    prevBase.Yst + bit::extract_signed<6, 18, sint32>(vdpState.mem2.ReadVRAM<uint32>(address + 0x10));
            }

            if (readAll || src.readKAst) {
                const uint32 KAst = bit::extract<6, 31>(vdpState.mem2.ReadVRAM<uint32>(address + 0x54));
                base.KA = src.coeffTableAddressOffset + KAst;
                src.readKAst = false;
            } else {
                const VDP2RotParamBase &prevBase = vdp2.cpuRotParamBases[i * kMaxNormalResV + y - 1];
                base.KA = prevBase.KA + bit::extract_signed<6, 25>(vdpState.mem2.ReadVRAM<uint32>(address + 0x58));
            }
        }
    }

    void VDP2UpdateState() {
        VDP2FlushVRAM();
        VDP2FlushCRAM();
    }

    void VDP2BeginFrame() {
        auto &cmdList = vdp2.cmdList;

        vdp2.nextBGLine = 0;
        vdp2.nextComposeLine = 0;

        VDP2CalcAccessPatterns();
        VDP2InitNBGs();

        VDP2UpdateState();
    }

    void VDP2RenderLayerLines(uint32 y) {
        // TODO: implement
        // - if BG rendering state is dirty:
        //   - set rendering parameters
        //   - render BG lines from bgRenderSegmentStartY to y-1 (if possible)
        //   - set bgRenderSegmentStartY = y
        //   - clear dirty state
    }

    void VDP2ComposeLines(uint32 y) {
        // TODO: implement
        // - if compositing state is dirty:
        //   - set compositing parameters
        //   - compose lines from composeSegmentStartY to y-1 (if possible)
        //   - set composeSegmentStartY = y
        //   - clear dirty state
    }

    void VDP2RenderLine(uint32 y) {
        VDP2CalcVCellScrollDelay();
        VDP2DrawLineColorBackScreens(y);
        VDP2UpdateRotationParameterBases(y);
        vdpState.state2.UpdateRotationPageBaseAddresses(vdpState.regs2);

        // When Y=0, the changes happened during vblank (or, more precisely, between the last Y of the previous frame
        // and the first line of this frame). Otherwise, the changes happened between Y-1 and Y. Therefore, we need to
        // render lines up to Y-1 then sync the state, unless Y=0, in which case we just sync the state.

        if (y > 0) {
            const bool renderLayers =
                (hwRenderConfig.vdp2SyncInterval == VDP2VRAMSyncInterval::Scanline && vdp2.vramDirty) ||
                vdp2.cramDirty || vdp2.rotRegsDirty || vdp2.bgRenderParamsDirty || vdp2.composeParamsDirty;
            const bool compose = vdp2.composeParamsDirty;
            if (renderLayers) {
                VDP2RenderLayerLines(y - 1);
            }
            if (compose) {
                VDP2ComposeLines(y - 1);
            }
        }

        VDP2UpdateState();
    }

    void VDP2EndFrame() {
        const bool vShift = vdpState.regs2.TVMD.IsInterlaced() ? 1u : 0u;
        const uint32 vres = VRes >> vShift;
        VDP2RenderLayerLines(vres - 1);
        VDP2ComposeLines(VRes - 1);

        VDP2UpdateState();
        if (hwRenderConfig.vdp2SyncInterval == VDP2VRAMSyncInterval::Frame) {
            VDP2FlushVRAM();
        }

        // TODO: this is just for testing; remove it
        auto &cmdList = vdp2.cmdList;
        cmdList->SetPipelineState(vdp2.drawBGsPSO.GetPointer());
        cmdList->SetComputeRootSignature(vdp2.drawBGsRootSig.GetPointer());
        cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp2.cpuCommonRenderParams) / sizeof(uint32),
                                              &vdp2.cpuCommonRenderParams, 0);
        cmdList->SetComputeRootDescriptorTable(1, vdp2.layerOutUAV.gpuHandle);
        cmdList->Dispatch(vdp::kMaxResH / 32, vdp::kMaxResV, 6);
        cmdList->Close();

        // ---------------------------

        // Submit command list
        cmdQueue->ExecuteCommandLists(1, cmdList.GetAddressOfBase());

        // Advance frame
        vdp2.frames.MoveToNextFrame(fence, cmdQueue);

        // Reset frame
        VDP2FrameContext &nextFrame = vdp2.frames.GetCurrentFrame();
        nextFrame.Reset();
        cmdList->Reset(nextFrame.cmdAlloc.GetPointer(), nullptr);

        // Setup command list
        ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer()};
        cmdList->SetDescriptorHeaps(std::size(heaps), heaps);
    }
};

// ---------------------------------------------------------------------------------------------------------------------

Direct3D12VDPRenderer::Direct3D12VDPRenderer(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                                             const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig,
                                             core::Configuration::HardwareRenderer &hwRenderConfig)
    : HardwareVDPRendererBase(VDPRendererType::Direct3D12, hwRenderConfig)
    , m_impl(std::make_unique<Impl>(state, vdp2AccessPatternsConfig, vdp2DebugRenderOptions, hwRenderConfig)) {}

Direct3D12VDPRenderer::~Direct3D12VDPRenderer() {
    m_impl->Shutdown();
}

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
    m_impl->Reset();
}

// -----------------------------------------------------------------------------
// Save states

void Direct3D12VDPRenderer::PreSaveStateSync() {}

void Direct3D12VDPRenderer::PostLoadStateSync() {
    // TODO: m_impl->vdp1.vramDirty.SetAll();

    m_impl->VDP2UpdateEnabledLayers();
    m_impl->vdp2.vramDirty.SetAll();
    m_impl->vdp2.cramDirty = true;
    m_impl->vdp2.rotRegsDirty = true;
    m_impl->vdp2.bgRenderParamsDirty = true;
    m_impl->vdp2.composeParamsDirty = true;
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
    m_impl->VDP2WriteVRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {
    // The address is always word-aligned, so the value will never straddle two chunks
    m_impl->VDP2WriteVRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {
    m_impl->VDP2WriteCRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {
    // The address is always word-aligned
    m_impl->VDP2WriteCRAM(address);
}

void Direct3D12VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    m_impl->VDP2WriteReg(address, value);
}

// -----------------------------------------------------------------------------
// Debugger

void Direct3D12VDPRenderer::UpdateEnabledLayers() {
    m_impl->VDP2UpdateEnabledLayers();
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
    m_impl->HRes = h;
    m_impl->VRes = v;
    m_impl->exclusiveMonitor = exclusive;
    Callbacks.VDP2ResolutionChanged(h, v);
}

void Direct3D12VDPRenderer::VDP2SetField(bool odd) {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D12VDPRenderer::VDP2LatchTVMD() {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D12VDPRenderer::VDP2BeginFrame() {
    m_impl->VDP2BeginFrame();
}

void Direct3D12VDPRenderer::VDP2RenderLine(uint32 y) {
    m_impl->VDP2RenderLine(y);
}

void Direct3D12VDPRenderer::VDP2EndFrame() {
    m_impl->VDP2EndFrame();
    Callbacks.VDP2DrawFinished();
}

} // namespace ymir::vdp
