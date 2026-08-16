#include <ymir/hw/vdp/renderer/vdp_renderer_hw_d3d12.hpp>

#include <ymir/gpu/d3d12/d3d12_commands.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap.hpp>
#include <ymir/gpu/d3d12/d3d12_descriptor_heap_allocator.hpp>
#include <ymir/gpu/d3d12/d3d12_device.hpp>
#include <ymir/gpu/d3d12/d3d12_fence.hpp>
#include <ymir/gpu/d3d12/d3d12_pipeline_state.hpp>
#include <ymir/gpu/d3d12/d3d12_resource.hpp>
#include <ymir/gpu/d3d12/d3d12_root_signature.hpp>

#include <d3d12.h>

#include <fmt/format.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_core_shaders);

using namespace ymir::gpu::d3d12;

namespace ymir::vdp {

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

        // VDP2 CRAM buffer
        {
            auto builder = vdp2.bufCRAM.BufferBuilder(vdp::kVDP2CRAMSize);
            if (HRESULT hr = builder.BuildCommitted(device); FAILED(hr)) {
                return util::ErrorMessage{
                    fmt::format("Could not create raw VDP2 CRAM buffer, error code {:X}", (uint32)hr)};
            }
            vdp2.bufCRAM->SetName(L"[Ymir-VDP2] Raw CRAM buffer");

            if (!resourceHeapAlloc.Allocate(vdp2.srvCRAM)) {
                return util::ErrorMessage{"Could not allocate raw VDP2 CRAM buffer SRV"};
            }

            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
                .Format = DXGI_FORMAT_R32_TYPELESS,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer =
                    {
                        .FirstElement = 0,
                        .NumElements = vdp::kVDP2CRAMSize / sizeof(uint32),
                        .StructureByteStride = 0,
                        .Flags = D3D12_BUFFER_SRV_FLAG_RAW,
                    },
            };
            device->CreateShaderResourceView(vdp2.bufCRAM.GetPointer(), &srvDesc, vdp2.srvCRAM.cpuHandle);
        }

        return util::ErrorMessage{"Unimplemented"};
    }

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

        /// @brief Raw VRAM data buffer.
        D3D12Resource bufVRAM;
        /// @brief Raw VRAM data buffer SRV.
        Descriptor srvVRAM;

        /// @brief Raw CRAM data
        D3D12Resource bufCRAM;
        /// @brief Raw CRAM data buffer SRV.
        Descriptor srvCRAM;
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
