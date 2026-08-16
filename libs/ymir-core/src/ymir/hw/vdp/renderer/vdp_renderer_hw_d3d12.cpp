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

#include <d3d12.h>

#include <fmt/format.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_core_shaders);

using namespace ymir::gpu::d3d12;

namespace ymir::vdp {

cmrc::embedded_filesystem g_fsShaders = cmrc::ymir_core_shaders::get_filesystem();

template <gpu::ShaderStage stage>
D3D12_SHADER_BYTECODE ToShaderBytecode(const gpu::CompiledShader<stage> &shader) {
    return D3D12_SHADER_BYTECODE{
        .pShaderBytecode = shader.bytecode.data(),
        .BytecodeLength = shader.bytecode.size(),
    };
}

struct Direct3D12VDPRenderer::Impl {
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
                    fmt::format("Could not create VDP renderer resource heap, error code {:X}", (uint32)hr)};
            }
            resourceHeap->SetName(L"[Ymir-VDP] Resource heap");
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
            auto builder = vdp2.bufVRAM.BufferBuilder(vdp::kVDP2VRAMSize);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create raw VDP2 VRAM buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.bufVRAM->SetName(L"[Ymir-VDP2] Raw VRAM buffer");

            if (!resourceHeapAlloc.Allocate(vdp2.srvVRAM)) {
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
            device->CreateShaderResourceView(vdp2.bufVRAM.GetPointer(), &srvDesc, vdp2.srvVRAM.cpuHandle);
        }

        // VDP2 CRAM color buffer
        {
            // As a reminder, here are the supported color RAM modes and formats:
            //   0 = RGB 5:5:5, 1024 words
            //   1 = RGB 5:5:5, 2048 words
            //   2 = RGB 8:8:8, 1024 words
            //   3 = RGB 8:8:8, 1024 words  (same as mode 2, undocumented)
            static constexpr UINT kMaxNumColors = vdp::kVDP2CRAMSize / sizeof(uint16);

            auto builder = vdp2.bufCRAMColor.BufferBuilder(kMaxNumColors * sizeof(uint32));
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create VDP2 CRAM color buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.bufCRAMColor->SetName(L"[Ymir-VDP2] CRAM color buffer");

            if (!resourceHeapAlloc.Allocate(vdp2.srvCRAMColor)) {
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
            device->CreateShaderResourceView(vdp2.bufCRAMColor.GetPointer(), &srvDesc, vdp2.srvCRAMColor.cpuHandle);
        }

        // VDP2 CRAM rotation coefficients buffer
        {
            // The second half of CRAM can be used as rotation coefficients.
            static constexpr UINT kCRAMRotCoeffSize = vdp::kVDP2CRAMSize / 2;

            auto builder = vdp2.bufCRAMRotCoeff.BufferBuilder(kCRAMRotCoeffSize * sizeof(uint32));
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not create VDP2 CRAM rotation coefficients buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.bufCRAMRotCoeff->SetName(L"[Ymir-VDP2] CRAM rotation coefficients buffer");

            if (!resourceHeapAlloc.Allocate(vdp2.srvCRAMRotCoeff)) {
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
            device->CreateShaderResourceView(vdp2.bufCRAMRotCoeff.GetPointer(), &srvDesc,
                                             vdp2.srvCRAMRotCoeff.cpuHandle);
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

            auto builder = vdp2.texLayerOut.Texture2DBuilder(vdp::kMaxResH, vdp::kMaxResV, kNumLayers);
            builder.Format(kFormat);
            builder.Flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create layer outputs texture array, error code {:X}", (uint32)hr)};
            }
            vdp2.texLayerOut->SetName(L"[Ymir-VDP2] Layer outputs array");

            if (!resourceHeapAlloc.Allocate(vdp2.srvLayerOut)) {
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
            device->CreateShaderResourceView(vdp2.texLayerOut.GetPointer(), &srvDesc, vdp2.srvLayerOut.cpuHandle);

            if (!resourceHeapAlloc.Allocate(vdp2.uavLayerOut)) {
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
            device->CreateUnorderedAccessView(vdp2.texLayerOut.GetPointer(), nullptr, &uavDesc,
                                              vdp2.uavLayerOut.cpuHandle);
        }

        // Draw background layers compute shader, root signature and pipeline state object
        {
            auto shaderBlobResult = LoadShader("vdp/vdp2_render_bgs_cs.cso");
            if (!shaderBlobResult) {
                return util::ErrorMessage{
                    fmt::format("Could not load VDP2 background layer rendering compute shader: {}",
                                shaderBlobResult.Error().message)};
            }
            vdp2.csDrawBGs.format = gpu::ShaderBytecodeFormat::DXIL;
            vdp2.csDrawBGs.bytecode = shaderBlobResult.Value();
            vdp2.csDrawBGs.entrypoint = kCSEntrypoint;
            auto result = gpu::ValidateShader(vdp2.csDrawBGs);
            if (!result) {
                return util::ErrorMessage{fmt::format(
                    "VDP2 background layer rendering compute shader validation failed: {}", result.Error().message)};
            }

            auto rootSigBuilder = vdp2.rootSigDrawBGs.Builder();
            // TODO: add parameters as needed
            rootSigBuilder.Add32BitConstants(0, 1); // TODO: rendering parameters
            rootSigBuilder.AddDescriptorTable().AddUAVs(1, 0);
            if (HRESULT hr = rootSigBuilder.Build(device); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not build VDP2 background layer rendering root signature, error code {:X}", (uint32)hr)};
            }
            vdp2.rootSigDrawBGs->SetName(L"[Ymir-VDP2] Background layer rendering root signature");

            const D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
                .pRootSignature = vdp2.rootSigDrawBGs.GetPointer(),
                .CS = ToShaderBytecode(vdp2.csDrawBGs),
            };
            if (HRESULT hr = vdp2.psoDrawBGs.CreateCompute(device, psoDesc); FAILED(hr)) {
                return util::ErrorMessage{fmt::format(
                    "Could not build VDP2 background layer rendering pipeline state object, error code {:X}",
                    (uint32)hr)};
            }
            vdp2.psoDrawBGs->SetName(L"[Ymir-VDP2] Background layer rendering pipeline state object");
        }

        return util::ErrorMessage{"Unimplemented"};
    }

    util::ValueResult<std::vector<char>> LoadShader(const char *path) {
        if (!g_fsShaders.is_file(path)) {
            return util::ErrorMessage{fmt::format("Embedded file not found: {}", path)};
        }
        auto file = g_fsShaders.open(path);
        return std::vector<char>{file.begin(), file.end()};
    }

    static constexpr const char *kCSEntrypoint = "CSMain";

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

    struct VDP1 {
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

    struct VDP2 {
        /// @brief VDP2 command allocator.
        D3D12CommandAllocator cmdAlloc;
        /// @brief VDP2 command list.
        D3D12GraphicsCommandList cmdList;

        // VDP2 VRAM is exposed as a ByteAddressBuffer to shaders as they often need to access raw bytes in 8-bit,
        // 16-bit and 32-bit formats.

        /// @brief Raw VRAM data buffer.
        D3D12Resource bufVRAM;
        /// @brief Raw VRAM data buffer SRV.
        Descriptor srvVRAM;

        // VDP2 CRAM is not directly exposed. Instead, shaders get two convenient views:
        // - CRAM converted to R8G8B8A8 colors based on the current color RAM mode
        // - Top half of raw CRAM bytes, for rotation coefficients

        /// @brief CRAM color buffer.
        D3D12Resource bufCRAMColor;
        /// @brief CRAM color buffer SRV.
        Descriptor srvCRAMColor;

        /// @brief Raw CRAM rotation coefficients buffer.
        D3D12Resource bufCRAMRotCoeff;
        /// @brief Raw CRAM rotation coefficients buffer SRV.
        Descriptor srvCRAMRotCoeff;

        // LayerOut contains the intermediate per-layer outputs of the VDP2 rendering process.

        /// @brief 2D texture array for the outputs of NBG0-3, RBG0-1, sprite and mesh layers (in that order).
        D3D12Resource texLayerOut;
        /// @brief Layer outputs SRV.
        Descriptor srvLayerOut;
        /// @brief Layer outputs UAV.
        Descriptor uavLayerOut;

        // ---------------------------------------------------------------------

        /// @brief Compute shader for drawing background layers.
        gpu::ComputeShader csDrawBGs;
        /// @brief Root signature for drawing background layers.
        D3D12RootSignature rootSigDrawBGs;
        /// @brief Pipeline state object for drawing background layers.
        D3D12PipelineState psoDrawBGs;
    } vdp2;
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

void Direct3D12VDPRenderer::VDP2BeginFrame() {
    // TODO: prepare new VDP2 frame
}

void Direct3D12VDPRenderer::VDP2RenderLine(uint32 y) {
    // TODO: prepare next line, render and compose lines; optimize by batching lines without state changes
}

void Direct3D12VDPRenderer::VDP2EndFrame() {
    // TODO: finish VDP2 frame
    Callbacks.VDP2DrawFinished();
}

} // namespace ymir::vdp
