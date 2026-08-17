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

#include <unordered_map>
#include <vector>

using namespace ymir::gpu::d3d12;

namespace ymir::vdp {

/// @brief Name of the entrypoint function for all compute shaders.
static constexpr const char *kCSEntrypoint = "CSMain";

/// @brief Contains the compiled shader files from res/shaders/src.
cmrc::embedded_filesystem g_fsShaders = cmrc::ymir_core_shaders::get_filesystem();

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
    Impl(VDPState &state)
        : vdpState(state) {}

    VDPState &vdpState;

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
    template <size_t count>
    struct FrameSet {
        std::array<FrameContext, count> frames;
        size_t frameIndex = 0;

        FrameContext &GetCurrentFrame() {
            return frames[frameIndex];
        }
        const FrameContext &GetCurrentFrame() const {
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

        FrameContext &operator[](size_t index) {
            return frames[index];
        }
        const FrameContext &operator[](size_t index) const {
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
        /// @brief VDP2 frame resources.
        FrameSet<3> frames;

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

        /// @brief Size of a VDP2 VRAM upload buffer.
        static constexpr UINT64 kVRAMUploadBufferSize = vdp::kVDP2VRAMSize * 4;
        // Note to developers: tweak size as needed. The value should be large enough to cover most cases while
        // requiring at most one overflow buffer in extreme cases.
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

        // VDP2 VRAM upload buffer
        {
            if (auto result = vdp2.vramUploadBuffer.Create(device, VDP2Resources::kVRAMUploadBufferSize); !result) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 VRAM upload buffer: {}", result.Error().message)};
            }
            vdp2.vramUploadBuffer.resource->SetName(L"[Ymir-VDP2] VRAM upload buffer");
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

            auto builder = vdp2.cramRotCoeffBuffer.BufferBuilder(kCRAMRotCoeffSize);
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
                        .NumElements = kCRAMRotCoeffSize / sizeof(uint32),
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
        // TODO: reset to initial state (clear VRAM, reset registers, mark everything as dirty, etc.)

        vdp2.vramDirty.SetAll();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // VDP2 rendering

    uint32 HRes = vdp::kDefaultResH;
    uint32 VRes = vdp::kDefaultResV;
    bool exclusiveMonitor = false;

    void VDP2WriteVRAM(uint32 address, uint8 value) {
        vdp2.vramDirty.Set(address >> VDP2Resources::kVRAMDirtyBitmapChunkSizeShift);
    }

    void VDP2WriteVRAM(uint32 address, uint16 value) {
        // The address is always word-aligned, so the value will never straddle two chunks
        vdp2.vramDirty.Set(address >> VDP2Resources::kVRAMDirtyBitmapChunkSizeShift);
    }

    void VDP2WriteCRAM(uint32 address, uint8 value) {
        // TODO: mark as dirty; cache converted colors
    }

    void VDP2WriteCRAM(uint32 address, uint16 value) {
        // TODO: mark as dirty; cache converted colors
    }

    void VDP2WriteReg(uint32 address, uint16 value) {
        // TODO: mark as dirty depending on register; recompute cached CRAM colors if needed
    }

    util::VoidResult<> VDP2FlushVRAM() {
        if (!vdp2.vramDirty) {
            return {};
        }

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
                buffer = FindUploadBuffer(vdp2.vramUploadBuffer, vdp2.vramUploadOverflowBuffers, size);
                if (buffer == nullptr) {
                    buffer = &vdp2.vramUploadOverflowBuffers.emplace_back();
                    const size_t index = vdp2.vramUploadOverflowBuffers.size();
                    if (auto result = buffer->Create(device, VDP2Resources::kVRAMUploadBufferSize); !result) {
                        return util::ErrorMessage{
                            fmt::format("Could not create VDP2 VRAM upload overflow buffer #{}: {}", index,
                                        result.Error().message)};
                    }
                    buffer->resource->SetName(
                        fmt::format(L"[Ymir-VDP2] VRAM upload overflow buffer #{}", index).c_str());
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
                    .Size = VDP2Resources::kVRAMUploadBufferSize,
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
                    .Size = VDP2Resources::kVRAMUploadBufferSize,
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
        // TODO: copy the whole thing to an upload buffer then issue a CopyBufferRegion
        // - upload buffer can be 256 times as large as the CRAM buffers (8 KB for colors, 2 KB for rotcoeff)
        //   - 256 = maximum normal vertical resolution (interlaced modes count every other line)
        // - use y*size as the offset, no need to bump-allocate
        // - note that this has to be done for the color and and rotcoeff buffers
        //   - rotcoeff only needs to be updated if:
        //     (regs2.bgEnabled[4] || regs2.bgEnabled[5]) && regs2.vramControl.colorRAMCoeffTableEnable
    }

    void VDP2BeginFrame() {
        auto &cmdList = vdp2.cmdList;

        VDP2FlushVRAM();
        VDP2FlushCRAM();

        // TODO: prepare new VDP2 frame
        // - set up rendering parameters
        // - update rendering parameters and set 32-bit constants
    }

    void VDP2RenderLine(uint32 y) {
        // TODO: prepare next line, render and compose lines; optimize by batching lines without state changes
        // - if there are pending VRAM writes:
        //   - draw lines from segmentStartY to y-1 (if possible)
        //   - VDP2FlushVRAM() + VDP2FlustCRAM()
        //   - set segmentStartY = y
        // - update rendering parameters and set 32-bit constants
    }

    void VDP2EndFrame() {
        // TODO: finish VDP2 frame
        // - draw lines from segmentStartY to bottom of framebuffer
        // - update rendering parameters and set 32-bit constants

        // TODO: this is just for testing; remove it
        auto &cmdList = vdp2.cmdList;
        cmdList->SetPipelineState(vdp2.drawBGsPSO.GetPointer());
        cmdList->SetComputeRootSignature(vdp2.drawBGsRootSig.GetPointer());
        // TODO: cmdList->SetComputeRoot32BitConstants(0, sizeof(vdp2.cpuRenderParams), &vdp2.cpuRenderParams, 0);
        cmdList->SetComputeRootDescriptorTable(1, vdp2.layerOutUAV.gpuHandle);
        cmdList->Dispatch(vdp::kMaxResH / 32, vdp::kMaxResV, 6);
        cmdList->Close();

        // ---------------------------

        // Submit command list
        cmdQueue->ExecuteCommandLists(1, cmdList.GetAddressOfBase());

        // Advance frame
        vdp2.frames.MoveToNextFrame(fence, cmdQueue);

        // Reset command list
        FrameContext &nextFrame = vdp2.frames.GetCurrentFrame();
        nextFrame.cmdAlloc->Reset();
        cmdList->Reset(nextFrame.cmdAlloc.GetPointer(), nullptr);

        // Setup command list
        ID3D12DescriptorHeap *heaps[] = {resourceHeap.GetPointer()};
        cmdList->SetDescriptorHeaps(std::size(heaps), heaps);

        // Clear state
        // TODO: needs to be properly fenced
        vdp2.vramUploadBuffer.Reset();
        for (UploadBuffer &buffer : vdp2.vramUploadOverflowBuffers) {
            buffer.Reset();
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------

Direct3D12VDPRenderer::Direct3D12VDPRenderer(VDPState &state, const config::VDP2DebugRender &vdp2DebugRenderOptions,
                                             const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig,
                                             core::Configuration::HardwareRenderer &hwRenderConfig)
    : HardwareVDPRendererBase(VDPRendererType::Direct3D12, hwRenderConfig)
    , m_impl(std::make_unique<Impl>(state))
    , m_vdp2DebugRenderOptions(vdp2DebugRenderOptions)
    , m_vdp2AccessPatternsConfig(vdp2AccessPatternsConfig)
    , m_hwRenderConfig(hwRenderConfig) {}

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
    m_impl->VDP2WriteVRAM(address, value);
}

void Direct3D12VDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {
    m_impl->VDP2WriteVRAM(address, value);
}

void Direct3D12VDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {
    m_impl->VDP2WriteCRAM(address, value);
}

void Direct3D12VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {
    m_impl->VDP2WriteCRAM(address, value);
}

void Direct3D12VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    m_impl->VDP2WriteReg(address, value);
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
