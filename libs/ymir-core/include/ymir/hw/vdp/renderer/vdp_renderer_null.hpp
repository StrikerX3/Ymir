#pragma once

#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

namespace ymir::vdp {

class NullVDPRenderer : public IVDPRenderer {
public:
    NullVDPRenderer()
        : IVDPRenderer(VDPRendererType::Null) {}

    // -------------------------------------------------------------------------
    // Basics

protected:
    void ResetImpl(bool hard) override {}

public:
    // -------------------------------------------------------------------------
    // Configuration

    void ConfigureEnhancements(const config::Enhancements &enhancements) override {}

    // -------------------------------------------------------------------------
    // Save states

    void PreSaveStateSync() override {}
    void PostLoadStateSync() override {}

    void SaveState(state::VDPState::VDPRendererState &state) override {}
    bool ValidateState(const state::VDPState::VDPRendererState &state) const override {
        return true;
    }
    void LoadState(const state::VDPState::VDPRendererState &state) override {}

    // -------------------------------------------------------------------------
    // VDP1 memory and register writes

    void VDP1WriteVRAM(uint32 address, uint8 value) override {}
    void VDP1WriteVRAM(uint32 address, uint16 value) override {}
    void VDP1WriteFB(uint32 address, uint8 value) override {}
    void VDP1WriteFB(uint32 address, uint16 value) override {}
    void VDP1WriteReg(uint32 address, uint16 value) override {}

    // -------------------------------------------------------------------------
    // VDP2 memory and register writes

    void VDP2WriteVRAM(uint32 address, uint8 value) override {}
    void VDP2WriteVRAM(uint32 address, uint16 value) override {}
    void VDP2WriteCRAM(uint32 address, uint8 value) override {}
    void VDP2WriteCRAM(uint32 address, uint16 value) override {}
    void VDP2WriteReg(uint32 address, uint16 value) override {}

    // -------------------------------------------------------------------------
    // Debugger

    void UpdateEnabledLayers() override {}

    // -------------------------------------------------------------------------
    // Utilities

    void DumpExtraVDP1Framebuffers(std::ostream &out) const override {}

    // -------------------------------------------------------------------------
    // Rendering process

    void VDP1EraseFramebuffer(uint64 cycles) override {}
    void VDP1SwapFramebuffer() override {}
    void VDP1BeginFrame() override {}
    void VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) override {}
    void VDP1EndFrame() override {}

    void VDP2SetResolution(uint32 h, uint32 v, bool exclusive) override {}
    void VDP2SetField(bool odd) override {}
    void VDP2LatchTVMD() override {}
    void VDP2BeginFrame() override {}
    void VDP2RenderLine(uint32 y) override {}
    void VDP2EndFrame() override {}
};

} // namespace ymir::vdp
