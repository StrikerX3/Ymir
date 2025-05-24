#include <ymir/hw/vdp/vdp.hpp>

#include "slope.hpp"

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/constexpr_for.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/scope_guard.hpp>
#include <ymir/util/thread_name.hpp>
#include <ymir/util/unreachable.hpp>

#include <cassert>

namespace ymir::vdp {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   vdp1
    //     vdp1_regs
    //     vdp1_render
    //   vdp2
    //     vdp2_regs
    //     vdp2_render

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "VDP";
    };

    struct vdp1 : public base {
        static constexpr std::string_view name = "VDP1";
    };

    struct vdp1_regs : public vdp1 {
        static constexpr std::string_view name = "VDP1-Regs";
    };

    struct vdp1_render : public vdp1 {
        static constexpr std::string_view name = "VDP1-Render";
    };

    struct vdp2 : public base {
        static constexpr std::string_view name = "VDP2";
    };

    struct vdp2_regs : public vdp2 {
        // static constexpr devlog::Level level = devlog::level::trace;
        static constexpr std::string_view name = "VDP2-Regs";
    };

    struct vdp2_render : public vdp2 {
        // static constexpr devlog::Level level = devlog::level::trace;
        static constexpr std::string_view name = "VDP2-Render";
    };

} // namespace grp

VDP::VDP(core::Scheduler &scheduler, core::Configuration &config)
    : m_scheduler(scheduler) {

    config.system.videoStandard.Observe([&](VideoStandard videoStandard) { SetVideoStandard(videoStandard); });
    config.video.threadedVDP.Observe([&](bool value) { EnableThreadedVDP(value); });

    m_phaseUpdateEvent = scheduler.RegisterEvent(core::events::VDPPhase, this, OnPhaseUpdateEvent);

    Reset(true);
}

VDP::~VDP() {
    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::Shutdown());
        if (m_VDPRenderThread.joinable()) {
            m_VDPRenderThread.join();
        }
    }
}

void VDP::Reset(bool hard) {
    if (hard) {
        for (uint32 addr = 0; addr < m_VRAM1.size(); addr++) {
            if ((addr & 0x1F) == 0) {
                m_VRAM1[addr] = 0x80;
            } else if ((addr & 0x1F) == 1) {
                m_VRAM1[addr] = 0x00;
            } else if ((addr & 2) == 2) {
                m_VRAM1[addr] = 0x55;
            } else {
                m_VRAM1[addr] = 0xAA;
            }
        }

        m_VRAM2.fill(0);
        m_CRAM.fill(0);
        m_CRAMCache.fill({});
        for (auto &fb : m_spriteFB) {
            fb.fill(0);
        }
        m_displayFB = 0;
    }

    m_VDP1.Reset();
    m_VDP2.Reset();

    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::Reset());
    }

    m_HPhase = HorizontalPhase::Active;
    m_VPhase = VerticalPhase::Active;
    m_VCounter = 0;
    m_HRes = 320;
    m_VRes = 224;

    m_VDP1RenderContext.Reset();

    for (auto &state : m_layerStates) {
        state.Reset();
    }
    m_spriteLayerState.Reset();
    for (auto &state : m_normBGLayerStates) {
        state.Reset();
    }
    for (auto &state : m_rotParamStates) {
        state.Reset();
    }
    m_lineBackLayerState.Reset();

    BeginHPhaseActiveDisplay();
    BeginVPhaseActiveDisplay();

    UpdateResolution<false>();

    m_scheduler.ScheduleFromNow(m_phaseUpdateEvent, GetPhaseCycles());
}

void VDP::MapMemory(sys::Bus &bus) {
    static constexpr auto cast = [](void *ctx) -> VDP & { return *static_cast<VDP *>(ctx); };

    // VDP1 VRAM
    bus.MapBoth(
        0x5C0'0000, 0x5C7'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP1ReadVRAM<uint8>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP1ReadVRAM<uint16>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP1ReadVRAM<uint16>(address + 0) << 16u;
            value |= cast(ctx).VDP1ReadVRAM<uint16>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP1WriteVRAM<uint8>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteVRAM<uint16>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteVRAM<uint16>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteVRAM<uint16>(address + 2, value >> 0u);
        });

    // VDP1 framebuffer
    bus.MapBoth(
        0x5C8'0000, 0x5CF'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP1ReadFB<uint8>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP1ReadFB<uint16>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP1ReadFB<uint16>(address + 0) << 16u;
            value |= cast(ctx).VDP1ReadFB<uint16>(address + 2) << 0u;
            return value;
        },

        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP1WriteFB<uint8>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteFB<uint16>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteFB<uint16>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteFB<uint16>(address + 2, value >> 0u);
        });

    // VDP1 registers
    bus.MapNormal(
        0x5D0'0000, 0x5D7'FFFF, this,
        [](uint32 address, void * /*ctx*/) -> uint8 {
            address &= 0x7FFFF;
            devlog::debug<grp::vdp1_regs>("Illegal 8-bit VDP1 register read from {:05X}", address);
            return 0;
        },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP1ReadReg<false>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP1ReadReg<false>(address + 0) << 16u;
            value |= cast(ctx).VDP1ReadReg<false>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void * /*ctx*/) {
            address &= 0x7FFFF;
            devlog::debug<grp::vdp1_regs>("Illegal 8-bit VDP1 register write to {:05X} = {:02X}", address, value);
        },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteReg<false>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteReg<false>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteReg<false>(address + 2, value >> 0u);
        });

    bus.MapSideEffectFree(
        0x5D0'0000, 0x5D7'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 {
            const uint16 value = cast(ctx).VDP1ReadReg<true>(address);
            return value >> ((~address & 1) * 8u);
        },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP1ReadReg<true>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP1ReadReg<true>(address + 0) << 16u;
            value |= cast(ctx).VDP1ReadReg<true>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) {
            uint16 currValue = cast(ctx).VDP1ReadReg<true>(address & ~1);
            const uint16 shift = (~address & 1) * 8u;
            const uint16 mask = ~(0xFF << shift);
            currValue = (currValue & mask) | (value << shift);
            cast(ctx).VDP1WriteReg<true>(address & ~1, currValue);
        },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteReg<true>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteReg<true>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteReg<true>(address + 2, value >> 0u);
        });

    // VDP2 VRAM
    bus.MapBoth(
        0x5E0'0000, 0x5EF'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP2ReadVRAM<uint8>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadVRAM<uint16>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadVRAM<uint16>(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadVRAM<uint16>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP2WriteVRAM<uint8>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP2WriteVRAM<uint16>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP2WriteVRAM<uint16>(address + 0, value >> 16u);
            cast(ctx).VDP2WriteVRAM<uint16>(address + 2, value >> 0u);
        });

    // VDP2 CRAM
    bus.MapNormal(
        0x5F0'0000, 0x5F7'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP2ReadCRAM<uint8, false>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadCRAM<uint16, false>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadCRAM<uint16, false>(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadCRAM<uint16, false>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP2WriteCRAM<uint8, false>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP2WriteCRAM<uint16, false>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP2WriteCRAM<uint16, false>(address + 0, value >> 16u);
            cast(ctx).VDP2WriteCRAM<uint16, false>(address + 2, value >> 0u);
        });

    bus.MapSideEffectFree(
        0x5F0'0000, 0x5F7'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).VDP2ReadCRAM<uint8, true>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadCRAM<uint16, true>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadCRAM<uint16, true>(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadCRAM<uint16, true>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP2WriteCRAM<uint8, true>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP2WriteCRAM<uint16, true>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP2WriteCRAM<uint16, true>(address + 0, value >> 16u);
            cast(ctx).VDP2WriteCRAM<uint16, true>(address + 2, value >> 0u);
        });

    // VDP2 registers
    bus.MapBoth(
        0x5F8'0000, 0x5FB'FFFF, this,
        [](uint32 address, void * /*ctx*/) -> uint8 {
            address &= 0x1FF;
            devlog::debug<grp::vdp1_regs>("Illegal 8-bit VDP2 register read from {:05X}", address);
            return 0;
        },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadReg(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadReg(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadReg(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void * /*ctx*/) {
            address &= 0x1FF;
            devlog::debug<grp::vdp1_regs>("Illegal 8-bit VDP2 register write to {:05X} = {:02X}", address, value);
        },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP2WriteReg(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP2WriteReg(address + 0, value >> 16u);
            cast(ctx).VDP2WriteReg(address + 2, value >> 0u);
        });

    bus.MapSideEffectFree(
        0x5F8'0000, 0x5FB'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 {
            const uint16 value = cast(ctx).VDP2ReadReg(address);
            return value >> ((~address & 1) * 8u);
        },
        [](uint32 address, uint8 value, void *ctx) {
            uint16 currValue = cast(ctx).VDP2ReadReg(address & ~1);
            const uint16 shift = (~address & 1) * 8u;
            const uint16 mask = ~(0xFF << shift);
            currValue = (currValue & mask) | (value << shift);
            cast(ctx).VDP2WriteReg(address & ~1, currValue);
        });
}

template <bool debug>
void VDP::Advance(uint64 cycles) {
    if (!m_threadedVDPRendering) {
        // HACK: slow down VDP1 commands to avoid FMV freezes on Virtua Racing
        // TODO: use this counter in the threaded renderer
        // TODO: proper cycle counting
        static constexpr uint64 kCyclesPerCommand = 12;

        m_VDP1RenderContext.cycleCount += cycles;
        const uint64 steps = m_VDP1RenderContext.cycleCount / kCyclesPerCommand;
        m_VDP1RenderContext.cycleCount %= kCyclesPerCommand;

        for (uint64 i = 0; i < steps; i++) {
            VDP1ProcessCommand();
        }
    }
}

template void VDP::Advance<false>(uint64 cycles);
template void VDP::Advance<true>(uint64 cycles);

void VDP::DumpVDP1VRAM(std::ostream &out) const {
    out.write((const char *)m_VRAM1.data(), m_VRAM1.size());
}

void VDP::DumpVDP2VRAM(std::ostream &out) const {
    out.write((const char *)m_VRAM2.data(), m_VRAM2.size());
}

void VDP::DumpVDP2CRAM(std::ostream &out) const {
    out.write((const char *)m_CRAM.data(), m_CRAM.size());
}

void VDP::DumpVDP1Framebuffers(std::ostream &out) const {
    out.write((const char *)m_spriteFB[m_displayFB ^ 1].data(), m_spriteFB[m_displayFB ^ 1].size());
    out.write((const char *)m_spriteFB[m_displayFB].data(), m_spriteFB[m_displayFB].size());
}

void VDP::SaveState(state::VDPState &state) const {
    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::PreSaveStateSync());
        m_VDPRenderContext.preSaveSyncSignal.Wait(true);
    }

    state.VRAM1 = m_VRAM1;
    state.VRAM2 = m_VRAM2;
    state.CRAM = m_CRAM;
    state.spriteFB = m_spriteFB;
    state.displayFB = m_displayFB;

    state.regs1.TVMR = m_VDP1.ReadTVMR();
    state.regs1.FBCR = m_VDP1.ReadFBCR();
    state.regs1.PTMR = m_VDP1.ReadPTMR();
    state.regs1.EWDR = m_VDP1.ReadEWDR();
    state.regs1.EWLR = m_VDP1.ReadEWLR();
    state.regs1.EWRR = m_VDP1.ReadEWRR();
    state.regs1.EDSR = m_VDP1.ReadEDSR();
    state.regs1.LOPR = m_VDP1.ReadLOPR();
    state.regs1.COPR = m_VDP1.ReadCOPR();
    state.regs1.MODR = m_VDP1.ReadMODR();
    state.regs1.manualSwap = m_VDP1.fbManualSwap;
    state.regs1.manualErase = m_VDP1.fbManualErase;

    state.regs2.TVMD = m_VDP2.ReadTVMD();
    state.regs2.EXTEN = m_VDP2.ReadEXTEN();
    state.regs2.TVSTAT = m_VDP2.ReadTVSTAT();
    state.regs2.VRSIZE = m_VDP2.ReadVRSIZE();
    state.regs2.HCNT = m_VDP2.ReadHCNT();
    state.regs2.VCNT = m_VDP2.ReadVCNT();
    state.regs2.RAMCTL = m_VDP2.ReadRAMCTL();
    state.regs2.CYCA0L = m_VDP2.ReadCYCA0L();
    state.regs2.CYCA0U = m_VDP2.ReadCYCA0U();
    state.regs2.CYCA1L = m_VDP2.ReadCYCA1L();
    state.regs2.CYCA1U = m_VDP2.ReadCYCA1U();
    state.regs2.CYCB0L = m_VDP2.ReadCYCB0L();
    state.regs2.CYCB0U = m_VDP2.ReadCYCB0U();
    state.regs2.CYCB1L = m_VDP2.ReadCYCB1L();
    state.regs2.CYCB1U = m_VDP2.ReadCYCB1U();
    state.regs2.BGON = m_VDP2.ReadBGON();
    state.regs2.MZCTL = m_VDP2.ReadMZCTL();
    state.regs2.SFSEL = m_VDP2.ReadSFSEL();
    state.regs2.SFCODE = m_VDP2.ReadSFCODE();
    state.regs2.CHCTLA = m_VDP2.ReadCHCTLA();
    state.regs2.CHCTLB = m_VDP2.ReadCHCTLB();
    state.regs2.BMPNA = m_VDP2.ReadBMPNA();
    state.regs2.BMPNB = m_VDP2.ReadBMPNB();
    state.regs2.PNCNA = m_VDP2.ReadPNCNA();
    state.regs2.PNCNB = m_VDP2.ReadPNCNB();
    state.regs2.PNCNC = m_VDP2.ReadPNCNC();
    state.regs2.PNCND = m_VDP2.ReadPNCND();
    state.regs2.PNCR = m_VDP2.ReadPNCR();
    state.regs2.PLSZ = m_VDP2.ReadPLSZ();
    state.regs2.MPOFN = m_VDP2.ReadMPOFN();
    state.regs2.MPOFR = m_VDP2.ReadMPOFR();
    state.regs2.MPABN0 = m_VDP2.ReadMPABN0();
    state.regs2.MPCDN0 = m_VDP2.ReadMPCDN0();
    state.regs2.MPABN1 = m_VDP2.ReadMPABN1();
    state.regs2.MPCDN1 = m_VDP2.ReadMPCDN1();
    state.regs2.MPABN2 = m_VDP2.ReadMPABN2();
    state.regs2.MPCDN2 = m_VDP2.ReadMPCDN2();
    state.regs2.MPABN3 = m_VDP2.ReadMPABN3();
    state.regs2.MPCDN3 = m_VDP2.ReadMPCDN3();
    state.regs2.MPABRA = m_VDP2.ReadMPABRA();
    state.regs2.MPCDRA = m_VDP2.ReadMPCDRA();
    state.regs2.MPEFRA = m_VDP2.ReadMPEFRA();
    state.regs2.MPGHRA = m_VDP2.ReadMPGHRA();
    state.regs2.MPIJRA = m_VDP2.ReadMPIJRA();
    state.regs2.MPKLRA = m_VDP2.ReadMPKLRA();
    state.regs2.MPMNRA = m_VDP2.ReadMPMNRA();
    state.regs2.MPOPRA = m_VDP2.ReadMPOPRA();
    state.regs2.MPABRB = m_VDP2.ReadMPABRB();
    state.regs2.MPCDRB = m_VDP2.ReadMPCDRB();
    state.regs2.MPEFRB = m_VDP2.ReadMPEFRB();
    state.regs2.MPGHRB = m_VDP2.ReadMPGHRB();
    state.regs2.MPIJRB = m_VDP2.ReadMPIJRB();
    state.regs2.MPKLRB = m_VDP2.ReadMPKLRB();
    state.regs2.MPMNRB = m_VDP2.ReadMPMNRB();
    state.regs2.MPOPRB = m_VDP2.ReadMPOPRB();
    state.regs2.SCXIN0 = m_VDP2.ReadSCXIN0();
    state.regs2.SCXDN0 = m_VDP2.ReadSCXDN0();
    state.regs2.SCYIN0 = m_VDP2.ReadSCYIN0();
    state.regs2.SCYDN0 = m_VDP2.ReadSCYDN0();
    state.regs2.ZMXIN0 = m_VDP2.ReadZMXIN0();
    state.regs2.ZMXDN0 = m_VDP2.ReadZMXDN0();
    state.regs2.ZMYIN0 = m_VDP2.ReadZMYIN0();
    state.regs2.ZMYDN0 = m_VDP2.ReadZMYDN0();
    state.regs2.SCXIN1 = m_VDP2.ReadSCXIN1();
    state.regs2.SCXDN1 = m_VDP2.ReadSCXDN1();
    state.regs2.SCYIN1 = m_VDP2.ReadSCYIN1();
    state.regs2.SCYDN1 = m_VDP2.ReadSCYDN1();
    state.regs2.ZMXIN1 = m_VDP2.ReadZMXIN1();
    state.regs2.ZMXDN1 = m_VDP2.ReadZMXDN1();
    state.regs2.ZMYIN1 = m_VDP2.ReadZMYIN1();
    state.regs2.ZMYDN1 = m_VDP2.ReadZMYDN1();
    state.regs2.SCXIN2 = m_VDP2.ReadSCXN2();
    state.regs2.SCYIN2 = m_VDP2.ReadSCYN2();
    state.regs2.SCXIN3 = m_VDP2.ReadSCXN3();
    state.regs2.SCYIN3 = m_VDP2.ReadSCYN3();
    state.regs2.ZMCTL = m_VDP2.ReadZMCTL();
    state.regs2.SCRCTL = m_VDP2.ReadSCRCTL();
    state.regs2.VCSTAU = m_VDP2.ReadVCSTAU();
    state.regs2.VCSTAL = m_VDP2.ReadVCSTAL();
    state.regs2.LSTA0U = m_VDP2.ReadLSTA0U();
    state.regs2.LSTA0L = m_VDP2.ReadLSTA0L();
    state.regs2.LSTA1U = m_VDP2.ReadLSTA1U();
    state.regs2.LSTA1L = m_VDP2.ReadLSTA1L();
    state.regs2.LCTAU = m_VDP2.ReadLCTAU();
    state.regs2.LCTAL = m_VDP2.ReadLCTAL();
    state.regs2.BKTAU = m_VDP2.ReadBKTAU();
    state.regs2.BKTAL = m_VDP2.ReadBKTAL();
    state.regs2.RPMD = m_VDP2.ReadRPMD();
    state.regs2.RPRCTL = m_VDP2.ReadRPRCTL();
    state.regs2.KTCTL = m_VDP2.ReadKTCTL();
    state.regs2.KTAOF = m_VDP2.ReadKTAOF();
    state.regs2.OVPNRA = m_VDP2.ReadOVPNRA();
    state.regs2.OVPNRB = m_VDP2.ReadOVPNRB();
    state.regs2.RPTAU = m_VDP2.ReadRPTAU();
    state.regs2.RPTAL = m_VDP2.ReadRPTAL();
    state.regs2.WPSX0 = m_VDP2.ReadWPSX0();
    state.regs2.WPSY0 = m_VDP2.ReadWPSY0();
    state.regs2.WPEX0 = m_VDP2.ReadWPEX0();
    state.regs2.WPEY0 = m_VDP2.ReadWPEY0();
    state.regs2.WPSX1 = m_VDP2.ReadWPSX1();
    state.regs2.WPSY1 = m_VDP2.ReadWPSY1();
    state.regs2.WPEX1 = m_VDP2.ReadWPEX1();
    state.regs2.WPEY1 = m_VDP2.ReadWPEY1();
    state.regs2.WCTLA = m_VDP2.ReadWCTLA();
    state.regs2.WCTLB = m_VDP2.ReadWCTLB();
    state.regs2.WCTLC = m_VDP2.ReadWCTLC();
    state.regs2.WCTLD = m_VDP2.ReadWCTLD();
    state.regs2.LWTA0U = m_VDP2.ReadLWTA0U();
    state.regs2.LWTA0L = m_VDP2.ReadLWTA0L();
    state.regs2.LWTA1U = m_VDP2.ReadLWTA1U();
    state.regs2.LWTA1L = m_VDP2.ReadLWTA1L();
    state.regs2.SPCTL = m_VDP2.ReadSPCTL();
    state.regs2.SDCTL = m_VDP2.ReadSDCTL();
    state.regs2.CRAOFA = m_VDP2.ReadCRAOFA();
    state.regs2.CRAOFB = m_VDP2.ReadCRAOFB();
    state.regs2.LNCLEN = m_VDP2.ReadLNCLEN();
    state.regs2.SFPRMD = m_VDP2.ReadSFPRMD();
    state.regs2.CCCTL = m_VDP2.ReadCCCTL();
    state.regs2.SFCCMD = m_VDP2.ReadSFCCMD();
    state.regs2.PRISA = m_VDP2.ReadPRISA();
    state.regs2.PRISB = m_VDP2.ReadPRISB();
    state.regs2.PRISC = m_VDP2.ReadPRISC();
    state.regs2.PRISD = m_VDP2.ReadPRISD();
    state.regs2.PRINA = m_VDP2.ReadPRINA();
    state.regs2.PRINB = m_VDP2.ReadPRINB();
    state.regs2.PRIR = m_VDP2.ReadPRIR();
    state.regs2.CCRSA = m_VDP2.ReadCCRSA();
    state.regs2.CCRSB = m_VDP2.ReadCCRSB();
    state.regs2.CCRSC = m_VDP2.ReadCCRSC();
    state.regs2.CCRSD = m_VDP2.ReadCCRSD();
    state.regs2.CCRNA = m_VDP2.ReadCCRNA();
    state.regs2.CCRNB = m_VDP2.ReadCCRNB();
    state.regs2.CCRR = m_VDP2.ReadCCRR();
    state.regs2.CCRLB = m_VDP2.ReadCCRLB();
    state.regs2.CLOFEN = m_VDP2.ReadCLOFEN();
    state.regs2.CLOFSL = m_VDP2.ReadCLOFSL();
    state.regs2.COAR = m_VDP2.ReadCOAR();
    state.regs2.COAG = m_VDP2.ReadCOAG();
    state.regs2.COAB = m_VDP2.ReadCOAB();
    state.regs2.COBR = m_VDP2.ReadCOBR();
    state.regs2.COBG = m_VDP2.ReadCOBG();
    state.regs2.COBB = m_VDP2.ReadCOBB();

    switch (m_HPhase) {
    default:
    case HorizontalPhase::Active: state.HPhase = state::VDPState::HorizontalPhase::Active; break;
    case HorizontalPhase::RightBorder: state.HPhase = state::VDPState::HorizontalPhase::RightBorder; break;
    case HorizontalPhase::Sync: state.HPhase = state::VDPState::HorizontalPhase::Sync; break;
    case HorizontalPhase::VBlankOut: state.HPhase = state::VDPState::HorizontalPhase::VBlankOut; break;
    case HorizontalPhase::LeftBorder: state.HPhase = state::VDPState::HorizontalPhase::LeftBorder; break;
    case HorizontalPhase::LastDot: state.HPhase = state::VDPState::HorizontalPhase::LastDot; break;
    }

    switch (m_VPhase) {
    default:
    case VerticalPhase::Active: state.VPhase = state::VDPState::VerticalPhase::Active; break;
    case VerticalPhase::BottomBorder: state.VPhase = state::VDPState::VerticalPhase::BottomBorder; break;
    case VerticalPhase::BlankingAndSync: state.VPhase = state::VDPState::VerticalPhase::BlankingAndSync; break;
    case VerticalPhase::TopBorder: state.VPhase = state::VDPState::VerticalPhase::TopBorder; break;
    case VerticalPhase::LastLine: state.VPhase = state::VDPState::VerticalPhase::LastLine; break;
    }

    state.VCounter = m_VCounter;

    state.renderer.vdp1State.sysClipH = m_VDP1RenderContext.sysClipH;
    state.renderer.vdp1State.sysClipV = m_VDP1RenderContext.sysClipV;
    state.renderer.vdp1State.userClipX0 = m_VDP1RenderContext.userClipX0;
    state.renderer.vdp1State.userClipY0 = m_VDP1RenderContext.userClipY0;
    state.renderer.vdp1State.userClipX1 = m_VDP1RenderContext.userClipX1;
    state.renderer.vdp1State.userClipY1 = m_VDP1RenderContext.userClipY1;
    state.renderer.vdp1State.localCoordX = m_VDP1RenderContext.localCoordX;
    state.renderer.vdp1State.localCoordY = m_VDP1RenderContext.localCoordY;
    state.renderer.vdp1State.rendering = m_VDP1RenderContext.rendering;
    state.renderer.vdp1State.cycleCount = m_VDP1RenderContext.cycleCount;

    for (size_t i = 0; i < 4; i++) {
        state.renderer.normBGLayerStates[i].fracScrollX = m_normBGLayerStates[i].fracScrollX;
        state.renderer.normBGLayerStates[i].fracScrollY = m_normBGLayerStates[i].fracScrollY;
        state.renderer.normBGLayerStates[i].scrollIncH = m_normBGLayerStates[i].scrollIncH;
        state.renderer.normBGLayerStates[i].lineScrollTableAddress = m_normBGLayerStates[i].lineScrollTableAddress;
        state.renderer.normBGLayerStates[i].vertCellScrollOffset = m_normBGLayerStates[i].vertCellScrollOffset;
        state.renderer.normBGLayerStates[i].mosaicCounterY = m_normBGLayerStates[i].mosaicCounterY;
    }

    for (size_t i = 0; i < 2; i++) {
        state.renderer.rotParamStates[i].pageBaseAddresses = m_rotParamStates[i].pageBaseAddresses;
        state.renderer.rotParamStates[i].scrX = m_rotParamStates[i].scrX;
        state.renderer.rotParamStates[i].scrY = m_rotParamStates[i].scrY;
        state.renderer.rotParamStates[i].KA = m_rotParamStates[i].KA;
    }

    state.renderer.lineBackLayerState.lineColor = m_lineBackLayerState.lineColor.u32;
    state.renderer.lineBackLayerState.backColor = m_lineBackLayerState.backColor.u32;
    state.renderer.vertCellScrollInc = m_vertCellScrollInc;

    state.renderer.displayFB = m_VDPRenderContext.displayFB;
    state.renderer.vdp1Done = m_VDPRenderContext.vdp1Done;
}

bool VDP::ValidateState(const state::VDPState &state) const {
    switch (state.HPhase) {
    case state::VDPState::HorizontalPhase::Active: break;
    case state::VDPState::HorizontalPhase::RightBorder: break;
    case state::VDPState::HorizontalPhase::Sync: break;
    case state::VDPState::HorizontalPhase::VBlankOut: break;
    case state::VDPState::HorizontalPhase::LeftBorder: break;
    case state::VDPState::HorizontalPhase::LastDot: break;
    default: return false;
    }

    switch (state.VPhase) {
    case state::VDPState::VerticalPhase::Active: break;
    case state::VDPState::VerticalPhase::BottomBorder: break;
    case state::VDPState::VerticalPhase::BlankingAndSync: break;
    case state::VDPState::VerticalPhase::TopBorder: break;
    case state::VDPState::VerticalPhase::LastLine: break;
    default: return false;
    }

    return true;
}

void VDP::LoadState(const state::VDPState &state) {
    m_VRAM1 = state.VRAM1;
    m_VRAM2 = state.VRAM2;
    m_CRAM = state.CRAM;
    m_spriteFB = state.spriteFB;
    m_displayFB = state.displayFB;

    m_VDP1.WriteTVMR(state.regs1.TVMR);
    m_VDP1.WriteFBCR(state.regs1.FBCR);
    m_VDP1.WritePTMR(state.regs1.PTMR);
    m_VDP1.WriteEWDR(state.regs1.EWDR);
    m_VDP1.WriteEWLR(state.regs1.EWLR);
    m_VDP1.WriteEWRR(state.regs1.EWRR);
    m_VDP1.WriteEDSR(state.regs1.EDSR);
    m_VDP1.WriteLOPR(state.regs1.LOPR);
    m_VDP1.WriteCOPR(state.regs1.COPR);
    m_VDP1.WriteMODR(state.regs1.MODR);
    m_VDP1.fbManualSwap = state.regs1.manualSwap;
    m_VDP1.fbManualErase = state.regs1.manualErase;

    m_VDP2.WriteTVMD(state.regs2.TVMD);
    m_VDP2.WriteEXTEN(state.regs2.EXTEN);
    m_VDP2.WriteTVSTAT(state.regs2.TVSTAT);
    m_VDP2.WriteVRSIZE(state.regs2.VRSIZE);
    m_VDP2.WriteHCNT(state.regs2.HCNT);
    m_VDP2.WriteVCNT(state.regs2.VCNT);
    m_VDP2.WriteRAMCTL(state.regs2.RAMCTL);
    m_VDP2.WriteCYCA0L(state.regs2.CYCA0L);
    m_VDP2.WriteCYCA0U(state.regs2.CYCA0U);
    m_VDP2.WriteCYCA1L(state.regs2.CYCA1L);
    m_VDP2.WriteCYCA1U(state.regs2.CYCA1U);
    m_VDP2.WriteCYCB0L(state.regs2.CYCB0L);
    m_VDP2.WriteCYCB0U(state.regs2.CYCB0U);
    m_VDP2.WriteCYCB1L(state.regs2.CYCB1L);
    m_VDP2.WriteCYCB1U(state.regs2.CYCB1U);
    m_VDP2.WriteBGON(state.regs2.BGON);
    m_VDP2.WriteMZCTL(state.regs2.MZCTL);
    m_VDP2.WriteSFSEL(state.regs2.SFSEL);
    m_VDP2.WriteSFCODE(state.regs2.SFCODE);
    m_VDP2.WriteCHCTLA(state.regs2.CHCTLA);
    m_VDP2.WriteCHCTLB(state.regs2.CHCTLB);
    m_VDP2.WriteBMPNA(state.regs2.BMPNA);
    m_VDP2.WriteBMPNB(state.regs2.BMPNB);
    m_VDP2.WritePNCNA(state.regs2.PNCNA);
    m_VDP2.WritePNCNB(state.regs2.PNCNB);
    m_VDP2.WritePNCNC(state.regs2.PNCNC);
    m_VDP2.WritePNCND(state.regs2.PNCND);
    m_VDP2.WritePNCR(state.regs2.PNCR);
    m_VDP2.WritePLSZ(state.regs2.PLSZ);
    m_VDP2.WriteMPOFN(state.regs2.MPOFN);
    m_VDP2.WriteMPOFR(state.regs2.MPOFR);
    m_VDP2.WriteMPABN0(state.regs2.MPABN0);
    m_VDP2.WriteMPCDN0(state.regs2.MPCDN0);
    m_VDP2.WriteMPABN1(state.regs2.MPABN1);
    m_VDP2.WriteMPCDN1(state.regs2.MPCDN1);
    m_VDP2.WriteMPABN2(state.regs2.MPABN2);
    m_VDP2.WriteMPCDN2(state.regs2.MPCDN2);
    m_VDP2.WriteMPABN3(state.regs2.MPABN3);
    m_VDP2.WriteMPCDN3(state.regs2.MPCDN3);
    m_VDP2.WriteMPABRA(state.regs2.MPABRA);
    m_VDP2.WriteMPCDRA(state.regs2.MPCDRA);
    m_VDP2.WriteMPEFRA(state.regs2.MPEFRA);
    m_VDP2.WriteMPGHRA(state.regs2.MPGHRA);
    m_VDP2.WriteMPIJRA(state.regs2.MPIJRA);
    m_VDP2.WriteMPKLRA(state.regs2.MPKLRA);
    m_VDP2.WriteMPMNRA(state.regs2.MPMNRA);
    m_VDP2.WriteMPOPRA(state.regs2.MPOPRA);
    m_VDP2.WriteMPABRB(state.regs2.MPABRB);
    m_VDP2.WriteMPCDRB(state.regs2.MPCDRB);
    m_VDP2.WriteMPEFRB(state.regs2.MPEFRB);
    m_VDP2.WriteMPGHRB(state.regs2.MPGHRB);
    m_VDP2.WriteMPIJRB(state.regs2.MPIJRB);
    m_VDP2.WriteMPKLRB(state.regs2.MPKLRB);
    m_VDP2.WriteMPMNRB(state.regs2.MPMNRB);
    m_VDP2.WriteMPOPRB(state.regs2.MPOPRB);
    m_VDP2.WriteSCXIN0(state.regs2.SCXIN0);
    m_VDP2.WriteSCXDN0(state.regs2.SCXDN0);
    m_VDP2.WriteSCYIN0(state.regs2.SCYIN0);
    m_VDP2.WriteSCYDN0(state.regs2.SCYDN0);
    m_VDP2.WriteZMXIN0(state.regs2.ZMXIN0);
    m_VDP2.WriteZMXDN0(state.regs2.ZMXDN0);
    m_VDP2.WriteZMYIN0(state.regs2.ZMYIN0);
    m_VDP2.WriteZMYDN0(state.regs2.ZMYDN0);
    m_VDP2.WriteSCXIN1(state.regs2.SCXIN1);
    m_VDP2.WriteSCXDN1(state.regs2.SCXDN1);
    m_VDP2.WriteSCYIN1(state.regs2.SCYIN1);
    m_VDP2.WriteSCYDN1(state.regs2.SCYDN1);
    m_VDP2.WriteZMXIN1(state.regs2.ZMXIN1);
    m_VDP2.WriteZMXDN1(state.regs2.ZMXDN1);
    m_VDP2.WriteZMYIN1(state.regs2.ZMYIN1);
    m_VDP2.WriteZMYDN1(state.regs2.ZMYDN1);
    m_VDP2.WriteSCXN2(state.regs2.SCXIN2);
    m_VDP2.WriteSCYN2(state.regs2.SCYIN2);
    m_VDP2.WriteSCXN3(state.regs2.SCXIN3);
    m_VDP2.WriteSCYN3(state.regs2.SCYIN3);
    m_VDP2.WriteZMCTL(state.regs2.ZMCTL);
    m_VDP2.WriteSCRCTL(state.regs2.SCRCTL);
    m_VDP2.WriteVCSTAU(state.regs2.VCSTAU);
    m_VDP2.WriteVCSTAL(state.regs2.VCSTAL);
    m_VDP2.WriteLSTA0U(state.regs2.LSTA0U);
    m_VDP2.WriteLSTA0L(state.regs2.LSTA0L);
    m_VDP2.WriteLSTA1U(state.regs2.LSTA1U);
    m_VDP2.WriteLSTA1L(state.regs2.LSTA1L);
    m_VDP2.WriteLCTAU(state.regs2.LCTAU);
    m_VDP2.WriteLCTAL(state.regs2.LCTAL);
    m_VDP2.WriteBKTAU(state.regs2.BKTAU);
    m_VDP2.WriteBKTAL(state.regs2.BKTAL);
    m_VDP2.WriteRPMD(state.regs2.RPMD);
    m_VDP2.WriteRPRCTL(state.regs2.RPRCTL);
    m_VDP2.WriteKTCTL(state.regs2.KTCTL);
    m_VDP2.WriteKTAOF(state.regs2.KTAOF);
    m_VDP2.WriteOVPNRA(state.regs2.OVPNRA);
    m_VDP2.WriteOVPNRB(state.regs2.OVPNRB);
    m_VDP2.WriteRPTAU(state.regs2.RPTAU);
    m_VDP2.WriteRPTAL(state.regs2.RPTAL);
    m_VDP2.WriteWPSX0(state.regs2.WPSX0);
    m_VDP2.WriteWPSY0(state.regs2.WPSY0);
    m_VDP2.WriteWPEX0(state.regs2.WPEX0);
    m_VDP2.WriteWPEY0(state.regs2.WPEY0);
    m_VDP2.WriteWPSX1(state.regs2.WPSX1);
    m_VDP2.WriteWPSY1(state.regs2.WPSY1);
    m_VDP2.WriteWPEX1(state.regs2.WPEX1);
    m_VDP2.WriteWPEY1(state.regs2.WPEY1);
    m_VDP2.WriteWCTLA(state.regs2.WCTLA);
    m_VDP2.WriteWCTLB(state.regs2.WCTLB);
    m_VDP2.WriteWCTLC(state.regs2.WCTLC);
    m_VDP2.WriteWCTLD(state.regs2.WCTLD);
    m_VDP2.WriteLWTA0U(state.regs2.LWTA0U);
    m_VDP2.WriteLWTA0L(state.regs2.LWTA0L);
    m_VDP2.WriteLWTA1U(state.regs2.LWTA1U);
    m_VDP2.WriteLWTA1L(state.regs2.LWTA1L);
    m_VDP2.WriteSPCTL(state.regs2.SPCTL);
    m_VDP2.WriteSDCTL(state.regs2.SDCTL);
    m_VDP2.WriteCRAOFA(state.regs2.CRAOFA);
    m_VDP2.WriteCRAOFB(state.regs2.CRAOFB);
    m_VDP2.WriteLNCLEN(state.regs2.LNCLEN);
    m_VDP2.WriteSFPRMD(state.regs2.SFPRMD);
    m_VDP2.WriteCCCTL(state.regs2.CCCTL);
    m_VDP2.WriteSFCCMD(state.regs2.SFCCMD);
    m_VDP2.WritePRISA(state.regs2.PRISA);
    m_VDP2.WritePRISB(state.regs2.PRISB);
    m_VDP2.WritePRISC(state.regs2.PRISC);
    m_VDP2.WritePRISD(state.regs2.PRISD);
    m_VDP2.WritePRINA(state.regs2.PRINA);
    m_VDP2.WritePRINB(state.regs2.PRINB);
    m_VDP2.WritePRIR(state.regs2.PRIR);
    m_VDP2.WriteCCRSA(state.regs2.CCRSA);
    m_VDP2.WriteCCRSB(state.regs2.CCRSB);
    m_VDP2.WriteCCRSC(state.regs2.CCRSC);
    m_VDP2.WriteCCRSD(state.regs2.CCRSD);
    m_VDP2.WriteCCRNA(state.regs2.CCRNA);
    m_VDP2.WriteCCRNB(state.regs2.CCRNB);
    m_VDP2.WriteCCRR(state.regs2.CCRR);
    m_VDP2.WriteCCRLB(state.regs2.CCRLB);
    m_VDP2.WriteCLOFEN(state.regs2.CLOFEN);
    m_VDP2.WriteCLOFSL(state.regs2.CLOFSL);
    m_VDP2.WriteCOAR(state.regs2.COAR);
    m_VDP2.WriteCOAG(state.regs2.COAG);
    m_VDP2.WriteCOAB(state.regs2.COAB);
    m_VDP2.WriteCOBR(state.regs2.COBR);
    m_VDP2.WriteCOBG(state.regs2.COBG);
    m_VDP2.WriteCOBB(state.regs2.COBB);

    m_VDP2.cyclePatterns.dirty = true;

    switch (state.HPhase) {
    default:
    case state::VDPState::HorizontalPhase::Active: m_HPhase = HorizontalPhase::Active; break;
    case state::VDPState::HorizontalPhase::RightBorder: m_HPhase = HorizontalPhase::RightBorder; break;
    case state::VDPState::HorizontalPhase::Sync: m_HPhase = HorizontalPhase::Sync; break;
    case state::VDPState::HorizontalPhase::VBlankOut: m_HPhase = HorizontalPhase::VBlankOut; break;
    case state::VDPState::HorizontalPhase::LeftBorder: m_HPhase = HorizontalPhase::LeftBorder; break;
    case state::VDPState::HorizontalPhase::LastDot: m_HPhase = HorizontalPhase::LastDot; break;
    }

    switch (state.VPhase) {
    default:
    case state::VDPState::VerticalPhase::Active: m_VPhase = VerticalPhase::Active; break;
    case state::VDPState::VerticalPhase::BottomBorder: m_VPhase = VerticalPhase::BottomBorder; break;
    case state::VDPState::VerticalPhase::BlankingAndSync: m_VPhase = VerticalPhase::BlankingAndSync; break;
    case state::VDPState::VerticalPhase::TopBorder: m_VPhase = VerticalPhase::TopBorder; break;
    case state::VDPState::VerticalPhase::LastLine: m_VPhase = VerticalPhase::LastLine; break;
    }

    m_VCounter = state.VCounter;

    for (uint32 address = 0; address < kVDP2CRAMSize; address += 2) {
        VDP2UpdateCRAMCache<uint16>(address);
    }
    VDP2UpdateEnabledBGs();

    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::PostLoadStateSync());
        m_VDPRenderContext.postLoadSyncSignal.Wait(true);
    }

    m_VDP1RenderContext.sysClipH = state.renderer.vdp1State.sysClipH;
    m_VDP1RenderContext.sysClipV = state.renderer.vdp1State.sysClipV;
    m_VDP1RenderContext.userClipX0 = state.renderer.vdp1State.userClipX0;
    m_VDP1RenderContext.userClipY0 = state.renderer.vdp1State.userClipY0;
    m_VDP1RenderContext.userClipX1 = state.renderer.vdp1State.userClipX1;
    m_VDP1RenderContext.userClipY1 = state.renderer.vdp1State.userClipY1;
    m_VDP1RenderContext.localCoordX = state.renderer.vdp1State.localCoordX;
    m_VDP1RenderContext.localCoordY = state.renderer.vdp1State.localCoordY;
    m_VDP1RenderContext.rendering = state.renderer.vdp1State.rendering;
    m_VDP1RenderContext.cycleCount = state.renderer.vdp1State.cycleCount;

    for (size_t i = 0; i < 4; i++) {
        m_normBGLayerStates[i].fracScrollX = state.renderer.normBGLayerStates[i].fracScrollX;
        m_normBGLayerStates[i].fracScrollY = state.renderer.normBGLayerStates[i].fracScrollY;
        m_normBGLayerStates[i].scrollIncH = state.renderer.normBGLayerStates[i].scrollIncH;
        m_normBGLayerStates[i].lineScrollTableAddress = state.renderer.normBGLayerStates[i].lineScrollTableAddress;
        m_normBGLayerStates[i].mosaicCounterY = state.renderer.normBGLayerStates[i].mosaicCounterY;
    }

    for (size_t i = 0; i < 2; i++) {
        m_rotParamStates[i].pageBaseAddresses = state.renderer.rotParamStates[i].pageBaseAddresses;
        m_rotParamStates[i].scrX = state.renderer.rotParamStates[i].scrX;
        m_rotParamStates[i].scrY = state.renderer.rotParamStates[i].scrY;
        m_rotParamStates[i].KA = state.renderer.rotParamStates[i].KA;
    }

    m_lineBackLayerState.lineColor.u32 = state.renderer.lineBackLayerState.lineColor;
    m_lineBackLayerState.backColor.u32 = state.renderer.lineBackLayerState.backColor;

    m_VDPRenderContext.displayFB = state.renderer.displayFB;
    m_VDPRenderContext.vdp1Done = state.renderer.vdp1Done;

    UpdateResolution<true>();
}

void VDP::SetLayerEnabled(Layer layer, bool enabled) {
    m_layerStates[static_cast<size_t>(layer)].rendered = enabled;
    VDP2UpdateEnabledBGs();
}

bool VDP::IsLayerEnabled(Layer layer) const {
    return m_layerStates[static_cast<size_t>(layer)].rendered;
}

void VDP::OnPhaseUpdateEvent(core::EventContext &eventContext, void *userContext) {
    auto &vdp = *static_cast<VDP *>(userContext);
    vdp.UpdatePhase();
    const uint64 cycles = vdp.GetPhaseCycles();
    eventContext.RescheduleFromPrevious(cycles);
}

void VDP::SetVideoStandard(VideoStandard videoStandard) {
    const bool pal = videoStandard == VideoStandard::PAL;
    if (m_VDP2.TVSTAT.PAL != pal) {
        m_VDP2.TVSTAT.PAL = pal;
        m_VDP2.TVMDDirty = true;
    }
}

void VDP::EnableThreadedVDP(bool enable) {
    if (m_threadedVDPRendering == enable) {
        return;
    }

    devlog::debug<grp::vdp2_render>("{} threaded VDP rendering", (enable ? "Enabling" : "Disabling"));

    m_threadedVDPRendering = enable;
    if (enable) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::PostLoadStateSync());
        m_VDPRenderThread = std::thread{[&] { VDPRenderThread(); }};
        m_VDPRenderContext.postLoadSyncSignal.Wait(true);
    } else {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::Shutdown());
        if (m_VDPRenderThread.joinable()) {
            m_VDPRenderThread.join();
        }

        VDPRenderEvent dummy{};
        while (m_VDPRenderContext.eventQueue.try_dequeue(dummy)) {
        }
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP1ReadVRAM(uint32 address) {
    address &= 0x7FFFF;
    return util::ReadBE<T>(&m_VRAM1[address]);
}

template <mem_primitive T>
FORCE_INLINE void VDP::VDP1WriteVRAM(uint32 address, T value) {
    address &= 0x7FFFF;
    util::WriteBE<T>(&m_VRAM1[address], value);
    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP1VRAMWrite<T>(address, value));
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP1ReadFB(uint32 address) {
    address &= 0x3FFFF;
    return util::ReadBE<T>(&m_spriteFB[m_displayFB ^ 1][address]);
}

template <mem_primitive T>
FORCE_INLINE void VDP::VDP1WriteFB(uint32 address, T value) {
    address &= 0x3FFFF;
    util::WriteBE<T>(&m_spriteFB[m_displayFB ^ 1][address], value);
    // if (m_threadedVDPRendering) {
    //     m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP1FBWrite<T>(address, value));
    // }
}

template <bool peek>
FORCE_INLINE uint16 VDP::VDP1ReadReg(uint32 address) {
    address &= 0x7FFFF;
    return m_VDP1.Read<peek>(address);
}

template <bool poke>
FORCE_INLINE void VDP::VDP1WriteReg(uint32 address, uint16 value) {
    address &= 0x7FFFF;
    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP1RegWrite(address, value));
    }
    m_VDP1.Write<poke>(address, value);

    switch (address) {
    case 0x00:
        if constexpr (!poke) {
            devlog::trace<grp::vdp1_regs>("Write to TVM={:d}{:d}{:d}", m_VDP1.hdtvEnable, m_VDP1.fbRotEnable,
                                          m_VDP1.pixel8Bits);
            devlog::trace<grp::vdp1_regs>("Write to VBE={:d}", m_VDP1.vblankErase);
        }
        break;
    case 0x02:
        if constexpr (!poke) {
            devlog::trace<grp::vdp1_regs>("Write to DIE={:d} DIL={:d}", m_VDP1.dblInterlaceEnable,
                                          m_VDP1.dblInterlaceDrawLine);
            devlog::trace<grp::vdp1_regs>("Write to FCM={:d} FCT={:d} manualswap={:d} manualerase={:d}",
                                          m_VDP1.fbSwapMode, m_VDP1.fbSwapTrigger, m_VDP1.fbManualSwap,
                                          m_VDP1.fbManualErase);
        }
        break;
    case 0x04:
        if constexpr (!poke) {
            devlog::trace<grp::vdp1_regs>("Write to PTM={:d}", m_VDP1.plotTrigger);
            if (m_VDP1.plotTrigger == 0b01) {
                VDP1BeginFrame();
            }
        }
        break;
    case 0x0C: // ENDR
        // TODO: schedule drawing termination after 30 cycles
        m_VDP1RenderContext.rendering = false;
        break;
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP2ReadVRAM(uint32 address) {
    // TODO: handle VRSIZE.VRAMSZ
    address &= 0x7FFFF;
    return util::ReadBE<T>(&m_VRAM2[address]);
}

template <mem_primitive T>
FORCE_INLINE void VDP::VDP2WriteVRAM(uint32 address, T value) {
    // TODO: handle VRSIZE.VRAMSZ
    address &= 0x7FFFF;
    util::WriteBE<T>(&m_VRAM2[address], value);
    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP2VRAMWrite<T>(address, value));
    }
}

template <mem_primitive T, bool peek>
FORCE_INLINE T VDP::VDP2ReadCRAM(uint32 address) {
    if constexpr (std::is_same_v<T, uint32>) {
        uint32 value = VDP2ReadCRAM<uint16, peek>(address + 0) << 16u;
        value |= VDP2ReadCRAM<uint16, peek>(address + 2) << 0u;
        return value;
    }

    address = MapCRAMAddress(address);
    T value = util::ReadBE<T>(&m_CRAM[address]);
    if constexpr (!peek) {
        devlog::trace<grp::vdp2_regs>("{}-bit VDP2 CRAM read from {:03X} = {:X}", sizeof(T) * 8, address, value);
    }
    return value;
}

template <mem_primitive T, bool poke>
FORCE_INLINE void VDP::VDP2WriteCRAM(uint32 address, T value) {
    if constexpr (std::is_same_v<T, uint32>) {
        VDP2WriteCRAM<uint16, poke>(address + 0, value >> 16u);
        VDP2WriteCRAM<uint16, poke>(address + 2, value >> 0u);
        return;
    }

    address = MapCRAMAddress(address);
    if constexpr (!poke) {
        devlog::trace<grp::vdp2_regs>("{}-bit VDP2 CRAM write to {:05X} = {:X}", sizeof(T) * 8, address, value);
    }
    util::WriteBE<T>(&m_CRAM[address], value);
    VDP2UpdateCRAMCache<T>(address);
    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP2CRAMWrite<T>(address, value));
    }
    if (m_VDP2.vramControl.colorRAMMode == 0) {
        if constexpr (!poke) {
            devlog::trace<grp::vdp2_regs>("   replicated to {:05X}", address ^ 0x800);
        }
        util::WriteBE<T>(&m_CRAM[address ^ 0x800], value);
        VDP2UpdateCRAMCache<T>(address);
        if (m_threadedVDPRendering) {
            m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP2CRAMWrite<T>(address ^ 0x800, value));
        }
    }
}

template <mem_primitive T>
FORCE_INLINE void VDP::VDP2UpdateCRAMCache(uint32 address) {
    address &= ~1;
    const Color555 color5{.u16 = util::ReadBE<uint16>(&m_CRAM[address])};
    m_CRAMCache[address / sizeof(uint16)] = ConvertRGB555to888(color5);
    if constexpr (std::is_same_v<T, uint32>) {
        const Color555 color5{.u16 = util::ReadBE<uint16>(&m_CRAM[address + 2])};
        m_CRAMCache[(address + 2) / sizeof(uint16)] = ConvertRGB555to888(color5);
    }
}

FORCE_INLINE uint16 VDP::VDP2ReadReg(uint32 address) {
    address &= 0x1FF;
    return m_VDP2.Read(address);
}

FORCE_INLINE void VDP::VDP2WriteReg(uint32 address, uint16 value) {
    address &= 0x1FF;
    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP2RegWrite(address, value));
    }
    m_VDP2.Write(address, value);
    devlog::trace<grp::vdp2_regs>("VDP2 register write to {:03X} = {:04X}", address, value);

    switch (address) {
    case 0x000:
        devlog::trace<grp::vdp2_regs>("TVMD write: {:04X} - HRESO={:d} VRESO={:d} LSMD={:d} BDCLMD={:d} DISP={:d}{}",
                                      m_VDP2.TVMD.u16, (uint16)m_VDP2.TVMD.HRESOn, (uint16)m_VDP2.TVMD.VRESOn,
                                      (uint16)m_VDP2.TVMD.LSMDn, (uint16)m_VDP2.TVMD.BDCLMD, (uint16)m_VDP2.TVMD.DISP,
                                      (m_VDP2.TVMDDirty ? " (dirty)" : ""));
        break;
    case 0x020: [[fallthrough]]; // BGON
    case 0x028: [[fallthrough]]; // CHCTLA
    case 0x02A:                  // CHCTLB
        VDP2UpdateEnabledBGs();
        break;
    }
}

FORCE_INLINE void VDP::UpdatePhase() {
    auto nextPhase = static_cast<uint32>(m_HPhase) + 1;
    if (nextPhase == m_HTimings.size()) {
        nextPhase = 0;
    }

    m_HPhase = static_cast<HorizontalPhase>(nextPhase);
    switch (m_HPhase) {
    case HorizontalPhase::Active: BeginHPhaseActiveDisplay(); break;
    case HorizontalPhase::RightBorder: BeginHPhaseRightBorder(); break;
    case HorizontalPhase::Sync: BeginHPhaseSync(); break;
    case HorizontalPhase::VBlankOut: BeginHPhaseVBlankOut(); break;
    case HorizontalPhase::LeftBorder: BeginHPhaseLeftBorder(); break;
    case HorizontalPhase::LastDot: BeginHPhaseLastDot(); break;
    }
}

FORCE_INLINE uint64 VDP::GetPhaseCycles() const {
    return m_HTimings[static_cast<uint32>(m_HPhase)];
}

template <bool verbose>
void VDP::UpdateResolution() {
    if (!m_VDP2.TVMDDirty) {
        return;
    }

    m_VDP2.TVMDDirty = false;

    // Horizontal/vertical resolution tables
    // NTSC uses the first two vRes entries, PAL uses the full table, and exclusive monitors use 480 lines
    static constexpr uint32 hRes[] = {320, 352, 640, 704};
    static constexpr uint32 vRes[] = {224, 240, 256, 256};

    // TODO: check for NTSC, PAL or exclusive monitor; assuming NTSC for now
    // TODO: exclusive monitor: even hRes entries are valid for 31 KHz monitors, odd are for Hi-Vision
    m_HRes = hRes[m_VDP2.TVMD.HRESOn];
    m_VRes = vRes[m_VDP2.TVMD.VRESOn & (m_VDP2.TVSTAT.PAL ? 3 : 1)];
    if (m_VDP2.TVMD.LSMDn == InterlaceMode::DoubleDensity) {
        // Double-density interlace doubles the vertical resolution
        m_VRes *= 2;
    }

    // Timing tables

    // Horizontal phase timings (cycles until):
    //   RBd = Right Border
    //   HSy = Horizontal Sync
    //   VBC = VBlank Clear
    //   LBd = Left Border
    //   LDt = Last Dot
    //   ADp = Active Display
    // NOTE: these timings specify the HCNT to advance to the specified phase
    static constexpr std::array<std::array<uint32, 6>, 4> hTimings{{
        // RBd, HSy, VBC, LBd, LDt, ADp
        {320, 27, 27, 26, 26, 1}, // {320, 347, 374, 400, 426, 427},
        {352, 23, 28, 29, 22, 1}, // {352, 375, 403, 432, 454, 455},
        {640, 54, 54, 52, 52, 2}, // {640, 694, 748, 800, 852, 854},
        {704, 46, 56, 58, 44, 2}, // {704, 750, 806, 864, 908, 910},
    }};
    m_HTimings = hTimings[m_VDP2.TVMD.HRESOn & 3]; // TODO: check exclusive monitor timings

    // Vertical phase timings (to reach):
    //   BBd = Bottom Border
    //   BSy = Blanking and Vertical Sync
    //   TBd = Top Border
    //   LLn = Last Line
    //   ADp = Active Display
    // NOTE: these timings indicate the VCNT at which the specified phase begins
    static constexpr std::array<std::array<std::array<uint32, 5>, 4>, 2> vTimings{{
        // NTSC
        {{
            // BBd, BSy, TBd, LLn, ADp
            {224, 232, 255, 262, 263},
            {240, 240, 255, 262, 263},
            {224, 232, 255, 262, 263},
            {240, 240, 255, 262, 263},
        }},
        // PAL
        {{
            // BBd, BSy, TBd, LLn, ADp
            {224, 256, 281, 312, 313},
            {240, 264, 289, 312, 313},
            {256, 272, 297, 312, 313},
            {256, 272, 297, 312, 313},
        }},
    }};
    m_VTimings = vTimings[m_VDP2.TVSTAT.PAL][m_VDP2.TVMD.VRESOn];

    // Adjust for dot clock
    const uint32 dotClockMult = (m_VDP2.TVMD.HRESOn & 2) ? 2 : 4;
    for (auto &timing : m_HTimings) {
        timing *= dotClockMult;
    }

    if constexpr (verbose) {
        devlog::info<grp::vdp2>("Screen resolution set to {}x{}", m_HRes, m_VRes);
        switch (m_VDP2.TVMD.LSMDn) {
        case InterlaceMode::None: devlog::info<grp::vdp2>("Non-interlace mode"); break;
        case InterlaceMode::Invalid: devlog::info<grp::vdp2>("Invalid interlace mode"); break;
        case InterlaceMode::SingleDensity: devlog::info<grp::vdp2>("Single-density interlace mode"); break;
        case InterlaceMode::DoubleDensity: devlog::info<grp::vdp2>("Double-density interlace mode"); break;
        }
        devlog::info<grp::vdp2>("Dot clock mult = {}, display {}", dotClockMult, (m_VDP2.TVMD.DISP ? "ON" : "OFF"));
    }
}

FORCE_INLINE void VDP::IncrementVCounter() {
    ++m_VCounter;
    while (m_VCounter >= m_VTimings[static_cast<uint32>(m_VPhase)]) {
        auto nextPhase = static_cast<uint32>(m_VPhase) + 1;
        if (nextPhase == m_VTimings.size()) {
            m_VCounter = 0;
            nextPhase = 0;
        }

        m_VPhase = static_cast<VerticalPhase>(nextPhase);
        switch (m_VPhase) {
        case VerticalPhase::Active: BeginVPhaseActiveDisplay(); break;
        case VerticalPhase::BottomBorder: BeginVPhaseBottomBorder(); break;
        case VerticalPhase::BlankingAndSync: BeginVPhaseBlankingAndSync(); break;
        case VerticalPhase::TopBorder: BeginVPhaseTopBorder(); break;
        case VerticalPhase::LastLine: BeginVPhaseLastLine(); break;
        }
    }
}

// ----

void VDP::BeginHPhaseActiveDisplay() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering horizontal active display phase", m_VCounter);
    if (m_VPhase == VerticalPhase::Active) {
        if (m_VCounter == 0) {
            devlog::trace<grp::base>("Begin VDP2 frame, VDP1 framebuffer {}", m_displayFB);

            VDP2InitFrame();
        } else if (m_VCounter == 210) { // ~1ms before VBlank IN
            m_cbTriggerOptimizedINTBACKRead();
        }

        if (m_threadedVDPRendering) {
            if (m_VDPRenderContext.vdp1Done) {
                m_VDP1.currFrameEnded = true;
                m_cbTriggerSpriteDrawEnd();
                m_cbVDP1FrameComplete();
                m_VDPRenderContext.vdp1Done = false;
            }

            m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP2DrawLine(m_VCounter));
        } else {
            VDP2DrawLine(m_VCounter);
        }
    }
}

void VDP::BeginHPhaseRightBorder() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering right border phase", m_VCounter);

    devlog::trace<grp::base>("## HBlank IN {:3d}", m_VCounter);

    m_VDP2.TVSTAT.HBLANK = 1;
    m_cbHBlankStateChange(true);

    // Start erasing if we just entered VBlank IN
    if (m_VCounter == m_VTimings[static_cast<uint32>(VerticalPhase::Active)]) {
        devlog::trace<grp::base>("## HBlank IN + VBlank IN  VBE={:d} manualerase={:d}", m_VDP1.vblankErase,
                                 m_VDP1.fbManualErase);

        if (m_VDP1.vblankErase || !m_VDP1.fbSwapMode) {
            // TODO: cycle-count the erase process, starting here
            if (m_threadedVDPRendering) {
                m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP1EraseFramebuffer());
            } else {
                VDP1EraseFramebuffer();
            }
        }
    }

    // TODO: draw border
}

void VDP::BeginHPhaseSync() {
    IncrementVCounter();
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering horizontal sync phase", m_VCounter);
}

void VDP::BeginHPhaseVBlankOut() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering VBlank OUT horizontal phase", m_VCounter);

    if (m_VPhase == VerticalPhase::LastLine) {
        devlog::trace<grp::base>("## HBlank half + VBlank OUT  FCM={:d} FCT={:d} manualswap={:d} PTM={:d}",
                                 m_VDP1.fbSwapMode, m_VDP1.fbSwapTrigger, m_VDP1.fbManualSwap, m_VDP1.plotTrigger);

        // Swap framebuffer in manual swap requested or in 1-cycle mode
        if (!m_VDP1.fbSwapMode || m_VDP1.fbManualSwap) {
            m_VDP1.fbManualSwap = false;
            VDP1SwapFramebuffer();
        }
    }
}

void VDP::BeginHPhaseLeftBorder() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering left border phase", m_VCounter);

    m_VDP2.TVSTAT.HBLANK = 0;
    m_cbHBlankStateChange(false);

    // TODO: draw border
}

void VDP::BeginHPhaseLastDot() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering last dot phase", m_VCounter);

    // If we just entered the bottom blanking vertical phase, switch fields
    if (m_VCounter == m_VTimings[static_cast<uint32>(VerticalPhase::Active)]) {
        if (m_VDP2.TVMD.LSMDn != InterlaceMode::None) {
            m_VDP2.TVSTAT.ODD ^= 1;
            devlog::trace<grp::base>("Switched to {} field", (m_VDP2.TVSTAT.ODD ? "odd" : "even"));
            if (m_threadedVDPRendering) {
                m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::OddField(m_VDP2.TVSTAT.ODD));
            }
        } else {
            if (m_VDP2.TVSTAT.ODD != 1) {
                m_VDP2.TVSTAT.ODD = 1;
                if (m_threadedVDPRendering) {
                    m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::OddField(m_VDP2.TVSTAT.ODD));
                }
            }
        }
    }
}

// ----

void VDP::BeginVPhaseActiveDisplay() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering vertical active display phase", m_VCounter);
}

void VDP::BeginVPhaseBottomBorder() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering bottom border phase", m_VCounter);

    devlog::trace<grp::base>("## VBlank IN");

    m_VDP2.TVSTAT.VBLANK = 1;
    m_cbVBlankStateChange(true);

    // TODO: draw border
}

void VDP::BeginVPhaseBlankingAndSync() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering blanking/vertical sync phase", m_VCounter);

    // End frame
    devlog::trace<grp::base>("End VDP2 frame");
    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP2EndFrame());
        m_VDPRenderContext.renderFinishedSignal.Wait(true);
    }
    m_cbFrameComplete(m_framebuffer.data(), m_HRes, m_VRes);
}

void VDP::BeginVPhaseTopBorder() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering top border phase", m_VCounter);

    UpdateResolution<true>();

    // TODO: draw border
}

void VDP::BeginVPhaseLastLine() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering last line phase", m_VCounter);

    devlog::trace<grp::base>("## VBlank OUT");

    m_VDP2.TVSTAT.VBLANK = 0;
    m_cbVBlankStateChange(false);
}

// -----------------------------------------------------------------------------
// Rendering

void VDP::VDPRenderThread() {
    util::SetCurrentThreadName("VDP render thread");

    auto &rctx = m_VDPRenderContext;

    std::array<VDPRenderEvent, 64> events{};

    bool running = true;
    while (running) {
        const size_t count = rctx.DequeueEvents(events.begin(), events.size());

        for (size_t i = 0; i < count; ++i) {
            auto &event = events[i];
            using EvtType = VDPRenderEvent::Type;
            switch (event.type) {
            case EvtType::Reset:
                rctx.Reset();
                m_framebuffer.fill(0xFF000000);
                break;
            case EvtType::OddField: rctx.vdp2.regs.TVSTAT.ODD = event.oddField.odd; break;
            case EvtType::VDP1EraseFramebuffer: VDP1EraseFramebuffer(); break;
            case EvtType::VDP1SwapFramebuffer:
                rctx.displayFB ^= 1;
                rctx.framebufferSwapSignal.Set();
                break;
            case EvtType::VDP1BeginFrame:
                m_VDPRenderContext.vdp1Done = false;
                for (int i = 0; i < 100000 && m_VDP1RenderContext.rendering; i++) {
                    VDP1ProcessCommand();
                }
                break;
            /*case EvtType::VDP1ProcessCommands:
                for (uint64 i = 0; i < event.processCommands.steps; i++) {
                    VDP1ProcessCommand();
                }
                break;*/
            case EvtType::VDP2DrawLine: VDP2DrawLine(event.drawLine.vcnt); break;
            case EvtType::VDP2EndFrame: rctx.renderFinishedSignal.Set(); break;

            case EvtType::VDP1VRAMWriteByte: rctx.vdp1.VRAM[event.write.address] = event.write.value; break;
            case EvtType::VDP1VRAMWriteWord:
                util::WriteBE<uint16>(&rctx.vdp1.VRAM[event.write.address], event.write.value);
                break;
            /*case EvtType::VDP1FBWriteByte: rctx.vdp1.spriteFB[event.write.address] = event.write.value; break;
            case EvtType::VDP1FBWriteWord:
                util::WriteBE<uint16>(&rctx.vdp1.spriteFB[event.write.address], event.write.value);
                break;*/
            case EvtType::VDP1RegWrite: rctx.vdp1.regs.Write<false>(event.write.address, event.write.value); break;

            case EvtType::VDP2VRAMWriteByte: rctx.vdp2.VRAM[event.write.address] = event.write.value; break;
            case EvtType::VDP2VRAMWriteWord:
                util::WriteBE<uint16>(&rctx.vdp2.VRAM[event.write.address], event.write.value);
                break;
            case EvtType::VDP2CRAMWriteByte:
                // Update CRAM cache if color RAM mode changed is in one of the RGB555 modes
                if (rctx.vdp2.regs.vramControl.colorRAMMode <= 1) {
                    const uint8 oldValue = rctx.vdp2.CRAM[event.write.address];
                    rctx.vdp2.CRAM[event.write.address] = event.write.value;

                    if (oldValue != event.write.value) {
                        const uint32 cramAddress = event.write.address & ~1;
                        const uint16 colorValue = VDP2ReadRendererCRAM<uint16>(cramAddress);
                        const Color555 color5{.u16 = colorValue};
                        rctx.vdp2.CRAMCache[cramAddress / sizeof(uint16)] = ConvertRGB555to888(color5);
                    }
                } else {
                    rctx.vdp2.CRAM[event.write.address] = event.write.value;
                }
                break;
            case EvtType::VDP2CRAMWriteWord:
                // Update CRAM cache if color RAM mode is in one of the RGB555 modes
                if (rctx.vdp2.regs.vramControl.colorRAMMode <= 1) {
                    const uint16 oldValue = util::ReadBE<uint16>(&rctx.vdp2.CRAM[event.write.address]);
                    util::WriteBE<uint16>(&rctx.vdp2.CRAM[event.write.address], event.write.value);

                    if (oldValue != event.write.value) {
                        const uint32 cramAddress = event.write.address & ~1;
                        const Color555 color5{.u16 = (uint16)event.write.value};
                        rctx.vdp2.CRAMCache[cramAddress / sizeof(uint16)] = ConvertRGB555to888(color5);
                    }
                } else {
                    util::WriteBE<uint16>(&rctx.vdp2.CRAM[event.write.address], event.write.value);
                }
                break;
            case EvtType::VDP2RegWrite:
                // Refill CRAM cache if color RAM mode changed to one of the RGB555 modes
                if (event.write.address == 0x00E) {
                    const uint8 oldMode = rctx.vdp2.regs.vramControl.colorRAMMode;
                    rctx.vdp2.regs.WriteRAMCTL(event.write.value);

                    const uint8 newMode = rctx.vdp2.regs.vramControl.colorRAMMode;
                    if (newMode != oldMode && newMode <= 1) {
                        for (uint32 addr = 0; addr < rctx.vdp2.CRAM.size(); addr += sizeof(uint16)) {
                            const uint16 colorValue = VDP2ReadRendererCRAM<uint16>(addr);
                            const Color555 color5{.u16 = colorValue};
                            rctx.vdp2.CRAMCache[addr / sizeof(uint16)] = ConvertRGB555to888(color5);
                        }
                    }
                } else {
                    rctx.vdp2.regs.Write(event.write.address, event.write.value);
                }
                break;

            case EvtType::PreSaveStateSync: rctx.preSaveSyncSignal.Set(); break;
            case EvtType::PostLoadStateSync:
                rctx.vdp1.regs = m_VDP1;
                rctx.vdp1.VRAM = m_VRAM1;
                rctx.vdp2.regs = m_VDP2;
                rctx.vdp2.VRAM = m_VRAM2;
                rctx.vdp2.CRAM = m_CRAM;
                rctx.postLoadSyncSignal.Set();
                for (uint32 addr = 0; addr < rctx.vdp2.CRAM.size(); addr += sizeof(uint16)) {
                    const uint16 colorValue = VDP2ReadRendererCRAM<uint16>(addr);
                    const Color555 color5{.u16 = colorValue};
                    rctx.vdp2.CRAMCache[addr / sizeof(uint16)] = ConvertRGB555to888(color5);
                }
                break;

            case EvtType::Shutdown: running = false; break;
            }
        }
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP1ReadRendererVRAM(uint32 address) {
    if (m_threadedVDPRendering) {
        return util::ReadBE<T>(&m_VDPRenderContext.vdp1.VRAM[address & 0x7FFFF]);
    } else {
        return VDP1ReadVRAM<T>(address);
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP2ReadRendererVRAM(uint32 address) {
    if (m_threadedVDPRendering) {
        // TODO: handle VRSIZE.VRAMSZ
        address &= 0x7FFFF;
        return util::ReadBE<T>(&m_VDPRenderContext.vdp2.VRAM[address]);
    } else {
        return VDP2ReadVRAM<T>(address);
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP2ReadRendererCRAM(uint32 address) {
    if (m_threadedVDPRendering) {
        if constexpr (std::is_same_v<T, uint32>) {
            uint32 value = VDP2ReadRendererCRAM<uint16>(address + 0) << 16u;
            value |= VDP2ReadRendererCRAM<uint16>(address + 2) << 0u;
            return value;
        }

        address = MapRendererCRAMAddress(address);
        return util::ReadBE<T>(&m_VDPRenderContext.vdp2.CRAM[address]);
    } else {
        return VDP2ReadCRAM<T, false>(address);
    }
}

FORCE_INLINE Color888 VDP::VDP2ReadRendererColor5to8(uint32 address) {
    if (m_threadedVDPRendering) {
        return m_VDPRenderContext.vdp2.CRAMCache[(address / sizeof(uint16)) & 0x7FF];
    } else {
        return m_CRAMCache[(address / sizeof(uint16)) & 0x7FF];
    }
}

// -----------------------------------------------------------------------------
// VDP1

FORCE_INLINE VDP1Regs &VDP::VDP1GetRegs() {
    if (m_threadedVDPRendering) {
        return m_VDPRenderContext.vdp1.regs;
    } else {
        return m_VDP1;
    }
}

FORCE_INLINE uint8 VDP::VDP1GetDisplayFBIndex() const {
    if (m_threadedVDPRendering) {
        return m_VDPRenderContext.displayFB;
    } else {
        return m_displayFB;
    }
}

FORCE_INLINE void VDP::VDP1EraseFramebuffer() {
    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();

    devlog::trace<grp::vdp1_render>("Erasing framebuffer {} - {}x{} to {}x{} -> {:04X}  {}x{}  {}-bit", m_displayFB,
                                    regs1.eraseX1, regs1.eraseY1, regs1.eraseX3, regs1.eraseY3, regs1.eraseWriteValue,
                                    regs1.fbSizeH, regs1.fbSizeV, (regs1.pixel8Bits ? 8 : 16));

    auto &fb = m_spriteFB[VDP1GetDisplayFBIndex()];

    // Horizontal scale is doubled in hi-res modes or when targeting rotation background
    const uint32 scaleH = (regs2.TVMD.HRESOn & 0b010) || regs1.fbRotEnable ? 1 : 0;
    // Vertical scale is doubled in double-interlace mode
    const uint32 scaleV = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity ? 1 : 0;

    // Constrain erase area to certain limits based on current resolution
    const uint32 maxH = (regs2.TVMD.HRESOn & 1) ? 428 : 400;
    const uint32 maxV = m_VRes >> scaleV;

    const uint32 offsetShift = regs1.pixel8Bits ? 0 : 1;

    const uint32 x1 = std::min<uint32>(regs1.eraseX1, maxH) << scaleH;
    const uint32 x3 = std::min<uint32>(regs1.eraseX3, maxH) << scaleH;
    const uint32 y1 = std::min<uint32>(regs1.eraseY1, maxV) << scaleV;
    const uint32 y3 = std::min<uint32>(regs1.eraseY3, maxV) << scaleV;

    for (uint32 y = y1; y <= y3; y++) {
        const uint32 fbOffset = y * regs1.fbSizeH;
        for (uint32 x = x1; x <= x3; x++) {
            const uint32 address = (fbOffset + x) << offsetShift;
            util::WriteBE<uint16>(&fb[address & 0x3FFFE], regs1.eraseWriteValue);
        }
    }
}

FORCE_INLINE void VDP::VDP1SwapFramebuffer() {
    devlog::trace<grp::vdp1_render>("Swapping framebuffers - draw {}, display {}", m_displayFB, m_displayFB ^ 1);

    if (m_VDP1.fbManualErase) {
        m_VDP1.fbManualErase = false;
        if (m_threadedVDPRendering) {
            m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP1EraseFramebuffer());
        } else {
            VDP1EraseFramebuffer();
        }
    }

    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP1SwapFramebuffer());
        m_VDPRenderContext.framebufferSwapSignal.Wait(true);
    }

    m_displayFB ^= 1;

    if (bit::test<1>(m_VDP1.plotTrigger)) {
        VDP1BeginFrame();
    }
}

void VDP::VDP1BeginFrame() {
    devlog::trace<grp::vdp1_render>("Begin VDP1 frame on framebuffer {}", VDP1GetDisplayFBIndex() ^ 1);

    // TODO: setup rendering
    // TODO: figure out VDP1 timings

    m_VDP1.prevCommandAddress = m_VDP1.currCommandAddress;
    m_VDP1.currCommandAddress = 0;
    m_VDP1.returnAddress = ~0;
    m_VDP1.prevFrameEnded = m_VDP1.currFrameEnded;
    m_VDP1.currFrameEnded = false;

    m_VDP1RenderContext.rendering = true;
    if (m_threadedVDPRendering) {
        m_VDPRenderContext.EnqueueEvent(VDPRenderEvent::VDP1BeginFrame());
    }
}

void VDP::VDP1EndFrame() {
    devlog::trace<grp::vdp1_render>("End VDP1 frame on framebuffer {}", VDP1GetDisplayFBIndex() ^ 1);
    m_VDP1RenderContext.rendering = false;

    if (m_threadedVDPRendering) {
        m_VDPRenderContext.vdp1Done = true;
    } else {
        m_VDP1.currFrameEnded = true;
        m_cbTriggerSpriteDrawEnd();
        m_cbVDP1FrameComplete();
    }
}

void VDP::VDP1ProcessCommand() {
    static constexpr uint32 kNoReturn = ~0;

    if (!m_VDP1RenderContext.rendering) {
        return;
    }

    auto &cmdAddress = m_VDP1.currCommandAddress;

    const VDP1Command::Control control{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress)};
    devlog::trace<grp::vdp1_render>("Processing command {:04X} @ {:05X}", control.u16, cmdAddress);
    if (control.end) [[unlikely]] {
        devlog::trace<grp::vdp1_render>("End of command list");
        VDP1EndFrame();
    } else if (!control.skip) {
        // Process command
        using enum VDP1Command::CommandType;

        switch (control.command) {
        case DrawNormalSprite: VDP1Cmd_DrawNormalSprite(cmdAddress, control); break;
        case DrawScaledSprite: VDP1Cmd_DrawScaledSprite(cmdAddress, control); break;
        case DrawDistortedSprite: [[fallthrough]];
        case DrawDistortedSpriteAlt: VDP1Cmd_DrawDistortedSprite(cmdAddress, control); break;

        case DrawPolygon: VDP1Cmd_DrawPolygon(cmdAddress); break;
        case DrawPolylines: [[fallthrough]];
        case DrawPolylinesAlt: VDP1Cmd_DrawPolylines(cmdAddress); break;
        case DrawLine: VDP1Cmd_DrawLine(cmdAddress); break;

        case UserClipping: [[fallthrough]];
        case UserClippingAlt: VDP1Cmd_SetUserClipping(cmdAddress); break;
        case SystemClipping: VDP1Cmd_SetSystemClipping(cmdAddress); break;
        case SetLocalCoordinates: VDP1Cmd_SetLocalCoordinates(cmdAddress); break;

        default:
            devlog::debug<grp::vdp1_render>("Unexpected command type {:X}; aborting",
                                            static_cast<uint16>(control.command));
            VDP1EndFrame();
            return;
        }
    }

    // Go to the next command
    {
        using enum VDP1Command::JumpType;

        switch (control.jumpMode) {
        case Next: cmdAddress += 0x20; break;
        case Assign: {
            cmdAddress = (VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x02) << 3u) & ~0x1F;
            devlog::trace<grp::vdp1_render>("Jump to {:05X}", cmdAddress);

            // HACK: Sonic R attempts to jump back to 0 in some cases
            if (cmdAddress == 0) {
                devlog::warn<grp::vdp1_render>("Possible infinite loop detected; aborting");
                VDP1EndFrame();
                return;
            }
            break;
        }
        case Call: {
            // Nested calls seem to not update the return address
            if (m_VDP1.returnAddress == kNoReturn) {
                m_VDP1.returnAddress = cmdAddress + 0x20;
            }
            cmdAddress = (VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x02) << 3u) & ~0x1F;
            devlog::trace<grp::vdp1_render>("Call {:05X}", cmdAddress);
            break;
        }
        case Return: {
            // Return seems to only return if there was a previous Call
            if (m_VDP1.returnAddress != kNoReturn) {
                cmdAddress = m_VDP1.returnAddress;
                m_VDP1.returnAddress = kNoReturn;
            } else {
                cmdAddress += 0x20;
            }
            devlog::trace<grp::vdp1_render>("Return to {:05X}", cmdAddress);
            break;
        }
        }
        cmdAddress &= 0x7FFFF;
    }
}

FORCE_INLINE bool VDP::VDP1IsPixelUserClipped(CoordS32 coord) const {
    auto [x, y] = coord;
    const auto &ctx = m_VDP1RenderContext;
    if (x < ctx.userClipX0 || x > ctx.userClipX1) {
        return true;
    }
    if (y < ctx.userClipY0 || y > ctx.userClipY1) {
        return true;
    }
    return false;
}

FORCE_INLINE bool VDP::VDP1IsPixelSystemClipped(CoordS32 coord) const {
    auto [x, y] = coord;
    const auto &ctx = m_VDP1RenderContext;
    if (x < 0 || x > ctx.sysClipH) {
        return true;
    }
    if (y < 0 || y > ctx.sysClipV) {
        return true;
    }
    return false;
}

FORCE_INLINE bool VDP::VDP1IsLineSystemClipped(CoordS32 coord1, CoordS32 coord2) const {
    auto [x1, y1] = coord1;
    auto [x2, y2] = coord2;
    const auto &ctx = m_VDP1RenderContext;
    if (x1 < 0 && x2 < 0) {
        return true;
    }
    if (y1 < 0 && y2 < 0) {
        return true;
    }
    if (x1 > ctx.sysClipH && x2 > ctx.sysClipH) {
        return true;
    }
    if (y1 > ctx.sysClipV && y2 > ctx.sysClipV) {
        return true;
    }
    return false;
}

bool VDP::VDP1IsQuadSystemClipped(CoordS32 coord1, CoordS32 coord2, CoordS32 coord3, CoordS32 coord4) const {
    auto [x1, y1] = coord1;
    auto [x2, y2] = coord2;
    auto [x3, y3] = coord3;
    auto [x4, y4] = coord4;
    const auto &ctx = m_VDP1RenderContext;
    if (x1 < 0 && x2 < 0 && x3 < 0 && x4 < 0) {
        return true;
    }
    if (y1 < 0 && y2 < 0 && y3 < 0 && y4 < 0) {
        return true;
    }
    if (x1 > ctx.sysClipH && x2 > ctx.sysClipH && x3 > ctx.sysClipH && x4 > ctx.sysClipH) {
        return true;
    }
    if (y1 > ctx.sysClipV && y2 > ctx.sysClipV && y3 > ctx.sysClipV && y4 > ctx.sysClipV) {
        return true;
    }
    return false;
}

FORCE_INLINE void VDP::VDP1PlotPixel(CoordS32 coord, const VDP1PixelParams &pixelParams,
                                     const VDP1GouraudParams &gouraudParams) {
    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();

    auto [x, y] = coord;

    if (pixelParams.mode.meshEnable && ((x ^ y) & 1)) {
        return;
    }

    if (regs1.dblInterlaceEnable && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity) {
        if ((y & 1) == regs1.dblInterlaceDrawLine) {
            return;
        }
        y >>= 1;
    }

    // Reject pixels outside of clipping area
    if (VDP1IsPixelSystemClipped(coord)) {
        return;
    }
    if (pixelParams.mode.userClippingEnable) {
        // clippingMode = false -> draw inside, reject outside
        // clippingMode = true -> draw outside, reject inside
        // The function returns true if the pixel is clipped, therefore we want to reject pixels that return the
        // opposite of clippingMode on that function.
        if (VDP1IsPixelUserClipped(coord) != pixelParams.mode.clippingMode) {
            return;
        }
    }

    // TODO: pixelParams.mode.preClippingDisable

    const uint32 fbOffset = y * regs1.fbSizeH + x;
    auto &drawFB = m_spriteFB[VDP1GetDisplayFBIndex() ^ 1];
    if (regs1.pixel8Bits) {
        // TODO: what happens if pixelParams.mode.colorCalcBits/gouraudEnable != 0?
        if (pixelParams.mode.msbOn) {
            drawFB[fbOffset & 0x3FFFF] |= 0x80;
        } else {
            drawFB[fbOffset & 0x3FFFF] = pixelParams.color;
        }
    } else {
        uint8 *pixel = &drawFB[(fbOffset * sizeof(uint16)) & 0x3FFFE];

        if (pixelParams.mode.msbOn) {
            drawFB[(fbOffset * sizeof(uint16)) & 0x3FFFE] |= 0x80;
        } else {
            Color555 srcColor{.u16 = pixelParams.color};
            Color555 dstColor{.u16 = util::ReadBE<uint16>(pixel)};

            // Apply color calculations
            //
            // In all cases where calculation is done, the raw color data to be drawn ("original graphic") or from
            // the background are interpreted as 5:5:5 RGB.

            if (pixelParams.mode.gouraudEnable) {
                // Calculate gouraud shading on source color
                // Interpolate between A, B, C and D (ordered in the standard Saturn quad orientation) using U and V
                // Gouraud channel values are offset by -16

                auto lerp = [](sint64 x, sint64 y, uint64 t) -> sint16 {
                    static constexpr uint64 shift = Slope::kFracBits;
                    return ((x << shift) + (y - x) * t) >> shift;
                };

                const Color555 A = gouraudParams.colorA;
                const Color555 B = gouraudParams.colorB;
                const Color555 C = gouraudParams.colorC;
                const Color555 D = gouraudParams.colorD;
                const uint64 U = gouraudParams.U;
                const uint64 V = gouraudParams.V;

                const sint16 ABr = lerp(static_cast<sint16>(A.r), static_cast<sint16>(B.r), U);
                const sint16 ABg = lerp(static_cast<sint16>(A.g), static_cast<sint16>(B.g), U);
                const sint16 ABb = lerp(static_cast<sint16>(A.b), static_cast<sint16>(B.b), U);

                const sint16 DCr = lerp(static_cast<sint16>(D.r), static_cast<sint16>(C.r), U);
                const sint16 DCg = lerp(static_cast<sint16>(D.g), static_cast<sint16>(C.g), U);
                const sint16 DCb = lerp(static_cast<sint16>(D.b), static_cast<sint16>(C.b), U);

                srcColor.r = std::clamp(srcColor.r + lerp(ABr, DCr, V) - 0x10, 0, 31);
                srcColor.g = std::clamp(srcColor.g + lerp(ABg, DCg, V) - 0x10, 0, 31);
                srcColor.b = std::clamp(srcColor.b + lerp(ABb, DCb, V) - 0x10, 0, 31);

                // HACK: replace with U/V coordinates
                // srcColor.r = U >> (Slope::kFracBits - 5);
                // srcColor.g = V >> (Slope::kFracBits - 5);

                // HACK: replace with computed gouraud gradient
                // srcColor.r = lerp(ABr, DCr, V);
                // srcColor.g = lerp(ABg, DCg, V);
                // srcColor.b = lerp(ABb, DCb, V);
            }

            switch (pixelParams.mode.colorCalcBits) {
            case 0: // Replace
                util::WriteBE<uint16>(pixel, srcColor.u16);
                break;
            case 1: // Shadow
                // Halve destination luminosity if it's not transparent
                if (dstColor.msb) {
                    dstColor.r >>= 1u;
                    dstColor.g >>= 1u;
                    dstColor.b >>= 1u;
                    util::WriteBE<uint16>(pixel, dstColor.u16);
                }
                break;
            case 2: // Half-luminance
                // Draw original graphic with halved luminance
                srcColor.r >>= 1u;
                srcColor.g >>= 1u;
                srcColor.b >>= 1u;
                util::WriteBE<uint16>(pixel, srcColor.u16);
                break;
            case 3: // Half-transparency
                // If background is not transparent, blend half of original graphic and half of background
                // Otherwise, draw original graphic as is
                if (dstColor.msb) {
                    srcColor.r = (srcColor.r + dstColor.r) >> 1u;
                    srcColor.g = (srcColor.g + dstColor.g) >> 1u;
                    srcColor.b = (srcColor.b + dstColor.b) >> 1u;
                }
                util::WriteBE<uint16>(pixel, srcColor.u16);
                break;
            }
        }
    }
}

FORCE_INLINE void VDP::VDP1PlotLine(CoordS32 coord1, CoordS32 coord2, const VDP1PixelParams &pixelParams,
                                    VDP1GouraudParams &gouraudParams) {
    for (LineStepper line{coord1, coord2}; line.CanStep(); line.Step()) {
        gouraudParams.U = line.FracPos();
        VDP1PlotPixel(line.Coord(), pixelParams, gouraudParams);
        if (line.NeedsAntiAliasing()) {
            VDP1PlotPixel(line.AACoord(), pixelParams, gouraudParams);
        }
    }
}

void VDP::VDP1PlotTexturedLine(CoordS32 coord1, CoordS32 coord2, const VDP1TexturedLineParams &lineParams,
                               VDP1GouraudParams &gouraudParams) {
    const VDP1Regs &regs = VDP1GetRegs();

    const uint32 charSizeH = lineParams.charSizeH;
    const uint32 charSizeV = lineParams.charSizeV;
    const auto mode = lineParams.mode;
    const auto control = lineParams.control;

    const uint32 v = lineParams.texFracV >> Slope::kFracBits;
    gouraudParams.V = lineParams.texFracV;
    if (charSizeV != 0) {
        gouraudParams.V /= charSizeV;
    }

    uint16 color = 0;
    bool transparent = true;
    const bool flipU = control.flipH;
    bool hasEndCode = false;
    int endCodeCount = 0;
    for (TexturedLineStepper line{coord1, coord2, charSizeH, flipU}; line.CanStep(); line.Step()) {
        // Load new texel if U coordinate changed.
        // Note that the very first pixel in the line always passes the check.
        if (line.UChanged()) {
            const uint32 u = line.U();

            const bool useHighSpeedShrink = mode.highSpeedShrink && line.uinc > Slope::kFracOne;
            const uint32 adjustedU = useHighSpeedShrink ? ((u & ~1) | (uint32)regs.evenOddCoordSelect) : u;

            const uint32 charIndex = adjustedU + v * charSizeH;

            auto processEndCode = [&](bool endCode) {
                if (endCode && !mode.endCodeDisable && !useHighSpeedShrink) {
                    hasEndCode = true;
                    endCodeCount++;
                } else {
                    hasEndCode = false;
                }
            };

            // Read next texel
            switch (mode.colorMode) {
            case 0: // 4 bpp, 16 colors, bank mode
                color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + (charIndex >> 1));
                color = (color >> ((~u & 1) * 4)) & 0xF;
                processEndCode(color == 0xF);
                transparent = color == 0x0;
                color |= lineParams.colorBank;
                break;
            case 1: // 4 bpp, 16 colors, lookup table mode
                color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + (charIndex >> 1));
                color = (color >> ((~u & 1) * 4)) & 0xF;
                processEndCode(color == 0xF);
                transparent = color == 0x0;
                color = VDP1ReadRendererVRAM<uint16>(color * sizeof(uint16) + lineParams.colorBank * 8);
                break;
            case 2: // 8 bpp, 64 colors, bank mode
                color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + charIndex) & 0x3F;
                processEndCode(color == 0xFF);
                transparent = color == 0x0;
                color |= lineParams.colorBank & 0xFFC0;
                break;
            case 3: // 8 bpp, 128 colors, bank mode
                color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + charIndex) & 0x7F;
                processEndCode(color == 0xFF);
                transparent = color == 0x00;
                color |= lineParams.colorBank & 0xFF80;
                break;
            case 4: // 8 bpp, 256 colors, bank mode
                color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + charIndex);
                processEndCode(color == 0xFF);
                transparent = color == 0x00;
                color |= lineParams.colorBank & 0xFF00;
                break;
            case 5: // 16 bpp, 32768 colors, RGB mode
                color = VDP1ReadRendererVRAM<uint16>(lineParams.charAddr + charIndex * sizeof(uint16));
                processEndCode(color == 0x7FFF);
                transparent = color == 0x0000;
                break;
            }

            if (endCodeCount == 2) {
                break;
            }
        }

        if (hasEndCode || (transparent && !mode.transparentPixelDisable)) {
            continue;
        }

        VDP1PixelParams pixelParams{
            .mode = mode,
            .color = color,
        };

        gouraudParams.U = line.FracU();
        if (charSizeH != 0) {
            gouraudParams.U /= charSizeH;
        }

        VDP1PlotPixel(line.Coord(), pixelParams, gouraudParams);
        if (line.NeedsAntiAliasing()) {
            VDP1PlotPixel(line.AACoord(), pixelParams, gouraudParams);
        }
    }
}

void VDP::VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control) {
    auto &ctx = m_VDP1RenderContext;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};
    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const uint32 charAddr = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x08) * 8u;
    const VDP1Command::Size size{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0A)};
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const uint32 charSizeH = size.H * 8;
    const uint32 charSizeV = size.V;

    const sint32 lx = xa;                                // left X
    const sint32 ty = ya;                                // top Y
    const sint32 rx = xa + std::max(charSizeH, 1u) - 1u; // right X
    const sint32 by = ya + std::max(charSizeV, 1u) - 1u; // bottom Y

    const CoordS32 coordA{lx, ty};
    const CoordS32 coordB{rx, ty};
    const CoordS32 coordC{rx, by};
    const CoordS32 coordD{lx, by};

    devlog::trace<grp::vdp1_render>(
        "Draw normal sprite: {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} color={:04X} "
        "gouraud={:04X} mode={:04X} size={:2d}x{:<2d} char={:X}",
        lx, ty, rx, ty, rx, by, lx, by, color, gouraudTable, mode.u16, charSizeH, charSizeV, charAddr);

    if (VDP1IsQuadSystemClipped(coordA, coordB, coordC, coordD)) {
        return;
    }

    VDP1TexturedLineParams lineParams{
        .control = control,
        .mode = mode,
        .colorBank = color,
        .charAddr = charAddr,
        .charSizeH = charSizeH,
        .charSizeV = charSizeV,
    };

    VDP1GouraudParams gouraudParams{
        .colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)},
        .colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)},
        .colorC{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)},
        .colorD{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)},
    };
    if (control.flipH) {
        std::swap(gouraudParams.colorA, gouraudParams.colorB);
        std::swap(gouraudParams.colorD, gouraudParams.colorC);
    }
    if (control.flipV) {
        std::swap(gouraudParams.colorA, gouraudParams.colorD);
        std::swap(gouraudParams.colorB, gouraudParams.colorC);
    }

    // Interpolate linearly over edges A-D and B-C
    const bool flipV = control.flipV;
    for (TexturedQuadEdgesStepper edge{coordA, coordB, coordC, coordD, charSizeV, flipV}; edge.CanStep(); edge.Step()) {
        // Plot lines between the interpolated points
        const CoordS32 coordL{edge.LX(), edge.LY()};
        const CoordS32 coordR{edge.RX(), edge.RY()};
        lineParams.texFracV = edge.FracV();
        VDP1PlotTexturedLine(coordL, coordR, lineParams, gouraudParams);
    }
}

void VDP::VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control) {
    auto &ctx = m_VDP1RenderContext;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};
    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const uint32 charAddr = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x08) * 8u;
    const VDP1Command::Size size{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0A)};
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const uint32 charSizeH = size.H * 8;
    const uint32 charSizeV = size.V;

    // Calculated quad coordinates
    sint32 qxa;
    sint32 qya;
    sint32 qxb;
    sint32 qyb;
    sint32 qxc;
    sint32 qyc;
    sint32 qxd;
    sint32 qyd;

    const uint8 zoomPointH = bit::extract<0, 1>(control.zoomPoint);
    const uint8 zoomPointV = bit::extract<2, 3>(control.zoomPoint);
    if (zoomPointH == 0 || zoomPointV == 0) {
        const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));
        const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));

        // Top-left coordinates on vertex A
        // Bottom-right coordinates on vertex C

        qxa = xa;
        qya = ya;
        qxb = xc;
        qyb = ya;
        qxc = xc;
        qyc = yc;
        qxd = xa;
        qyd = yc;
    } else {
        const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10));
        const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12));

        // Zoom origin on vertex A
        // Zoom dimensions on vertex B

        // X axis
        switch (zoomPointH) {
        case 1: // left
            qxa = xa;
            qxb = xa + xb;
            qxc = xa + xb;
            qxd = xa;
            break;
        case 2: // center
            qxa = xa - xb / 2;
            qxb = xa + (xb + 1) / 2;
            qxc = xa + (xb + 1) / 2;
            qxd = xa - xb / 2;
            break;
        case 3: // right
            qxa = xa - xb;
            qxb = xa;
            qxc = xa;
            qxd = xa - xb;
            break;
        }

        // Y axis
        switch (zoomPointV) {
        case 1: // upper
            qya = ya;
            qyb = ya;
            qyc = ya + yb;
            qyd = ya + yb;
            break;
        case 2: // center
            qya = ya - yb / 2;
            qyb = ya - yb / 2;
            qyc = ya + (yb + 1) / 2;
            qyd = ya + (yb + 1) / 2;
            break;
        case 3: // lower
            qya = ya - yb;
            qyb = ya - yb;
            qyc = ya;
            qyd = ya;
            break;
        }
    }

    qxa += ctx.localCoordX;
    qya += ctx.localCoordY;
    qxb += ctx.localCoordX;
    qyb += ctx.localCoordY;
    qxc += ctx.localCoordX;
    qyc += ctx.localCoordY;
    qxd += ctx.localCoordX;
    qyd += ctx.localCoordY;

    const CoordS32 coordA{qxa, qya};
    const CoordS32 coordB{qxb, qyb};
    const CoordS32 coordC{qxc, qyc};
    const CoordS32 coordD{qxd, qyd};

    devlog::trace<grp::vdp1_render>(
        "Draw scaled sprite: {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} color={:04X} "
        "gouraud={:04X} mode={:04X} size={:2d}x{:<2d} char={:X}",
        qxa, qya, qxb, qyb, qxc, qyc, qxd, qyd, color, gouraudTable, mode.u16, charSizeH, charSizeV, charAddr);

    if (VDP1IsQuadSystemClipped(coordA, coordB, coordC, coordD)) {
        return;
    }

    VDP1TexturedLineParams lineParams{
        .control = control,
        .mode = mode,
        .colorBank = color,
        .charAddr = charAddr,
        .charSizeH = charSizeH,
        .charSizeV = charSizeV,
    };

    VDP1GouraudParams gouraudParams{
        .colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)},
        .colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)},
        .colorC{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)},
        .colorD{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)},
    };
    if (control.flipH) {
        std::swap(gouraudParams.colorA, gouraudParams.colorB);
        std::swap(gouraudParams.colorD, gouraudParams.colorC);
    }
    if (control.flipV) {
        std::swap(gouraudParams.colorA, gouraudParams.colorD);
        std::swap(gouraudParams.colorB, gouraudParams.colorC);
    }

    // Interpolate linearly over edges A-D and B-C
    const bool flipV = control.flipV;
    for (TexturedQuadEdgesStepper edge{coordA, coordB, coordC, coordD, charSizeV, flipV}; edge.CanStep(); edge.Step()) {
        // Plot lines between the interpolated points
        const CoordS32 coordL{edge.LX(), edge.LY()};
        const CoordS32 coordR{edge.RX(), edge.RY()};
        lineParams.texFracV = edge.FracV();
        VDP1PlotTexturedLine(coordL, coordR, lineParams, gouraudParams);
    }
}

void VDP::VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control) {
    auto &ctx = m_VDP1RenderContext;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};
    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const uint32 charAddr = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x08) * 8u;
    const VDP1Command::Size size{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0A)};
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14)) + ctx.localCoordX;
    const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16)) + ctx.localCoordY;
    const sint32 xd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x18)) + ctx.localCoordX;
    const sint32 yd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1A)) + ctx.localCoordY;
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const uint32 charSizeH = size.H * 8;
    const uint32 charSizeV = size.V;

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, yb};
    const CoordS32 coordC{xc, yc};
    const CoordS32 coordD{xd, yd};

    devlog::trace<grp::vdp1_render>(
        "Draw distorted sprite: {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} color={:04X} "
        "gouraud={:04X} mode={:04X} size={:2d}x{:<2d} char={:X}",
        xa, ya, xb, yb, xc, yc, xd, yd, color, gouraudTable, mode.u16, charSizeH, charSizeV, charAddr);

    if (VDP1IsQuadSystemClipped(coordA, coordB, coordC, coordD)) {
        return;
    }

    VDP1TexturedLineParams lineParams{
        .control = control,
        .mode = mode,
        .colorBank = color,
        .charAddr = charAddr,
        .charSizeH = charSizeH,
        .charSizeV = charSizeV,
    };

    VDP1GouraudParams gouraudParams{
        .colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)},
        .colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)},
        .colorC{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)},
        .colorD{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)},
    };
    if (control.flipH) {
        std::swap(gouraudParams.colorA, gouraudParams.colorB);
        std::swap(gouraudParams.colorD, gouraudParams.colorC);
    }
    if (control.flipV) {
        std::swap(gouraudParams.colorA, gouraudParams.colorD);
        std::swap(gouraudParams.colorB, gouraudParams.colorC);
    }

    // Interpolate linearly over edges A-D and B-C
    const bool flipV = control.flipV;
    for (TexturedQuadEdgesStepper edge{coordA, coordB, coordC, coordD, charSizeV, flipV}; edge.CanStep(); edge.Step()) {
        // Plot lines between the interpolated points
        const CoordS32 coordL{edge.LX(), edge.LY()};
        const CoordS32 coordR{edge.RX(), edge.RY()};
        lineParams.texFracV = edge.FracV();
        VDP1PlotTexturedLine(coordL, coordR, lineParams, gouraudParams);
    }
}

void VDP::VDP1Cmd_DrawPolygon(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderContext;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};

    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14)) + ctx.localCoordX;
    const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16)) + ctx.localCoordY;
    const sint32 xd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x18)) + ctx.localCoordX;
    const sint32 yd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1A)) + ctx.localCoordY;
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, yb};
    const CoordS32 coordC{xc, yc};
    const CoordS32 coordD{xd, yd};

    devlog::trace<grp::vdp1_render>(
        "Draw polygon: {}x{} - {}x{} - {}x{} - {}x{}, color {:04X}, gouraud table {}, CMDPMOD = {:04X}", xa, ya, xb, yb,
        xc, yc, xd, yd, color, gouraudTable, mode.u16);

    if (VDP1IsQuadSystemClipped(coordA, coordB, coordC, coordD)) {
        return;
    }

    const VDP1PixelParams pixelParams{
        .mode = mode,
        .color = color,
    };

    VDP1GouraudParams gouraudParams{
        .colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)},
        .colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)},
        .colorC{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)},
        .colorD{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)},
    };

    // Interpolate linearly over edges A-D and B-C
    for (QuadEdgesStepper edge{coordA, coordB, coordC, coordD}; edge.CanStep(); edge.Step()) {
        const CoordS32 coordL{edge.LX(), edge.LY()};
        const CoordS32 coordR{edge.RX(), edge.RY()};

        gouraudParams.V = edge.FracPos();

        // Plot lines between the interpolated points
        VDP1PlotLine(coordL, coordR, pixelParams, gouraudParams);
    }
}

void VDP::VDP1Cmd_DrawPolylines(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderContext;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};

    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14)) + ctx.localCoordX;
    const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16)) + ctx.localCoordY;
    const sint32 xd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x18)) + ctx.localCoordX;
    const sint32 yd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1A)) + ctx.localCoordY;
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, yb};
    const CoordS32 coordC{xc, yc};
    const CoordS32 coordD{xd, yd};

    devlog::trace<grp::vdp1_render>(
        "Draw polylines: {}x{} - {}x{} - {}x{} - {}x{}, color {:04X}, gouraud table {}, CMDPMOD = {:04X}", xa, ya, xb,
        yb, xc, yc, xd, yd, color, gouraudTable >> 3u, mode.u16);

    if (VDP1IsQuadSystemClipped(coordA, coordB, coordC, coordD)) {
        return;
    }

    const VDP1PixelParams pixelParams{
        .mode = mode,
        .color = color,
    };

    const Color555 A{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)};
    const Color555 B{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)};
    const Color555 C{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)};
    const Color555 D{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)};

    VDP1GouraudParams gouraudParamsAB{.colorA = A, .colorB = B, .V = 0};
    VDP1GouraudParams gouraudParamsBC{.colorA = B, .colorB = C, .V = 0};
    VDP1GouraudParams gouraudParamsCD{.colorA = C, .colorB = D, .V = 0};
    VDP1GouraudParams gouraudParamsDA{.colorA = D, .colorB = A, .V = 0};

    VDP1PlotLine(coordA, coordB, pixelParams, gouraudParamsAB);
    VDP1PlotLine(coordB, coordC, pixelParams, gouraudParamsBC);
    VDP1PlotLine(coordC, coordD, pixelParams, gouraudParamsCD);
    VDP1PlotLine(coordD, coordA, pixelParams, gouraudParamsDA);
}

void VDP::VDP1Cmd_DrawLine(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderContext;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};

    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, yb};

    devlog::trace<grp::vdp1_render>("Draw line: {}x{} - {}x{}, color {:04X}, gouraud table {}, CMDPMOD = {:04X}", xa,
                                    ya, xb, yb, color, gouraudTable, mode.u16);

    if (VDP1IsLineSystemClipped(coordA, coordB)) {
        return;
    }

    const VDP1PixelParams pixelParams{
        .mode = mode,
        .color = color,
    };

    VDP1GouraudParams gouraudParams{
        .colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)},
        .colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)},
        .V = 0,
    };

    VDP1PlotLine(coordA, coordB, pixelParams, gouraudParams);
}

void VDP::VDP1Cmd_SetSystemClipping(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderContext;
    ctx.sysClipH = bit::extract<0, 9>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));
    ctx.sysClipV = bit::extract<0, 8>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));
    devlog::trace<grp::vdp1_render>("Set system clipping: {}x{}", ctx.sysClipH, ctx.sysClipV);
}

void VDP::VDP1Cmd_SetUserClipping(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderContext;
    ctx.userClipX0 = bit::extract<0, 9>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
    ctx.userClipY0 = bit::extract<0, 8>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
    ctx.userClipX1 = bit::extract<0, 9>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));
    ctx.userClipY1 = bit::extract<0, 8>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));
    devlog::trace<grp::vdp1_render>("Set user clipping: {}x{} - {}x{}", ctx.userClipX0, ctx.userClipY0, ctx.userClipX1,
                                    ctx.userClipY1);
}

void VDP::VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderContext;
    ctx.localCoordX = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
    ctx.localCoordY = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
    devlog::trace<grp::vdp1_render>("Set local coordinates: {}x{}", ctx.localCoordX, ctx.localCoordY);
}

// -----------------------------------------------------------------------------
// VDP2

FORCE_INLINE VDP2Regs &VDP::VDP2GetRegs() {
    if (m_threadedVDPRendering) {
        return m_VDPRenderContext.vdp2.regs;
    } else {
        return m_VDP2;
    }
}

FORCE_INLINE const VDP2Regs &VDP::VDP2GetRegs() const {
    if (m_threadedVDPRendering) {
        return m_VDPRenderContext.vdp2.regs;
    } else {
        return m_VDP2;
    }
}

FORCE_INLINE std::array<uint8, kVDP2VRAMSize> &VDP::VDP2GetVRAM() {
    if (m_threadedVDPRendering) {
        return m_VDPRenderContext.vdp2.VRAM;
    } else {
        return m_VRAM2;
    }
}

void VDP::VDP2InitFrame() {
    if (m_VDP2.bgEnabled[5]) {
        VDP2InitRotationBG<0>();
        VDP2InitRotationBG<1>();
    } else {
        VDP2InitRotationBG<0>();
        VDP2InitNormalBG<0>();
        VDP2InitNormalBG<1>();
        VDP2InitNormalBG<2>();
        VDP2InitNormalBG<3>();
    }
}

template <uint32 index>
FORCE_INLINE void VDP::VDP2InitNormalBG() {
    static_assert(index < 4, "Invalid NBG index");

    if (!m_VDP2.bgEnabled[index]) {
        return;
    }

    const BGParams &bgParams = m_VDP2.bgParams[index + 1];
    NormBGLayerState &bgState = m_normBGLayerStates[index];
    bgState.fracScrollX = 0;
    bgState.fracScrollY = 0;
    if (m_VDP2.TVMD.LSMDn == InterlaceMode::DoubleDensity && m_VDP2.TVSTAT.ODD) {
        bgState.fracScrollY += bgParams.scrollIncV;
    }

    bgState.scrollIncH = bgParams.scrollIncH;
    bgState.mosaicCounterY = 0;
    if constexpr (index < 2) {
        bgState.lineScrollTableAddress = bgParams.lineScrollTableAddress;
    }
}

template <uint32 index>
FORCE_INLINE void VDP::VDP2InitRotationBG() {
    static_assert(index < 2, "Invalid RBG index");

    if (!m_VDP2.bgEnabled[index + 4]) {
        return;
    }

    const BGParams &bgParams = m_VDP2.bgParams[index];
    const bool cellSizeShift = bgParams.cellSizeShift;
    const bool twoWordChar = bgParams.twoWordChar;

    for (int param = 0; param < 2; param++) {
        const RotationParams &rotParam = m_VDP2.rotParams[param];
        auto &pageBaseAddresses = m_rotParamStates[param].pageBaseAddresses;
        const uint16 plsz = rotParam.plsz;
        for (int plane = 0; plane < 16; plane++) {
            const uint32 mapIndex = rotParam.mapIndices[plane];
            pageBaseAddresses[plane] = CalcPageBaseAddress(cellSizeShift, twoWordChar, plsz, mapIndex);
        }
    }
}

void VDP::VDP2UpdateEnabledBGs() {
    // Sprite layer is always enabled, unless forcibly disabled
    m_layerStates[0].enabled = m_layerStates[0].rendered;

    if (m_VDP2.bgEnabled[5]) {
        m_layerStates[1].enabled = m_layerStates[1].rendered; // RBG0
        m_layerStates[2].enabled = m_layerStates[2].rendered; // RBG1
        m_layerStates[3].enabled = false;                     // EXBG
        m_layerStates[4].enabled = false;                     // not used
        m_layerStates[5].enabled = false;                     // not used
    } else {
        // Certain color format settings on NBG0 and NBG1 restrict which BG layers can be enabled
        // - NBG1 is disabled when NBG0 uses 8:8:8 RGB
        // - NBG2 is disabled when NBG0 uses 2048 color palette or any RGB format
        // - NBG3 is disabled when NBG0 uses 8:8:8 RGB or NBG1 uses 2048 color palette or 5:5:5 RGB color format
        const ColorFormat colorFormatNBG0 = m_VDP2.bgParams[1].colorFormat;
        const ColorFormat colorFormatNBG1 = m_VDP2.bgParams[2].colorFormat;
        const bool disableNBG1 = colorFormatNBG0 == ColorFormat::RGB888;
        const bool disableNBG2 = colorFormatNBG0 == ColorFormat::Palette2048 ||
                                 colorFormatNBG0 == ColorFormat::RGB555 || colorFormatNBG0 == ColorFormat::RGB888;
        const bool disableNBG3 = colorFormatNBG0 == ColorFormat::RGB888 ||
                                 colorFormatNBG1 == ColorFormat::Palette2048 || colorFormatNBG1 == ColorFormat::RGB555;

        m_layerStates[1].enabled = m_layerStates[1].rendered && m_VDP2.bgEnabled[4];                 // RBG0
        m_layerStates[2].enabled = m_layerStates[2].rendered && m_VDP2.bgEnabled[0];                 // NBG0
        m_layerStates[3].enabled = m_layerStates[3].rendered && m_VDP2.bgEnabled[1] && !disableNBG1; // NBG1/EXBG
        m_layerStates[4].enabled = m_layerStates[4].rendered && m_VDP2.bgEnabled[2] && !disableNBG2; // NBG2
        m_layerStates[5].enabled = m_layerStates[5].rendered && m_VDP2.bgEnabled[3] && !disableNBG3; // NBG3
    }
}

FORCE_INLINE void VDP::VDP2UpdateLineScreenScroll(uint32 y, const BGParams &bgParams, NormBGLayerState &bgState) {
    uint32 address = bgState.lineScrollTableAddress;
    auto read = [&] {
        const uint32 value = VDP2ReadRendererVRAM<uint32>(address);
        address += sizeof(uint32);
        return value;
    };

    const VDP2Regs &regs = VDP2GetRegs();
    size_t count = 1;
    if (regs.TVMD.LSMDn == InterlaceMode::DoubleDensity && (y > 0 || regs.TVSTAT.ODD)) {
        ++count;
    }
    for (size_t i = 0; i < count; ++i) {
        if (bgParams.lineScrollXEnable) {
            bgState.fracScrollX = bit::extract<8, 26>(read());
        }
        if (bgParams.lineScrollYEnable) {
            // TODO: check/optimize this
            bgState.fracScrollY = bit::extract<8, 26>(read());
        }
        if (bgParams.lineZoomEnable) {
            bgState.scrollIncH = bit::extract<8, 18>(read());
        }
    }
    if (y > 0 && (y & ((1u << bgParams.lineScrollInterval) - 1)) == 0) {
        bgState.lineScrollTableAddress = address;
    }
}

FORCE_INLINE void VDP::VDP2CalcRotationParameterTables(uint32 y) {
    VDP2Regs &regs = VDP2GetRegs();

    const uint32 baseAddress = regs.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
    const bool readAll = y == 0;

    for (int i = 0; i < 2; i++) {
        RotationParams &params = regs.rotParams[i];
        RotationParamState &state = m_rotParamStates[i];

        const bool readXst = readAll || params.readXst;
        const bool readYst = readAll || params.readYst;
        const bool readKAst = readAll || params.readKAst;

        // Tables are located at the base address 0x80 bytes apart
        RotationParamTable t{};
        const uint32 address = baseAddress + i * 0x80;
        t.ReadFrom(&VDP2GetVRAM()[address & 0x7FFFF]);

        // Calculate parameters

        // Transformed starting screen coordinates
        // 16*(16-16) + 16*(16-16) + 16*(16-16) = 32 frac bits
        // reduce to 16 frac bits
        const sint64 Xsp = (t.A * (t.Xst - t.Px) + t.B * (t.Yst - t.Py) + t.C * (t.Zst - t.Pz)) >> 16ll;
        const sint64 Ysp = (t.D * (t.Xst - t.Px) + t.E * (t.Yst - t.Py) + t.F * (t.Zst - t.Pz)) >> 16ll;

        // Transformed view coordinates
        // 16*(16-16) + 16*(16-16) + 16*(16-16) + 16 + 16 = 32+32+32 + 16+16
        // reduce 32 to 16 frac bits, result is 16 frac bits
        /***/ sint64 Xp = ((t.A * (t.Px - t.Cx) + t.B * (t.Py - t.Cy) + t.C * (t.Pz - t.Cz)) >> 16ll) + t.Cx + t.Mx;
        const sint64 Yp = ((t.D * (t.Px - t.Cx) + t.E * (t.Py - t.Cy) + t.F * (t.Pz - t.Cz)) >> 16ll) + t.Cy + t.My;

        // Screen coordinate increments per Vcnt
        // 16*16 + 16*16 = 32
        // reduce to 16 frac bits
        const sint64 scrXIncV = (t.A * t.deltaXst + t.B * t.deltaYst) >> 16ll;
        const sint64 scrYIncV = (t.D * t.deltaXst + t.E * t.deltaYst) >> 16ll;

        // Screen coordinate increments per Hcnt
        // 16*16 + 16*16 = 32 frac bits
        // reduce to 16 frac bits
        const sint64 scrXIncH = (t.A * t.deltaX + t.B * t.deltaY) >> 16ll;
        const sint64 scrYIncH = (t.D * t.deltaX + t.E * t.deltaY) >> 16ll;

        // Scaling factors
        // 16 frac bits
        sint64 kx = t.kx;
        sint64 ky = t.ky;

        if (readXst) {
            state.scrX = Xsp;
        }
        if (readYst) {
            state.scrY = Ysp;
        }
        if (readKAst) {
            state.KA = t.KAst;
        }

        // Current screen coordinates (16 frac bits) and coefficient address (10 frac bits)
        sint32 scrX = state.scrX;
        sint32 scrY = state.scrY;
        uint32 KA = state.KA;

        const bool doubleResH = regs.TVMD.HRESOn & 0b010;
        const uint32 xShift = doubleResH ? 1 : 0;
        const uint32 maxX = m_HRes >> xShift;

        // Use per-dot coefficient if reading from CRAM or if any of the VRAM banks was designated as coefficient data
        bool perDotCoeff = regs.vramControl.colorRAMCoeffTableEnable;
        if (!perDotCoeff) {
            perDotCoeff = regs.vramControl.rotDataBankSelA0 == 1 || regs.vramControl.rotDataBankSelB0 == 1;
            if (regs.vramControl.partitionVRAMA) {
                perDotCoeff |= regs.vramControl.rotDataBankSelA1 == 1;
            }
            if (regs.vramControl.partitionVRAMB) {
                perDotCoeff |= regs.vramControl.rotDataBankSelB1 == 1;
            }
        }

        // Precompute line color data parameters
        const LineBackScreenParams &lineParams = regs.lineScreenParams;
        const uint32 line = lineParams.perLine ? y : 0;
        const uint32 lineColorAddress = lineParams.baseAddress + line * sizeof(uint16);
        const uint32 baseLineColorCRAMAddress = VDP2ReadRendererVRAM<uint16>(lineColorAddress) * sizeof(uint16);

        // Fetch first coefficient
        Coefficient coeff = VDP2FetchRotationCoefficient(params, KA);

        // Precompute whole line
        for (uint32 x = 0; x < maxX; x++) {
            // Process coefficient table
            if (params.coeffTableEnable) {
                state.transparent[x] = coeff.transparent;

                // Replace parameters with those obtained from the coefficient table if enabled
                using enum CoefficientDataMode;
                switch (params.coeffDataMode) {
                case ScaleCoeffXY: kx = ky = coeff.value; break;
                case ScaleCoeffX: kx = coeff.value; break;
                case ScaleCoeffY: ky = coeff.value; break;
                case ViewpointX: Xp = coeff.value; break;
                }

                // Compute line colors
                if (params.coeffUseLineColorData) {
                    const uint32 cramAddress = bit::deposit<1, 8>(baseLineColorCRAMAddress, coeff.lineColorData);
                    state.lineColor[x] = VDP2ReadRendererColor5to8(cramAddress);
                }

                // Increment coefficient table address by Hcnt if using per-dot coefficients
                if (perDotCoeff) {
                    KA += t.dKAx;
                    if (VDP2CanFetchCoefficient(params, KA)) {
                        coeff = VDP2FetchRotationCoefficient(params, KA);
                    }
                }
            }

            // Store screen coordinates
            state.screenCoords[x].x() = ((kx * scrX) >> 16ll) + Xp;
            state.screenCoords[x].y() = ((ky * scrY) >> 16ll) + Yp;

            // Increment screen coordinates and coefficient table address by Hcnt
            scrX += scrXIncH;
            scrY += scrYIncH;
        }

        // Increment screen coordinates and coefficient table address by Vcnt for the next iteration
        state.scrX += scrXIncV;
        state.scrY += scrYIncV;
        state.KA += t.dKAst;

        // Disable read flags now that we've dealt with them
        params.readXst = false;
        params.readYst = false;
        params.readKAst = false;
    }
}

FORCE_INLINE void VDP::VDP2CalcWindows(uint32 y) {
    const VDP2Regs &regs = VDP2GetRegs();

    y = VDP2GetY(y);

    // Calculate window for NBGs and RBGs
    for (int i = 0; i < 5; i++) {
        auto &bgParams = regs.bgParams[i];
        auto &bgWindow = m_bgWindows[i];

        VDP2CalcWindow(y, bgParams.windowSet, regs.windowParams, bgWindow);
    }

    // Calculate window for rotation parameters
    VDP2CalcWindow(y, regs.commonRotParams.windowSet, regs.windowParams, m_rotParamsWindow);

    // Calculate window for sprite layer
    VDP2CalcWindow(y, regs.spriteParams.windowSet, regs.windowParams, m_spriteLayerState.window);

    // Calculate window for color calculations
    VDP2CalcWindow(y, regs.colorCalcParams.windowSet, regs.windowParams, m_colorCalcWindow);
}

template <bool hasSpriteWindow>
FORCE_INLINE void VDP::VDP2CalcWindow(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                                      const std::array<WindowParams, 2> &windowParams,
                                      std::array<bool, kMaxResH> &windowState) {
    // If no windows are enabled, consider the pixel outside of windows
    if (!std::any_of(windowSet.enabled.begin(), windowSet.enabled.end(), std::identity{})) {
        windowState.fill(false);
        return;
    }

    if (windowSet.logic == WindowLogic::And) {
        VDP2CalcWindowAnd(y, windowSet, windowParams, windowState);
    } else {
        VDP2CalcWindowOr(y, windowSet, windowParams, windowState);
    }
}

template <bool hasSpriteWindow>
FORCE_INLINE void VDP::VDP2CalcWindowAnd(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                                         const std::array<WindowParams, 2> &windowParams,
                                         std::array<bool, kMaxResH> &windowState) {
    // Initialize to all inside if using AND logic
    windowState.fill(true);

    // Check normal windows
    for (int i = 0; i < 2; i++) {
        // Skip if disabled
        if (!windowSet.enabled[i]) {
            continue;
        }

        const WindowParams &windowParam = windowParams[i];
        const bool inverted = windowSet.inverted[i];

        // Check vertical coordinate
        //
        // Truth table: (state: false=outside, true=inside)
        // state  inverted  result   st != ao
        // false  false     outside  false
        // true   false     inside   true
        // false  true      inside   true
        // true   true      outside  false
        const bool insideY = y >= windowParam.startY && y <= windowParam.endY;
        if (!insideY && !inverted) {
            // Short-circuit
            windowState.fill(false);
            return;
        }

        sint16 startX = windowParam.startX;
        sint16 endX = windowParam.endX;

        // Read line window if enabled
        if (windowParam.lineWindowTableEnable) {
            const uint32 address = windowParam.lineWindowTableAddress + y * sizeof(uint16) * 2;
            sint16 startVal = VDP2ReadRendererVRAM<uint16>(address + 0);
            sint16 endVal = VDP2ReadRendererVRAM<uint16>(address + 2);

            // Some games set out-of-range window parameters and expects them to work.
            // It seems like window coordinates should be signed...
            //
            // Panzer Dragoon 2 Zwei:
            //   0000 to FFFE -> empty window
            //   FFFE to 02C0 -> full line
            //
            // Panzer Dragoon Saga:
            //   0000 to FFFF -> empty window
            //
            // Handle these cases here
            if (startVal < 0) {
                startVal = 0;
            }
            if (endVal < 0) {
                if (startVal >= endVal) {
                    startVal = 0x3FF;
                }
                endVal = 0;
            }

            startX = bit::extract<0, 9>(startVal);
            endX = bit::extract<0, 9>(endVal);
        }

        // For normal screen modes, X coordinates don't use bit 0
        if (VDP2GetRegs().TVMD.HRESOn < 2) {
            startX >>= 1;
            endX >>= 1;
        }

        // Fill in horizontal coordinate
        if (inverted) {
            if (startX < windowState.size()) {
                endX = std::min<sint16>(endX, windowState.size() - 1);
                std::fill(windowState.begin() + startX, windowState.begin() + endX + 1, false);
            }
        } else {
            std::fill_n(windowState.begin(), startX, false);
            if (endX < windowState.size()) {
                std::fill(windowState.begin() + endX + 1, windowState.end(), false);
            }
        }
    }

    // Check sprite window
    if constexpr (hasSpriteWindow) {
        if (windowSet.enabled[2]) {
            const bool inverted = windowSet.inverted[2];
            for (uint32 x = 0; x < m_HRes; x++) {
                windowState[x] &= m_spriteLayerState.attrs[x].shadowOrWindow != inverted;
            }
        }
    }
}

template <bool hasSpriteWindow>
FORCE_INLINE void VDP::VDP2CalcWindowOr(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                                        const std::array<WindowParams, 2> &windowParams,
                                        std::array<bool, kMaxResH> &windowState) {
    // Initialize to all outside if using OR logic
    windowState.fill(false);

    // Check normal windows
    for (int i = 0; i < 2; i++) {
        // Skip if disabled
        if (!windowSet.enabled[i]) {
            continue;
        }

        const WindowParams &windowParam = windowParams[i];
        const bool inverted = windowSet.inverted[i];

        // Check vertical coordinate
        //
        // Truth table: (state: false=outside, true=inside)
        // state  inverted  result   st != ao
        // false  false     outside  false
        // true   false     inside   true
        // false  true      inside   true
        // true   true      outside  false
        const bool insideY = y >= windowParam.startY && y <= windowParam.endY;
        if (!insideY && inverted) {
            // Short-circuit
            windowState.fill(true);
            return;
        }

        sint16 startX = windowParam.startX;
        sint16 endX = windowParam.endX;

        // Read line window if enabled
        if (windowParam.lineWindowTableEnable) {
            const uint32 address = windowParam.lineWindowTableAddress + y * sizeof(uint16) * 2;
            sint16 startVal = VDP2ReadRendererVRAM<uint16>(address + 0);
            sint16 endVal = VDP2ReadRendererVRAM<uint16>(address + 2);

            // Some games set out-of-range window parameters and expects them to work.
            // It seems like window coordinates should be signed...
            //
            // Panzer Dragoon 2 Zwei:
            //   0000 to FFFE -> empty window
            //   FFFE to 02C0 -> full line
            //
            // Panzer Dragoon Saga:
            //   0000 to FFFF -> empty window
            //
            // Handle these cases here
            if (startVal < 0) {
                startVal = 0;
            }
            if (endVal < 0) {
                if (startVal >= endVal) {
                    startVal = 0x3FF;
                }
                endVal = 0;
            }

            startX = bit::extract<0, 9>(startVal);
            endX = bit::extract<0, 9>(endVal);
        }

        // For normal screen modes, X coordinates don't use bit 0
        if (VDP2GetRegs().TVMD.HRESOn < 2) {
            startX >>= 1;
            endX >>= 1;
        }

        // Fill in horizontal coordinate
        if (inverted) {
            std::fill_n(windowState.begin(), startX, true);
            if (endX < windowState.size()) {
                std::fill(windowState.begin() + endX + 1, windowState.end(), true);
            }

        } else {
            if (startX < windowState.size()) {
                endX = std::min<sint16>(endX, windowState.size() - 1);
                if (startX <= endX) {
                    std::fill(windowState.begin() + startX, windowState.begin() + endX + 1, true);
                }
            }
        }
    }

    // Check sprite window
    if constexpr (hasSpriteWindow) {
        if (windowSet.enabled[2]) {
            const bool inverted = windowSet.inverted[2];
            for (uint32 x = 0; x < m_HRes; x++) {
                windowState[x] |= m_spriteLayerState.attrs[x].shadowOrWindow != inverted;
            }
        }
    }
}

FORCE_INLINE void VDP::VDP2CalcAccessCycles() {
    VDP2Regs &regs2 = VDP2GetRegs();

    if (!regs2.bgEnabled[5]) {
        // Translate VRAM access cycles for vertical cell scroll data into increment and offset for NBG0 and NBG1
        // and access flags for each VRAM bank for all NBGs.
        //
        // Some games set up "illegal" access patterns which we have to honor. This is an approximation of the real
        // thing, since this VDP emulator does not actually perform the accesses described by the CYCxn registers.

        if (regs2.cyclePatterns.dirty) [[unlikely]] {
            regs2.cyclePatterns.dirty = false;

            m_vertCellScrollInc = 0;
            uint32 vcellAccessOffset = 0;

            // Check in which bank the vertical cell scroll table is located at
            uint32 vcellBank = regs2.verticalCellScrollTableAddress >> 17;
            if (vcellBank < 2 && !regs2.vramControl.partitionVRAMA) {
                vcellBank = 0;
            } else if (!regs2.vramControl.partitionVRAMB) {
                vcellBank = 2;
            }

            const bool useVertScrollNBG0 = regs2.bgParams[1].verticalCellScrollEnable;
            const bool useVertScrollNBG1 = regs2.bgParams[2].verticalCellScrollEnable;

            // Update cycle accesses
            for (uint32 bank = 0; bank < 4; ++bank) {
                for (auto access : regs2.cyclePatterns.timings[bank]) {
                    switch (access) {
                    case CyclePatterns::VCellScrollNBG0:
                        if (useVertScrollNBG0 && bank == vcellBank) {
                            m_vertCellScrollInc += sizeof(uint32);
                            m_normBGLayerStates[0].vertCellScrollOffset = vcellAccessOffset;
                            vcellAccessOffset += sizeof(uint32);
                        }
                        break;
                    case CyclePatterns::VCellScrollNBG1:
                        if (useVertScrollNBG1 && bank == vcellBank) {
                            m_vertCellScrollInc += sizeof(uint32);
                            m_normBGLayerStates[1].vertCellScrollOffset = vcellAccessOffset;
                            vcellAccessOffset += sizeof(uint32);
                        }
                        break;
                    default: break;
                    }
                }
            }
        }
    }
}

void VDP::VDP2DrawLine(uint32 y) {
    devlog::trace<grp::vdp2_render>("Drawing line {}", y);

    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();

    // If starting a new frame, compute access cycles
    if (y == 0) {
        VDP2CalcAccessCycles();
    }

    using FnDrawLayer = void (VDP::*)(uint32 y);

    // Lookup table of sprite drawing functions
    // Indexing: [colorMode][rotate]
    static constexpr auto fnDrawSprite = [] {
        std::array<std::array<FnDrawLayer, 2>, 4> arr{};

        util::constexpr_for<2 * 4>([&](auto index) {
            const uint32 cmIndex = bit::extract<0, 1>(index());
            const uint32 rotIndex = bit::extract<2>(index());

            const uint32 colorMode = cmIndex <= 2 ? cmIndex : 2;
            const bool rotate = rotIndex;
            arr[cmIndex][rotate] = &VDP::VDP2DrawSpriteLayer<colorMode, rotate>;
        });

        return arr;
    }();

    const uint32 colorMode = regs2.vramControl.colorRAMMode;
    const bool rotate = regs1.fbRotEnable;

    // Precalculate window state
    VDP2CalcWindows(y);

    // Load rotation parameters if any of the RBG layers is enabled
    if (regs2.bgEnabled[4] || regs2.bgEnabled[5]) {
        VDP2CalcRotationParameterTables(y);
    }

    // Draw line color and back screen layers
    VDP2DrawLineColorAndBackScreens(y);

    // Draw sprite layer
    (this->*fnDrawSprite[colorMode][rotate])(y);

    // Draw background layers
    if (regs2.bgEnabled[5]) {
        VDP2DrawRotationBG<0>(y, colorMode); // RBG0
        VDP2DrawRotationBG<1>(y, colorMode); // RBG1
    } else {
        VDP2DrawRotationBG<0>(y, colorMode); // RBG0
        VDP2DrawNormalBG<0>(y, colorMode);   // NBG0
        VDP2DrawNormalBG<1>(y, colorMode);   // NBG1
        VDP2DrawNormalBG<2>(y, colorMode);   // NBG2
        VDP2DrawNormalBG<3>(y, colorMode);   // NBG3
    }

    // Compose image
    VDP2ComposeLine(y);
}

FORCE_INLINE void VDP::VDP2DrawLineColorAndBackScreens(uint32 y) {
    const VDP2Regs &regs = VDP2GetRegs();

    const LineBackScreenParams &lineParams = regs.lineScreenParams;
    const LineBackScreenParams &backParams = regs.backScreenParams;

    // Read line color screen color
    {
        const uint32 line = lineParams.perLine ? y : 0;
        const uint32 address = lineParams.baseAddress + line * sizeof(uint16);
        const uint32 cramAddress = VDP2ReadRendererVRAM<uint16>(address) * sizeof(uint16);
        m_lineBackLayerState.lineColor = VDP2ReadRendererColor5to8(cramAddress);
    }

    // Read back screen color
    {
        const uint32 line = backParams.perLine ? y : 0;
        const uint32 address = backParams.baseAddress + line * sizeof(Color555);
        const Color555 color555{.u16 = VDP2ReadRendererVRAM<uint16>(address)};
        m_lineBackLayerState.backColor = ConvertRGB555to888(color555);
    }
}

template <uint32 colorMode, bool rotate>
NO_INLINE void VDP::VDP2DrawSpriteLayer(uint32 y) {
    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();

    // VDP1 scaling:
    // 2x horz: VDP1 TVM=000 and VDP2 HRESO=01x
    const bool doubleResH =
        !regs1.hdtvEnable && !regs1.fbRotEnable && !regs1.pixel8Bits && (regs2.TVMD.HRESOn & 0b110) == 0b010;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 maxX = m_HRes >> xShift;

    auto &layerState = m_layerStates[0];
    auto &spriteLayerState = m_spriteLayerState;

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;

        const auto &spriteFB = m_spriteFB[VDP1GetDisplayFBIndex()];
        const uint32 spriteFBOffset = [&] {
            if constexpr (rotate) {
                const auto &rotParamState = m_rotParamStates[0];
                const auto &screenCoord = rotParamState.screenCoords[x];
                const sint32 sx = screenCoord.x() >> 16;
                const sint32 sy = screenCoord.y() >> 16;
                return sx + sy * regs1.fbSizeH;
            } else {
                return x + y * regs1.fbSizeH;
            }
        }();

        const SpriteParams &params = regs2.spriteParams;
        auto &pixel = layerState.pixels[xx];
        auto &attr = spriteLayerState.attrs[xx];

        util::ScopeGuard sgDoublePixel{[&] {
            if (doubleResH) {
                layerState.pixels[xx + 1] = pixel;
                spriteLayerState.attrs[xx + 1] = attr;
            }
        }};

        if (params.mixedFormat) {
            const uint16 spriteDataValue = util::ReadBE<uint16>(&spriteFB[(spriteFBOffset * sizeof(uint16)) & 0x3FFFE]);
            if (bit::test<15>(spriteDataValue)) {
                // RGB data
                pixel.color = ConvertRGB555to888(Color555{spriteDataValue});
                pixel.transparent = false;
                pixel.priority = params.priorities[0];
                attr.colorCalcRatio = params.colorCalcRatios[0];
                attr.shadowOrWindow = false;
                attr.normalShadow = false;
                continue;
            }
        }

        // Palette data
        const SpriteData spriteData = VDP2FetchSpriteData(spriteFBOffset);
        const uint32 colorIndex = params.colorDataOffset + spriteData.colorData;
        pixel.color = VDP2FetchCRAMColor<colorMode>(0, colorIndex);
        pixel.transparent = spriteData.colorData == 0;
        pixel.priority = params.priorities[spriteData.priority];
        attr.colorCalcRatio = params.colorCalcRatios[spriteData.colorCalcRatio];
        attr.shadowOrWindow = spriteData.shadowOrWindow;
        attr.normalShadow = spriteData.normalShadow;
    }
}

template <uint32 bgIndex>
FORCE_INLINE void VDP::VDP2DrawNormalBG(uint32 y, uint32 colorMode) {
    static_assert(bgIndex < 4, "Invalid NBG index");

    using FnDraw =
        void (VDP::*)(uint32 y, const BGParams &, LayerState &, NormBGLayerState &, const std::array<bool, kMaxResH> &);

    // Lookup table of scroll BG drawing functions
    // Indexing: [charMode][fourCellChar][colorFormat][colorMode]
    static constexpr auto fnDrawScroll = [] {
        std::array<std::array<std::array<std::array<FnDraw, 4>, 8>, 2>, 3> arr{};

        util::constexpr_for<3 * 2 * 8 * 4>([&](auto index) {
            const uint32 fcc = bit::extract<0>(index());
            const uint32 cf = bit::extract<1, 3>(index());
            const uint32 clm = bit::extract<4, 5>(index());
            const uint32 chm = bit::extract<6, 7>(index());

            const CharacterMode chmEnum = static_cast<CharacterMode>(chm);
            const ColorFormat cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = clm <= 2 ? clm : 2;
            arr[chm][fcc][cf][clm] = &VDP::VDP2DrawNormalScrollBG<chmEnum, fcc, cfEnum, colorMode>;
        });

        return arr;
    }();

    // Lookup table of bitmap BG drawing functions
    // Indexing: [colorFormat]
    static constexpr auto fnDrawBitmap = [] {
        std::array<std::array<FnDraw, 4>, 8> arr{};

        util::constexpr_for<8 * 4>([&](auto index) {
            const uint32 cf = bit::extract<0, 2>(index());
            const uint32 cm = bit::extract<3, 4>(index());

            const ColorFormat cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = cm <= 2 ? cm : 2;
            arr[cf][cm] = &VDP::VDP2DrawNormalBitmapBG<cfEnum, colorMode>;
        });

        return arr;
    }();

    if (!m_layerStates[bgIndex + 2].enabled) {
        return;
    }

    const VDP2Regs &regs = VDP2GetRegs();
    const BGParams &bgParams = regs.bgParams[bgIndex + 1];
    LayerState &layerState = m_layerStates[bgIndex + 2];
    NormBGLayerState &bgState = m_normBGLayerStates[bgIndex];
    const auto &windowState = m_bgWindows[bgIndex + 1];

    if constexpr (bgIndex < 2) {
        VDP2UpdateLineScreenScroll(y, bgParams, bgState);
    }

    const uint32 cf = static_cast<uint32>(bgParams.colorFormat);
    if (bgParams.bitmap) {
        (this->*fnDrawBitmap[cf][colorMode])(y, bgParams, layerState, bgState, windowState);
    } else {
        const bool twc = bgParams.twoWordChar;
        const bool fcc = bgParams.cellSizeShift;
        const bool exc = bgParams.extChar;
        const uint32 chm = static_cast<uint32>(twc   ? CharacterMode::TwoWord
                                               : exc ? CharacterMode::OneWordExtended
                                                     : CharacterMode::OneWordStandard);
        (this->*fnDrawScroll[chm][fcc][cf][colorMode])(y, bgParams, layerState, bgState, windowState);
    }

    if (bgParams.mosaicEnable) {
        bgState.mosaicCounterY++;
        if (bgState.mosaicCounterY >= regs.mosaicV) {
            bgState.mosaicCounterY = 0;
        }
    }
}

template <uint32 bgIndex>
FORCE_INLINE void VDP::VDP2DrawRotationBG(uint32 y, uint32 colorMode) {
    static_assert(bgIndex < 2, "Invalid RBG index");

    static constexpr bool selRotParam = bgIndex == 0;

    using FnDraw = void (VDP::*)(uint32 y, const BGParams &, LayerState &, const std::array<bool, kMaxResH> &);

    // Lookup table of scroll BG drawing functions
    // Indexing: [charMode][fourCellChar][colorFormat][colorMode]
    static constexpr auto fnDrawScroll = [] {
        std::array<std::array<std::array<std::array<FnDraw, 4>, 8>, 2>, 3> arr{};

        util::constexpr_for<3 * 2 * 8 * 4>([&](auto index) {
            const uint32 fcc = bit::extract<0>(index());
            const uint32 cf = bit::extract<1, 3>(index());
            const uint32 clm = bit::extract<4, 5>(index());
            const uint32 chm = bit::extract<6, 7>(index());

            const CharacterMode chmEnum = static_cast<CharacterMode>(chm);
            const ColorFormat cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = clm <= 2 ? clm : 2;
            arr[chm][fcc][cf][clm] = &VDP::VDP2DrawRotationScrollBG<selRotParam, chmEnum, fcc, cfEnum, colorMode>;
        });

        return arr;
    }();

    // Lookup table of bitmap BG drawing functions
    // Indexing: [colorFormat][colorMode]
    static constexpr auto fnDrawBitmap = [] {
        std::array<std::array<FnDraw, 4>, 8> arr{};

        util::constexpr_for<8 * 4>([&](auto index) {
            const uint32 cf = bit::extract<0, 2>(index());
            const uint32 cm = bit::extract<3, 4>(index());

            const ColorFormat cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = cm <= 2 ? cm : 2;
            arr[cf][cm] = &VDP::VDP2DrawRotationBitmapBG<selRotParam, cfEnum, colorMode>;
        });

        return arr;
    }();

    if (!m_layerStates[bgIndex].enabled) {
        return;
    }

    const VDP2Regs &regs = VDP2GetRegs();
    const BGParams &bgParams = regs.bgParams[bgIndex];
    LayerState &layerState = m_layerStates[bgIndex + 1];
    const auto &windowState = m_bgWindows[bgIndex];

    const uint32 cf = static_cast<uint32>(bgParams.colorFormat);
    if (bgParams.bitmap) {
        (this->*fnDrawBitmap[cf][colorMode])(y, bgParams, layerState, windowState);
    } else {
        const bool twc = bgParams.twoWordChar;
        const bool fcc = bgParams.cellSizeShift;
        const bool exc = bgParams.extChar;
        const uint32 chm = static_cast<uint32>(twc   ? CharacterMode::TwoWord
                                               : exc ? CharacterMode::OneWordExtended
                                                     : CharacterMode::OneWordStandard);
        (this->*fnDrawScroll[chm][fcc][cf][colorMode])(y, bgParams, layerState, windowState);
    }
}

// Lookup table for color offset effects.
// Indexing: [colorOffset][channelValue]
static const auto kColorOffsetLUT = [] {
    std::array<std::array<uint8, 256>, 512> arr{};
    for (uint32 i = 0; i < 512; i++) {
        const sint32 ofs = bit::sign_extend<9>(i);
        for (uint32 c = 0; c < 256; c++) {
            arr[i][c] = std::clamp<sint32>(c + ofs, 0, 255);
        }
    }
    return arr;
}();

FORCE_INLINE void VDP::VDP2ComposeLine(uint32 y) {
    const VDP2Regs &regs = VDP2GetRegs();
    const auto &colorCalcParams = regs.colorCalcParams;

    // Extended color calculations (only in normal TV modes)
    const bool useExtendedColorCalc = colorCalcParams.extendedColorCalcEnable && regs.TVMD.HRESOn < 2;

    y = VDP2GetY(y);

    if (!regs.TVMD.DISP) {
        std::fill_n(&m_framebuffer[y * m_HRes], m_HRes, 0xFF000000);
        return;
    }

    // Determine layer orders
    alignas(16) std::array<std::array<LayerIndex, 3>, kMaxResH> scanline_layers;
    scanline_layers.fill({LYR_Back, LYR_Back, LYR_Back});

    alignas(16) std::array<std::array<uint8, 3>, kMaxResH> scanline_layerPrios;
    scanline_layerPrios.fill({0, 0, 0});

    for (uint32 x = 0; x < m_HRes; x++) {
        std::array<LayerIndex, 3> &layers = scanline_layers[x];
        std::array<uint8, 3> &layerPrios = scanline_layerPrios[x];

        for (int layer = 0; layer < m_layerStates.size(); layer++) {
            const LayerState &state = m_layerStates[layer];
            if (!state.enabled) {
                continue;
            }

            const Pixel &pixel = state.pixels[x];
            if (pixel.transparent) {
                continue;
            }
            if (pixel.priority == 0) {
                continue;
            }
            if (layer == LYR_Sprite) {
                const auto &attr = m_spriteLayerState.attrs[x];
                if (attr.normalShadow) {
                    continue;
                }
            }

            // Insert the layer into the appropriate position in the stack
            // - Higher priority beats lower priority
            // - If same priority, lower Layer index beats higher Layer index
            // - layers[0] is topmost (first) layer
            for (int i = 0; i < 3; i++) {
                if (pixel.priority > layerPrios[i] || (pixel.priority == layerPrios[i] && layer < layers[i])) {
                    // Push layers back
                    for (int j = 2; j > i; j--) {
                        layers[j] = layers[j - 1];
                        layerPrios[j] = layerPrios[j - 1];
                    }
                    layers[i] = static_cast<LayerIndex>(layer);
                    layerPrios[i] = pixel.priority;
                    break;
                }
            }
        }
    }

    // Gather pixels for each layer
    alignas(16) std::array<Color888, kMaxResH> layer0Pixels = {};
    alignas(16) std::array<Color888, kMaxResH> layer1Pixels = {};
    alignas(16) std::array<Color888, kMaxResH> layer2Pixels = {};

    for (uint32 x = 0; x < m_HRes; x++) {
        // Retrieves the color of the given layer
        auto getLayerColor = [&](LayerIndex layer) -> Color888 {
            if (layer == LYR_Back) {
                return m_lineBackLayerState.backColor;
            } else {
                const LayerState &state = m_layerStates[layer];
                const Pixel &pixel = state.pixels[x];
                return pixel.color;
            }
        };

        layer0Pixels[x] = getLayerColor(scanline_layers[x][0]);
        layer1Pixels[x] = getLayerColor(scanline_layers[x][1]);
        layer2Pixels[x] = getLayerColor(scanline_layers[x][2]);
    }

    // Apply Extended color calc to layer 1
    if (useExtendedColorCalc) {
        for (uint32 x = 0; x < m_HRes; ++x) {
            Color888 &pixel = layer1Pixels[x];
            // TODO: honor color RAM mode + palette/RGB format restrictions
            // - modes 1 and 2 don't blend layers if the bottom layer uses palette color

            // HACK: assuming color RAM mode 0 for now (aka no restrictions)
            const Color888 &l2Color = layer2Pixels[x];
            pixel.r = (pixel.r + l2Color.r) / 2;
            pixel.g = (pixel.g + l2Color.g) / 2;
            pixel.b = (pixel.b + l2Color.b) / 2;
        }
    }

    // Gather line-color data
    alignas(16) std::bitset<kMaxResH> layer0LineColorEnabled = {};
    alignas(16) std::array<Color888, kMaxResH> layer0LineColors = {};
    for (uint32 x = 0; x < m_HRes; x++) {

        auto isLineColorEnabled = [&](LayerIndex layer) {
            if (layer == LYR_Sprite) {
                return regs.spriteParams.lineColorScreenEnable;
            } else if (layer == LYR_Back) {
                return false;
            } else {
                return regs.bgParams[layer - LYR_RBG0].lineColorScreenEnable;
            }
        };

        auto getLineColor = [&](LayerIndex layer) {
            if (layer == LYR_RBG0 || (layer == LYR_NBG0_RBG1 && regs.bgEnabled[5])) {
                const auto &rotParams = regs.rotParams[layer - LYR_RBG0];
                if (rotParams.coeffTableEnable && rotParams.coeffUseLineColorData) {
                    return m_rotParamStates[layer - LYR_RBG0].lineColor[x];
                } else {
                    return m_lineBackLayerState.lineColor;
                }
            } else {
                return m_lineBackLayerState.lineColor;
            }
        };

        layer0LineColorEnabled[x] = isLineColorEnabled(scanline_layers[x][0]);
        layer0LineColors[x] = getLineColor(scanline_layers[x][0]);
    }

    // Blend line color screen if top layer uses it
    if (layer0LineColorEnabled.any()) {
        if (useExtendedColorCalc) {
            // Average color
            // TODO: _mm_avg_epu8(pavgb)
            for (uint32 x = 0; x < m_HRes; ++x) {
                Color888 &pixel = layer1Pixels[x];
                const Color888 &lineColor = layer0LineColors[x];
                pixel.r = (lineColor.r + pixel.r) / 2;
                pixel.g = (lineColor.g + pixel.g) / 2;
                pixel.b = (lineColor.b + pixel.b) / 2;
            }
        } else {
            // Alpha composite
            for (uint32 x = 0; x < m_HRes; ++x) {
                Color888 &pixel = layer1Pixels[x];
                const Color888 &lineColor = layer0LineColors[x];
                const uint8 ratio = regs.lineScreenParams.colorCalcRatio;
                pixel.r = lineColor.r + ((int)pixel.r - (int)lineColor.r) * ratio / 32;
                pixel.g = lineColor.g + ((int)pixel.g - (int)lineColor.g) * ratio / 32;
                pixel.b = lineColor.b + ((int)pixel.b - (int)lineColor.b) * ratio / 32;
            }
        }
    }

    // Gather shadow data
    alignas(16) std::bitset<kMaxResH> layer0ShadowEnabled = {};
    for (uint32 x = 0; x < m_HRes; x++) {
        auto isShadowEnabled = [&](LayerIndex layer) {
            if (layer == LYR_Sprite) {
                return m_spriteLayerState.attrs[x].shadowOrWindow;
            } else if (layer == LYR_Back) {
                return regs.backScreenParams.shadowEnable;
            } else {
                return regs.bgParams[layer - LYR_RBG0].shadowEnable;
            }
        };
        const bool isNormalShadow = m_spriteLayerState.attrs[x].normalShadow;
        const bool isMSBShadow = !regs.spriteParams.spriteWindowEnable && m_spriteLayerState.attrs[x].shadowOrWindow;

        layer0ShadowEnabled[x] = isShadowEnabled(scanline_layers[x][0]) || isNormalShadow || isMSBShadow;
    }

    // Gether layer color calc data
    alignas(16) std::bitset<kMaxResH> layer0ColorCalcEnabled = {};
    alignas(16) std::bitset<kMaxResH> layer1ColorCalcEnabled = {};
    for (uint32 x = 0; x < m_HRes; x++) {
        const auto isColorCalcEnabled = [&](LayerIndex layer) {
            if (layer == LYR_Sprite) {
                const SpriteParams &spriteParams = regs.spriteParams;
                if (!spriteParams.colorCalcEnable) {
                    return false;
                }

                const Pixel &pixel = m_layerStates[LYR_Sprite].pixels[x];

                using enum SpriteColorCalculationCondition;
                switch (spriteParams.colorCalcCond) {
                case PriorityLessThanOrEqual: return pixel.priority <= spriteParams.colorCalcValue;
                case PriorityEqual: return pixel.priority == spriteParams.colorCalcValue;
                case PriorityGreaterThanOrEqual: return pixel.priority >= spriteParams.colorCalcValue;
                case MsbEqualsOne: return pixel.color.msb == 1;
                default: util::unreachable();
                }
            } else if (layer == LYR_Back) {
                return regs.backScreenParams.colorCalcEnable;
            } else {
                return regs.bgParams[layer - LYR_RBG0].colorCalcEnable;
            }
        };
        const auto isTopLayerColorCalcEnabled = [&]() -> bool {
            if (!isColorCalcEnabled(scanline_layers[x][0])) {
                return false;
            }
            if (m_colorCalcWindow[x]) {
                return false;
            }
            if (scanline_layers[x][0] == LYR_Back || scanline_layers[x][0] == LYR_Sprite) {
                return true;
            }
            return m_layerStates[scanline_layers[x][0]].pixels[x].specialColorCalc;
        };
        layer0ColorCalcEnabled[x] = isTopLayerColorCalcEnabled();
        layer1ColorCalcEnabled[x] = isColorCalcEnabled(scanline_layers[x][1]);
    }

    // Gather extended color ratio info
    alignas(16) std::array<uint8, kMaxResH> scanline_ratio = {};
    for (uint32 x = 0; x < m_HRes; x++) {
        auto getColorCalcRatio = [&](LayerIndex layer) {
            if (layer == LYR_Sprite) {
                return m_spriteLayerState.attrs[x].colorCalcRatio;
            } else if (layer == LYR_Back) {
                return regs.backScreenParams.colorCalcRatio;
            } else {
                return regs.bgParams[layer - LYR_RBG0].colorCalcRatio;
            }
        };
        scanline_ratio[x] =
            getColorCalcRatio(colorCalcParams.useSecondScreenRatio ? scanline_layers[x][1] : scanline_layers[x][0]);
    }

    // Gather color offset info
    alignas(16) std::bitset<kMaxResH> layer0ColorOffsetEnabled = {};
    for (uint32 x = 0; x < m_HRes; x++) {
        layer0ColorOffsetEnabled[x] = regs.colorOffsetEnable[scanline_layers[x][0]];
    }

    const std::span<Color888> framebufferOutput(reinterpret_cast<Color888 *>(&m_framebuffer[y * m_HRes]), m_HRes);

    // Blend layer 0 and layer 1
    if (layer0ColorCalcEnabled.any()) {
        if (colorCalcParams.useAdditiveBlend) {
            // Saturated add
            // TODO: _mm_adds_epi8(paddsb)
            for (uint32 x = 0; Color888 & outputColor : framebufferOutput) {
                const Color888 &topColor = layer0Pixels[x];
                const Color888 &btmColor = layer1Pixels[x];

                if (layer0ColorCalcEnabled[x]) {
                    outputColor.r = std::min<uint32>(topColor.r + btmColor.r, 255u);
                    outputColor.g = std::min<uint32>(topColor.g + btmColor.g, 255u);
                    outputColor.b = std::min<uint32>(topColor.b + btmColor.b, 255u);
                } else {
                    outputColor = topColor;
                }
                ++x;
            }
        } else {
            // Alpha composite
            for (uint32 x = 0; Color888 & outputColor : framebufferOutput) {
                const Color888 &topColor = layer0Pixels[x];
                const Color888 &btmColor = layer1Pixels[x];

                if (layer0ColorCalcEnabled[x]) {
                    const uint8 &ratio = scanline_ratio[x];
                    outputColor.r = btmColor.r + ((int)topColor.r - (int)btmColor.r) * ratio / 32;
                    outputColor.g = btmColor.g + ((int)topColor.g - (int)btmColor.g) * ratio / 32;
                    outputColor.b = btmColor.b + ((int)topColor.b - (int)btmColor.b) * ratio / 32;
                } else {
                    outputColor = topColor;
                }
                ++x;
            }
        }
    } else {
        std::copy_n(layer0Pixels.cbegin(), framebufferOutput.size(), framebufferOutput.begin());
    }

    // Apply sprite shadow
    if (layer0ShadowEnabled.any()) {
        for (uint32 x = 0; Color888 & outputColor : framebufferOutput) {
            if (layer0ShadowEnabled[x]) {
                outputColor.r >>= 1u;
                outputColor.g >>= 1u;
                outputColor.b >>= 1u;
            }
            ++x;
        }
    }

    // Apply color offset if enabled
    if (layer0ColorOffsetEnabled.any()) {
        for (uint32 x = 0; Color888 & outputColor : framebufferOutput) {
            if (layer0ColorOffsetEnabled[x]) {
                const auto &colorOffset = regs.colorOffset[regs.colorOffsetSelect[scanline_layers[x][0]]];
                if (colorOffset.nonZero) {
                    outputColor.r = kColorOffsetLUT[colorOffset.r][outputColor.r];
                    outputColor.g = kColorOffsetLUT[colorOffset.g][outputColor.g];
                    outputColor.b = kColorOffsetLUT[colorOffset.b][outputColor.b];
                }
            }
            ++x;
        }
    }

    // Opaque alpha
    for (Color888 &outputColor : framebufferOutput) {
        outputColor.u32 |= 0xFF000000;
    }
}

template <VDP::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode>
NO_INLINE void VDP::VDP2DrawNormalScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                           NormBGLayerState &bgState, const std::array<bool, kMaxResH> &windowState) {
    const VDP2Regs &regs = VDP2GetRegs();

    uint32 fracScrollX = bgState.fracScrollX + bgParams.scrollAmountH;
    const uint32 fracScrollY = bgState.fracScrollY + bgParams.scrollAmountV;
    bgState.fracScrollY += bgParams.scrollIncV;
    if (regs.TVMD.LSMDn == InterlaceMode::DoubleDensity) {
        bgState.fracScrollY += bgParams.scrollIncV;
    }

    uint32 cellScrollTableAddress = regs.verticalCellScrollTableAddress + bgState.vertCellScrollOffset;

    auto readCellScrollY = [&] {
        const uint32 value = VDP2ReadRendererVRAM<uint32>(cellScrollTableAddress);
        cellScrollTableAddress += m_vertCellScrollInc;
        return bit::extract<8, 26>(value);
    };

    uint8 mosaicCounterX = 0;
    uint32 cellScrollY = 0;

    if (bgParams.verticalCellScrollEnable) {
        // Read first vertical scroll amount if scrolled partway through a cell at the start of the line
        if (((fracScrollX >> 8u) & 7) != 0) {
            cellScrollY = readCellScrollY();
        }
    }

    for (uint32 x = 0; x < m_HRes; x++) {
        // Apply horizontal mosaic or vertical cell-scrolling
        // Mosaic takes priority
        if (bgParams.mosaicEnable) {
            // Apply horizontal mosaic
            const uint8 currMosaicCounterX = mosaicCounterX;
            mosaicCounterX++;
            if (mosaicCounterX >= regs.mosaicH) {
                mosaicCounterX = 0;
            }
            if (currMosaicCounterX > 0) {
                // Simply copy over the data from the previous pixel
                layerState.pixels[x] = layerState.pixels[x - 1];

                // Increment horizontal coordinate
                fracScrollX += bgState.scrollIncH;
                continue;
            }
        } else if (bgParams.verticalCellScrollEnable) {
            // Update vertical cell scroll amount
            if (((fracScrollX >> 8u) & 7) == 0) {
                cellScrollY = readCellScrollY();
            }
        }

        if (windowState[x]) {
            // Make pixel transparent if inside active window area
            layerState.pixels[x].transparent = true;
        } else {
            // Compute integer scroll screen coordinates
            const uint32 scrollX = fracScrollX >> 8u;
            const uint32 scrollY = ((fracScrollY + cellScrollY) >> 8u) - bgState.mosaicCounterY;
            const CoordU32 scrollCoord{scrollX, scrollY};

            // Plot pixel
            layerState.pixels[x] = VDP2FetchScrollBGPixel<false, charMode, fourCellChar, colorFormat, colorMode>(
                bgParams, bgParams.pageBaseAddresses, bgParams.pageShiftH, bgParams.pageShiftV, scrollCoord);
        }

        // Increment horizontal coordinate
        fracScrollX += bgState.scrollIncH;
    }
}

template <ColorFormat colorFormat, uint32 colorMode>
NO_INLINE void VDP::VDP2DrawNormalBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                           NormBGLayerState &bgState, const std::array<bool, kMaxResH> &windowState) {
    const VDP2Regs &regs = VDP2GetRegs();

    uint32 fracScrollX = bgState.fracScrollX + bgParams.scrollAmountH;
    const uint32 fracScrollY = bgState.fracScrollY + bgParams.scrollAmountV;
    bgState.fracScrollY += bgParams.scrollIncV;
    if (regs.TVMD.LSMDn == InterlaceMode::DoubleDensity) {
        bgState.fracScrollY += bgParams.scrollIncV;
    }

    uint32 cellScrollTableAddress = regs.verticalCellScrollTableAddress + bgState.vertCellScrollOffset;

    auto readCellScrollY = [&] {
        const uint32 value = VDP2ReadRendererVRAM<uint32>(cellScrollTableAddress);
        cellScrollTableAddress += m_vertCellScrollInc;
        return bit::extract<8, 26>(value);
    };

    uint32 mosaicCounterX = 0;
    uint32 cellScrollY = 0;

    for (uint32 x = 0; x < m_HRes; x++) {
        // Apply horizontal mosaic or vertical cell-scrolling
        // Mosaic takes priority
        if (bgParams.mosaicEnable) {
            // Apply horizontal mosaic
            const uint8 currMosaicCounterX = mosaicCounterX;
            mosaicCounterX++;
            if (mosaicCounterX >= regs.mosaicH) {
                mosaicCounterX = 0;
            }
            if (currMosaicCounterX > 0) {
                // Simply copy over the data from the previous pixel
                layerState.pixels[x] = layerState.pixels[x - 1];

                // Increment horizontal coordinate
                fracScrollX += bgState.scrollIncH;
                continue;
            }
        } else if (bgParams.verticalCellScrollEnable) {
            // Update vertical cell scroll amount
            if (((fracScrollX >> 8u) & 7) == 0) {
                cellScrollY = readCellScrollY();
            }
        }

        if (windowState[x]) {
            // Make pixel transparent if inside active window area
            layerState.pixels[x].transparent = true;
        } else {
            // Compute integer scroll screen coordinates
            const uint32 scrollX = fracScrollX >> 8u;
            const uint32 scrollY = ((fracScrollY + cellScrollY) >> 8u) - bgState.mosaicCounterY;
            const CoordU32 scrollCoord{scrollX, scrollY};

            // Plot pixel
            layerState.pixels[x] =
                VDP2FetchBitmapPixel<colorFormat, colorMode>(bgParams, bgParams.bitmapBaseAddress, scrollCoord);
        }

        // Increment horizontal coordinate
        fracScrollX += bgState.scrollIncH;
    }
}

template <bool selRotParam, VDP::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode>
NO_INLINE void VDP::VDP2DrawRotationScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                             const std::array<bool, kMaxResH> &windowState) {
    const VDP2Regs &regs = VDP2GetRegs();

    const bool doubleResH = regs.TVMD.HRESOn & 0b010;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 maxX = m_HRes >> xShift;

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;
        auto &pixel = layerState.pixels[xx];
        util::ScopeGuard sgDoublePixel{[&] {
            if (doubleResH) {
                layerState.pixels[xx + 1] = pixel;
            }
        }};

        const RotParamSelector rotParamSelector = selRotParam ? VDP2SelectRotationParameter(x, y) : RotParamA;

        const RotationParams &rotParams = regs.rotParams[rotParamSelector];
        const RotationParamState &rotParamState = m_rotParamStates[rotParamSelector];

        // Handle transparent pixels in coefficient table
        if (rotParams.coeffTableEnable && rotParamState.transparent[x]) {
            pixel.transparent = true;
            continue;
        }

        const sint32 fracScrollX = rotParamState.screenCoords[x].x();
        const sint32 fracScrollY = rotParamState.screenCoords[x].y();

        // Get integer scroll screen coordinates
        const uint32 scrollX = fracScrollX >> 16u;
        const uint32 scrollY = fracScrollY >> 16u;
        const CoordU32 scrollCoord{scrollX, scrollY};

        // Determine maximum coordinates and screen over process
        const bool usingFixed512 = rotParams.screenOverProcess == ScreenOverProcess::Fixed512;
        const bool usingRepeat = rotParams.screenOverProcess == ScreenOverProcess::Repeat;
        const uint32 maxScrollX = usingFixed512 ? 512 : ((512 * 4) << rotParams.pageShiftH);
        const uint32 maxScrollY = usingFixed512 ? 512 : ((512 * 4) << rotParams.pageShiftV);

        if (windowState[x]) {
            // Make pixel transparent if inside a window
            pixel.transparent = true;
        } else if ((scrollX < maxScrollX && scrollY < maxScrollY) || usingRepeat) {
            // Plot pixel
            pixel = VDP2FetchScrollBGPixel<true, charMode, fourCellChar, colorFormat, colorMode>(
                bgParams, rotParamState.pageBaseAddresses, rotParams.pageShiftH, rotParams.pageShiftV, scrollCoord);
        } else if (rotParams.screenOverProcess == ScreenOverProcess::RepeatChar) {
            // Out of bounds - repeat character
            const uint16 charData = rotParams.screenOverPatternName;

            // TODO: deduplicate code: VDP2FetchOneWordCharacter
            static constexpr bool largePalette = colorFormat != ColorFormat::Palette16;
            static constexpr bool extChar = charMode == CharacterMode::OneWordExtended;

            // Character number bit range from the 1-word character pattern data (charData)
            static constexpr uint32 baseCharNumStart = 0;
            static constexpr uint32 baseCharNumEnd = 9 + 2 * extChar;
            static constexpr uint32 baseCharNumPos = 2 * fourCellChar;

            // Upper character number bit range from the supplementary character number (bgParams.supplCharNum)
            static constexpr uint32 supplCharNumStart = 2 * fourCellChar + 2 * extChar;
            static constexpr uint32 supplCharNumEnd = 4;
            static constexpr uint32 supplCharNumPos = 10 + supplCharNumStart;
            // The lower bits are always in range 0..1 and only used if fourCellChar == true

            const uint32 baseCharNum = bit::extract<baseCharNumStart, baseCharNumEnd>(charData);
            const uint32 supplCharNum = bit::extract<supplCharNumStart, supplCharNumEnd>(bgParams.supplScrollCharNum);

            Character ch{};
            ch.charNum = (baseCharNum << baseCharNumPos) | (supplCharNum << supplCharNumPos);
            if constexpr (fourCellChar) {
                ch.charNum |= bit::extract<0, 1>(bgParams.supplScrollCharNum);
            }
            if constexpr (largePalette) {
                ch.palNum = bit::extract<12, 14>(charData) << 4u;
            } else {
                ch.palNum = bit::extract<12, 15>(charData) | bgParams.supplScrollPalNum;
            }
            ch.specColorCalc = bgParams.supplScrollSpecialColorCalc;
            ch.specPriority = bgParams.supplScrollSpecialPriority;
            ch.flipH = !extChar && bit::test<10>(charData);
            ch.flipV = !extChar && bit::test<11>(charData);

            const uint32 dotX = bit::extract<0, 2>(scrollX);
            const uint32 dotY = bit::extract<0, 2>(scrollY);
            const CoordU32 dotCoord{dotX, dotY};
            pixel = VDP2FetchCharacterPixel<colorFormat, colorMode>(bgParams, ch, dotCoord, 0);
        } else {
            // Out of bounds - transparent
            pixel.transparent = true;
        }
    }
}

template <bool selRotParam, ColorFormat colorFormat, uint32 colorMode>
NO_INLINE void VDP::VDP2DrawRotationBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                             const std::array<bool, kMaxResH> &windowState) {
    const VDP2Regs &regs = VDP2GetRegs();

    const bool doubleResH = regs.TVMD.HRESOn & 0b010;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 maxX = m_HRes >> xShift;

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;
        auto &pixel = layerState.pixels[xx];
        util::ScopeGuard sgDoublePixel{[&] {
            if (doubleResH) {
                layerState.pixels[xx + 1] = pixel;
            }
        }};
        const RotParamSelector rotParamSelector = selRotParam ? VDP2SelectRotationParameter(x, y) : RotParamA;

        const RotationParams &rotParams = regs.rotParams[rotParamSelector];
        const RotationParamState &rotParamState = m_rotParamStates[rotParamSelector];

        // Handle transparent pixels in coefficient table
        if (rotParams.coeffTableEnable && rotParamState.transparent[x]) {
            pixel.transparent = true;
            continue;
        }

        const sint32 fracScrollX = rotParamState.screenCoords[x].x();
        const sint32 fracScrollY = rotParamState.screenCoords[x].y();

        // Get integer scroll screen coordinates
        const uint32 scrollX = fracScrollX >> 16u;
        const uint32 scrollY = fracScrollY >> 16u;
        const CoordU32 scrollCoord{scrollX, scrollY};

        const bool usingFixed512 = rotParams.screenOverProcess == ScreenOverProcess::Fixed512;
        const bool usingRepeat = rotParams.screenOverProcess == ScreenOverProcess::Repeat;
        const uint32 maxScrollX = usingFixed512 ? 512 : bgParams.bitmapSizeH;
        const uint32 maxScrollY = usingFixed512 ? 512 : bgParams.bitmapSizeV;

        if (windowState[x]) {
            // Make pixel transparent if inside a window
            pixel.transparent = true;
        } else if ((scrollX < maxScrollX && scrollY < maxScrollY) || usingRepeat) {
            // Plot pixel
            pixel = VDP2FetchBitmapPixel<colorFormat, colorMode>(bgParams, rotParams.bitmapBaseAddress, scrollCoord);
        } else {
            // Out of bounds and no repeat
            pixel.transparent = true;
        }
    }
}

FORCE_INLINE VDP::RotParamSelector VDP::VDP2SelectRotationParameter(uint32 x, uint32 y) {
    const VDP2Regs &regs = VDP2GetRegs();

    const CommonRotationParams &commonRotParams = regs.commonRotParams;

    using enum RotationParamMode;
    switch (commonRotParams.rotParamMode) {
    case RotationParamA: return RotParamA;
    case RotationParamB: return RotParamB;
    case Coefficient:
        return regs.rotParams[0].coeffTableEnable && m_rotParamStates[0].transparent[x] ? RotParamB : RotParamA;
    case Window: return m_rotParamsWindow[x] ? RotParamB : RotParamA;
    }
    util::unreachable();
}

FORCE_INLINE bool VDP::VDP2CanFetchCoefficient(const RotationParams &params, uint32 coeffAddress) const {
    const VDP2Regs &regs = VDP2GetRegs();

    // Coefficients can always be fetched from CRAM
    if (regs.vramControl.colorRAMCoeffTableEnable) {
        return true;
    }

    const uint32 baseAddress = params.coeffTableAddressOffset;
    const uint32 offset = coeffAddress >> 10u;

    // Check that the VRAM bank containing the coefficient table is designated for coefficient data.
    // Return a default (transparent) coefficient if not.
    // Determine which bank is targeted
    const uint32 address = ((baseAddress + offset) * sizeof(uint32)) >> params.coeffDataSize;

    // Address is 19 bits wide when using 512 KiB VRAM.
    // Bank is designated by bits 17-18.
    uint32 bank = bit::extract<17, 18>(address);

    // RAMCTL.VRAMD and VRBMD specify if VRAM A and B respectively are partitioned into two blocks (when set).
    // If they're not partitioned, RDBSA0n/RDBSB0n designate the role of the whole block (VRAM-A or -B).
    // RDBSA1n/RDBSB1n designates the roles of the second half of the partitioned banks (VRAM-A1 or -A2).
    // Masking the bank index with VRAMD/VRBMD adjusts the bank index of the second half back to the first half so
    // we can uniformly handle both cases with one simple switch table.
    if (bank < 2) {
        bank &= ~(regs.vramControl.partitionVRAMA ^ 1);
    } else {
        bank &= ~(regs.vramControl.partitionVRAMB ^ 1);
    }

    switch (bank) {
    case 0: // VRAM-A0 or VRAM-A
        if (regs.vramControl.rotDataBankSelA0 != 1) {
            return false;
        }
        break;
    case 1: // VRAM-A1
        if (regs.vramControl.rotDataBankSelA1 != 1) {
            return false;
        }
        break;
    case 2: // VRAM-B0 or VRAM-B
        if (regs.vramControl.rotDataBankSelB0 != 1) {
            return false;
        }
        break;
    case 3: // VRAM-B1
        if (regs.vramControl.rotDataBankSelB1 != 1) {
            return false;
        }
        break;
    }

    return true;
}

FORCE_INLINE Coefficient VDP::VDP2FetchRotationCoefficient(const RotationParams &params, uint32 coeffAddress) {
    const VDP2Regs &regs = VDP2GetRegs();

    Coefficient coeff{};

    // Coefficient data formats:
    //
    // 1 word   15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
    // kx/ky   |TP|SN|Coeff. IP  | Coefficient fractional part |
    // Px      |TP|SN|Coefficient integer part            | FP |
    //
    // 2 words  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
    // kx/ky   |TP| Line color data    |SN|Coeff. integer part |Coefficient fractional part                    |
    // Px      |TP| Line color data    |SN|Coefficient integer part                    |Coeff. fractional part |
    //
    // TP=transparent bit   SN=coefficient sign bit   IP=coefficient integer part   FP=coefficient fractional part

    const uint32 baseAddress = params.coeffTableAddressOffset;
    const uint32 offset = coeffAddress >> 10u;

    if (params.coeffDataSize == 1) {
        // One-word coefficient data
        const uint32 address = (baseAddress + offset) * sizeof(uint16);
        const uint16 data = regs.vramControl.colorRAMCoeffTableEnable ? VDP2ReadRendererCRAM<uint16>(address | 0x800)
                                                                      : VDP2ReadRendererVRAM<uint16>(address);
        coeff.value = bit::extract_signed<0, 14>(data);
        coeff.lineColorData = 0;
        coeff.transparent = bit::test<15>(data);

        if (params.coeffDataMode == CoefficientDataMode::ViewpointX) {
            coeff.value <<= 14;
        } else {
            coeff.value <<= 6;
        }
    } else {
        // Two-word coefficient data
        const uint32 address = (baseAddress + offset) * sizeof(uint32);
        const uint32 data = regs.vramControl.colorRAMCoeffTableEnable ? VDP2ReadRendererCRAM<uint32>(address | 0x800)
                                                                      : VDP2ReadRendererVRAM<uint32>(address);
        coeff.value = bit::extract_signed<0, 23>(data);
        coeff.lineColorData = bit::extract<24, 30>(data);
        coeff.transparent = bit::test<31>(data);

        if (params.coeffDataMode == CoefficientDataMode::ViewpointX) {
            coeff.value <<= 8;
        }
    }

    return coeff;
}

// TODO: optimize - remove pageShiftH and pageShiftV params
template <bool rot, VDP::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode>
FORCE_INLINE VDP::Pixel VDP::VDP2FetchScrollBGPixel(const BGParams &bgParams, std::span<const uint32> pageBaseAddresses,
                                                    uint32 pageShiftH, uint32 pageShiftV, CoordU32 scrollCoord) {
    //      Map (NBGs)              Map (RBGs)
    // +---------+---------+   +----+----+----+----+
    // |         |         |   | A  | B  | C  | D  |
    // | Plane A | Plane B |   +----+----+----+----+
    // |         |         |   | E  | F  | G  | H  |
    // +---------+---------+   +----+----+----+----+
    // |         |         |   | I  | J  | K  | L  |
    // | Plane C | Plane D |   +----+----+----+----+
    // |         |         |   | M  | N  | O  | P  |
    // +---------+---------+   +----+----+----+----+
    //
    // Normal and rotation BGs are divided into planes in the exact configurations illustrated above.
    // The BG's Map Offset Register is combined with the BG plane's Map Register (MPxxN#) to produce a base address
    // for each plane:
    //   Address bits  Source
    //            8-6  Map Offset Register (MPOFN)
    //            5-0  Map Register (MPxxN#)
    //
    // These addresses are precomputed in pageBaseAddresses.
    //
    //       2x2 Plane               2x1 Plane          1x1 Plane
    //        PLSZ=3                  PLSZ=1             PLSZ=0
    // +---------+---------+   +---------+---------+   +---------+
    // |         |         |   |         |         |   |         |
    // | Page 1  | Page 2  |   | Page 1  | Page 2  |   | Page 1  |
    // |         |         |   |         |         |   |         |
    // +---------+---------+   +---------+---------+   +---------+
    // |         |         |
    // | Page 3  | Page 4  |
    // |         |         |
    // +---------+---------+
    //
    // Each plane is composed of 1x1, 2x1 or 2x2 pages, determined by Plane Size in the Plane Size Register (PLSZ).
    // Pages are stored sequentially in VRAM left to right, top to bottom, as shown.
    //
    // The size is stored as a bit shift in bgParams.pageShiftH and bgParams.pageShiftV.
    //
    //        64x64 Page                 32x32 Page
    // +----+----+..+----+----+   +----+----+..+----+----+
    // |CP 1|CP 2|  |CP63|CP64|   |CP 1|CP 2|  |CP31|CP32|
    // +----+----+..+----+----+   +----+----+..+----+----+
    // |  65|  66|  | 127| 128|   |  33|  34|  |  63|  64|
    // +----+----+..+----+----+   +----+----+..+----+----+
    // :    :    :  :    :    :   :    :    :  :    :    :
    // +----+----+..+----+----+   +----+----+..+----+----+
    // |3969|3970|  |4031|4032|   | 961| 962|  | 991| 992|
    // +----+----+..+----+----+   +----+----+..+----+----+
    // |4033|4034|  |4095|4096|   | 993| 994|  |1023|1024|
    // +----+----+..+----+----+   +----+----+..+----+----+
    //
    // Pages contain 32x32 or 64x64 character patterns, which are groups of 1x1 or 2x2 cells, determined by
    // Character Size in the Character Control Register (CHCTLA-B).
    //
    // Pages always contain a total of 64x64 cells - a grid of 64x64 1x1 character patterns or 32x32 2x2 character
    // patterns. Because of this, pages always have 512x512 dots.
    //
    // Character patterns in a page are stored sequentially in VRAM left to right, top to bottom, as shown above.
    //
    // fourCellChar specifies the size of the character patterns (1x1 when false, 2x2 when true) and, by extension,
    // the dimensions of the page (32x32 or 64x64 respectively).
    //
    // 2x2 Character Pattern     1x1 C.P.
    // +---------+---------+   +---------+
    // |         |         |   |         |
    // | Cell 1  | Cell 2  |   | Cell 1  |
    // |         |         |   |         |
    // +---------+---------+   +---------+
    // |         |         |
    // | Cell 3  | Cell 4  |
    // |         |         |
    // +---------+---------+
    //
    // Character patterns are groups of 1x1 or 2x2 cells, determined by Character Size in the Character Control
    // Register (CHCTLA-B).
    //
    // Cells are stored sequentially in VRAM left to right, top to bottom, as shown above.
    //
    // Character patterns contain a character number (15 bits), a palette number (7 bits, only used with 16 or 256
    // color palette modes), two special function bits (Special Priority and Special Color Calculation) and two flip
    // bits (horizontal and vertical).
    //
    // Character patterns can be one or two words long, as defined by Pattern Name Data Size in the Pattern Name
    // Control Register (PNCN0-3, PNCR). When using one word characters, some of the data comes from supplementary
    // registers.
    //
    // fourCellChar stores the character pattern size (1x1 when false, 2x2 when true).
    // twoWordChar determines if characters are one (false) or two (true) words long.
    // extChar determines the length of the character data field in one word characters -- when true, they're
    // extended by two bits, taking over the two flip bits.
    //
    //           Cell
    // +--+--+--+--+--+--+--+--+
    // | 1| 2| 3| 4| 5| 6| 7| 8|
    // +--+--+--+--+--+--+--+--+
    // | 9|10|11|12|13|14|15|16|
    // +--+--+--+--+--+--+--+--+
    // |17|18|19|20|21|22|23|24|
    // +--+--+--+--+--+--+--+--+
    // |25|26|27|28|29|30|31|32|
    // +--+--+--+--+--+--+--+--+
    // |33|34|35|36|37|38|39|40|
    // +--+--+--+--+--+--+--+--+
    // |41|42|43|44|45|46|47|48|
    // +--+--+--+--+--+--+--+--+
    // |49|50|51|52|53|54|55|56|
    // +--+--+--+--+--+--+--+--+
    // |57|58|59|60|61|62|63|64|
    // +--+--+--+--+--+--+--+--+
    //
    // Cells contain 8x8 dots (pixels) in one of the following color formats:
    //   - 16 color palette
    //   - 256 color palette
    //   - 1024 or 2048 color palette (depending on Color Mode)
    //   - 5:5:5 RGB (32768 colors)
    //   - 8:8:8 RGB (16777216 colors)
    //
    // colorFormat specifies one of the color formats above.
    // colorMode determines the palette color format in CRAM, one of:
    //   - 16-bit 5:5:5 RGB, 1024 words
    //   - 16-bit 5:5:5 RGB, 2048 words
    //   - 32-bit 8:8:8 RGB, 1024 longwords

    static constexpr std::size_t planeMSB = rot ? 11 : 10;
    static constexpr uint32 planeWidth = rot ? 4u : 2u;
    static constexpr uint32 planeMask = planeWidth - 1;

    static constexpr bool twoWordChar = charMode == CharacterMode::TwoWord;
    static constexpr bool extChar = charMode == CharacterMode::OneWordExtended;
    static constexpr uint32 fourCellCharValue = fourCellChar ? 1 : 0;

    auto [scrollX, scrollY] = scrollCoord;

    // Determine plane index from the scroll coordinates
    const uint32 planeX = (bit::extract<9, planeMSB>(scrollX) >> pageShiftH) & planeMask;
    const uint32 planeY = (bit::extract<9, planeMSB>(scrollY) >> pageShiftV) & planeMask;
    const uint32 plane = planeX + planeY * planeWidth;
    const uint32 pageBaseAddress = pageBaseAddresses[plane];
    const uint32 pageBank = (pageBaseAddress >> 17u) & 3u;

    // Determine page index from the scroll coordinates
    const uint32 pageX = bit::extract<9>(scrollX) & pageShiftH;
    const uint32 pageY = bit::extract<9>(scrollY) & pageShiftV;
    const uint32 page = pageX + pageY * 2u;
    const uint32 pageOffset = page << kPageSizes[fourCellChar][twoWordChar];

    // Determine character pattern from the scroll coordinates
    const uint32 charPatX = bit::extract<3, 8>(scrollX) >> fourCellCharValue;
    const uint32 charPatY = bit::extract<3, 8>(scrollY) >> fourCellCharValue;
    const uint32 charIndex = charPatX + charPatY * (64u >> fourCellCharValue);

    // Determine cell index from the scroll coordinates
    const uint32 cellX = bit::extract<3>(scrollX) & fourCellCharValue;
    const uint32 cellY = bit::extract<3>(scrollY) & fourCellCharValue;
    const uint32 cellIndex = cellX + cellY * 2u;

    // Determine dot coordinates
    const uint32 dotX = bit::extract<0, 2>(scrollX);
    const uint32 dotY = bit::extract<0, 2>(scrollY);
    const CoordU32 dotCoord{dotX, dotY};

    // Fetch character
    const uint32 pageAddress = pageBaseAddress + pageOffset;
    constexpr bool largePalette = colorFormat != ColorFormat::Palette16;
    const Character ch =
        twoWordChar ? VDP2FetchTwoWordCharacter(pageAddress, charIndex)
                    : VDP2FetchOneWordCharacter<fourCellChar, largePalette, extChar>(bgParams, pageAddress, charIndex);

    // Fetch pixel using character data
    return VDP2FetchCharacterPixel<colorFormat, colorMode>(bgParams, ch, dotCoord, cellIndex);
}

FORCE_INLINE VDP::Character VDP::VDP2FetchTwoWordCharacter(uint32 pageBaseAddress, uint32 charIndex) {
    const uint32 charAddress = pageBaseAddress + charIndex * sizeof(uint32);
    const uint32 charData = VDP2ReadRendererVRAM<uint32>(charAddress);

    Character ch{};
    ch.charNum = bit::extract<0, 14>(charData);
    ch.palNum = bit::extract<16, 22>(charData);
    ch.specColorCalc = bit::test<28>(charData);
    ch.specPriority = bit::test<29>(charData);
    ch.flipH = bit::test<30>(charData);
    ch.flipV = bit::test<31>(charData);
    return ch;
}

template <bool fourCellChar, bool largePalette, bool extChar>
FORCE_INLINE VDP::Character VDP::VDP2FetchOneWordCharacter(const BGParams &bgParams, uint32 pageBaseAddress,
                                                           uint32 charIndex) {
    // Contents of 1 word character patterns vary based on Character Size, Character Color Count and Auxiliary Mode:
    //     Character Size        = CHCTLA/CHCTLB.xxCHSZ  = !fourCellChar = !FCC
    //     Character Color Count = CHCTLA/CHCTLB.xxCHCNn = largePalette  = LP
    //     Auxiliary Mode        = PNCN0/PNCR.xxCNSM     = extChar      = EC
    //             ---------------- Character data ----------------    Supplement in Pattern Name Control Register
    // FCC LP  EC  |15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0|    | 9  8  7  6  5  4  3  2  1  0|
    //  F   F   F  |palnum 3-0 |VF|HF| character number 9-0       |    |PR|CC| PN 6-4 |charnum 14-10 |
    //  F   T   F  |--| PN 6-4 |VF|HF| character number 9-0       |    |PR|CC|--------|charnum 14-10 |
    //  T   F   F  |palnum 3-0 |VF|HF| character number 11-2      |    |PR|CC| PN 6-4 |CN 14-12|CN1-0|
    //  T   T   F  |--| PN 6-4 |VF|HF| character number 11-2      |    |PR|CC|--------|CN 14-12|CN1-0|
    //  F   F   T  |palnum 3-0 |       character number 11-0      |    |PR|CC| PN 6-4 |CN 14-12|-----|
    //  F   T   T  |--| PN 6-4 |       character number 11-0      |    |PR|CC|--------|CN 14-12|-----|
    //  T   F   T  |palnum 3-0 |       character number 13-2      |    |PR|CC| PN 6-4 |cn|-----|CN1-0|   cn=CN14
    //  T   T   T  |--| PN 6-4 |       character number 13-2      |    |PR|CC|--------|cn|-----|CN1-0|   cn=CN14

    const uint32 charAddress = pageBaseAddress + charIndex * sizeof(uint16);
    const uint16 charData = VDP2ReadRendererVRAM<uint16>(charAddress);

    // Character number bit range from the 1-word character pattern data (charData)
    static constexpr uint32 baseCharNumStart = 0;
    static constexpr uint32 baseCharNumEnd = 9 + 2 * extChar;
    static constexpr uint32 baseCharNumPos = 2 * fourCellChar;

    // Upper character number bit range from the supplementary character number (bgParams.supplCharNum)
    static constexpr uint32 supplCharNumStart = 2 * fourCellChar + 2 * extChar;
    static constexpr uint32 supplCharNumEnd = 4;
    static constexpr uint32 supplCharNumPos = 10 + supplCharNumStart;
    // The lower bits are always in range 0..1 and only used if fourCellChar == true

    const uint32 baseCharNum = bit::extract<baseCharNumStart, baseCharNumEnd>(charData);
    const uint32 supplCharNum = bit::extract<supplCharNumStart, supplCharNumEnd>(bgParams.supplScrollCharNum);

    Character ch{};
    ch.charNum = (baseCharNum << baseCharNumPos) | (supplCharNum << supplCharNumPos);
    if constexpr (fourCellChar) {
        ch.charNum |= bit::extract<0, 1>(bgParams.supplScrollCharNum);
    }
    if constexpr (largePalette) {
        ch.palNum = bit::extract<12, 14>(charData) << 4u;
    } else {
        ch.palNum = bit::extract<12, 15>(charData) | bgParams.supplScrollPalNum;
    }
    ch.specColorCalc = bgParams.supplScrollSpecialColorCalc;
    ch.specPriority = bgParams.supplScrollSpecialPriority;
    ch.flipH = !extChar && bit::test<10>(charData);
    ch.flipV = !extChar && bit::test<11>(charData);
    return ch;
}

template <ColorFormat colorFormat, uint32 colorMode>
FORCE_INLINE VDP::Pixel VDP::VDP2FetchCharacterPixel(const BGParams &bgParams, Character ch, CoordU32 dotCoord,
                                                     uint32 cellIndex) {
    static_assert(static_cast<uint32>(colorFormat) <= 4, "Invalid xxCHCN value");

    const VDP2Regs &regs = VDP2GetRegs();

    Pixel pixel{};

    auto [dotX, dotY] = dotCoord;

    assert(dotX < 8);
    assert(dotY < 8);

    // Flip dot coordinates if requested
    if (ch.flipH) {
        dotX ^= 7;
        if (bgParams.cellSizeShift > 0) {
            cellIndex ^= 1;
        }
    }
    if (ch.flipV) {
        dotY ^= 7;
        if (bgParams.cellSizeShift > 0) {
            cellIndex ^= 2;
        }
    }

    // Adjust cell index based on color format
    if constexpr (!IsPaletteColorFormat(colorFormat)) {
        cellIndex <<= 2;
    } else if constexpr (colorFormat != ColorFormat::Palette16) {
        cellIndex <<= 1;
    }

    // Cell addressing uses a fixed offset of 32 bytes
    const uint32 cellAddress = (ch.charNum + cellIndex) * 0x20;
    const uint32 dotOffset = dotX + dotY * 8;

    // Determine special color calculation flag
    const auto &specFuncCode = regs.specialFunctionCodes[bgParams.specialFunctionSelect];
    auto getSpecialColorCalcFlag = [&](uint8 colorData) {
        using enum SpecialColorCalcMode;
        switch (bgParams.specialColorCalcMode) {
        case PerScreen: return bgParams.colorCalcEnable;
        case PerCharacter: return bgParams.colorCalcEnable && ch.specColorCalc;
        case PerDot: return bgParams.colorCalcEnable && ch.specColorCalc && specFuncCode.colorMatches[colorData];
        case ColorDataMSB: return bgParams.colorCalcEnable && ch.specColorCalc && bit::test<2>(colorData);
        }
        util::unreachable();
    };

    // Fetch color and determine transparency.
    // Also determine special color calculation flag if using per-dot or color data MSB.
    uint8 colorData = 0;
    if constexpr (colorFormat == ColorFormat::Palette16) {
        const uint32 dotAddress = cellAddress + (dotOffset >> 1u);
        const uint8 dotData = (VDP2ReadRendererVRAM<uint8>(dotAddress) >> ((~dotX & 1) * 4)) & 0xF;
        const uint32 colorIndex = (ch.palNum << 4u) | dotData;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData);
    } else if constexpr (colorFormat == ColorFormat::Palette256) {
        const uint32 dotAddress = cellAddress + dotOffset;
        const uint8 dotData = VDP2ReadRendererVRAM<uint8>(dotAddress);
        const uint32 colorIndex = ((ch.palNum & 0x70) << 4u) | dotData;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData);
    } else if constexpr (colorFormat == ColorFormat::Palette2048) {
        const uint32 dotAddress = cellAddress + dotOffset * sizeof(uint16);
        const uint16 dotData = VDP2ReadRendererVRAM<uint16>(dotAddress);
        const uint32 colorIndex = dotData & 0x7FF;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && (dotData & 0x7FF) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData);
    } else if constexpr (colorFormat == ColorFormat::RGB555) {
        const uint32 dotAddress = cellAddress + dotOffset * sizeof(uint16);
        const uint16 dotData = VDP2ReadRendererVRAM<uint16>(dotAddress);
        pixel.color = ConvertRGB555to888(Color555{.u16 = dotData});
        pixel.transparent = bgParams.enableTransparency && bit::extract<15>(dotData) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(0b111);
    } else if constexpr (colorFormat == ColorFormat::RGB888) {
        const uint32 dotAddress = cellAddress + dotOffset * sizeof(uint32);
        const uint32 dotData = VDP2ReadRendererVRAM<uint32>(dotAddress);
        pixel.color.u32 = dotData;
        pixel.transparent = bgParams.enableTransparency && bit::extract<31>(dotData) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(0b111);
    }

    // Compute priority
    pixel.priority = bgParams.priorityNumber;
    if (bgParams.priorityMode == PriorityMode::PerCharacter) {
        pixel.priority &= ~1;
        pixel.priority |= (uint8)ch.specPriority;
    } else if (bgParams.priorityMode == PriorityMode::PerDot) {
        if constexpr (IsPaletteColorFormat(colorFormat)) {
            pixel.priority &= ~1;
            pixel.priority |= ch.specPriority && specFuncCode.colorMatches[colorData];
        }
    }

    return pixel;
}

template <ColorFormat colorFormat, uint32 colorMode>
FORCE_INLINE VDP::Pixel VDP::VDP2FetchBitmapPixel(const BGParams &bgParams, uint32 bitmapBaseAddress,
                                                  CoordU32 dotCoord) {
    static_assert(static_cast<uint32>(colorFormat) <= 4, "Invalid xxCHCN value");

    Pixel pixel{};

    auto [dotX, dotY] = dotCoord;

    // Bitmap data wraps around infinitely
    dotX &= bgParams.bitmapSizeH - 1;
    dotY &= bgParams.bitmapSizeV - 1;

    // Bitmap addressing uses a fixed offset of 0x20000 bytes which is precalculated when MPOFN/MPOFR is written to
    const uint32 dotOffset = dotX + dotY * bgParams.bitmapSizeH;
    const uint32 palNum = bgParams.supplBitmapPalNum;

    // Determine special color calculation flag
    auto getSpecialColorCalcFlag = [&](bool colorDataMSB) {
        using enum SpecialColorCalcMode;
        switch (bgParams.specialColorCalcMode) {
        case PerScreen: return bgParams.colorCalcEnable;
        case PerCharacter: return bgParams.colorCalcEnable && bgParams.supplBitmapSpecialColorCalc;
        case PerDot: return bgParams.colorCalcEnable && bgParams.supplBitmapSpecialColorCalc;
        case ColorDataMSB: return bgParams.colorCalcEnable && colorDataMSB;
        }
        util::unreachable();
    };

    if constexpr (colorFormat == ColorFormat::Palette16) {
        const uint32 dotAddress = bitmapBaseAddress + (dotOffset >> 1u);
        const uint8 dotData = (VDP2ReadRendererVRAM<uint8>(dotAddress) >> ((~dotX & 1) * 4)) & 0xF;
        const uint32 colorIndex = palNum | dotData;
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(bit::test<3>(dotData));
    } else if constexpr (colorFormat == ColorFormat::Palette256) {
        const uint32 dotAddress = bitmapBaseAddress + dotOffset;
        const uint8 dotData = VDP2ReadRendererVRAM<uint8>(dotAddress);
        const uint32 colorIndex = palNum | dotData;
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(bit::test<3>(dotData));
    } else if constexpr (colorFormat == ColorFormat::Palette2048) {
        const uint32 dotAddress = bitmapBaseAddress + dotOffset * sizeof(uint16);
        const uint16 dotData = VDP2ReadRendererVRAM<uint16>(dotAddress);
        const uint32 colorIndex = dotData & 0x7FF;
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && (dotData & 0x7FF) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(bit::test<3>(dotData));
    } else if constexpr (colorFormat == ColorFormat::RGB555) {
        const uint32 dotAddress = bitmapBaseAddress + dotOffset * sizeof(uint16);
        const uint16 dotData = VDP2ReadRendererVRAM<uint16>(dotAddress);
        pixel.color = ConvertRGB555to888(Color555{.u16 = dotData});
        pixel.transparent = bgParams.enableTransparency && bit::extract<15>(dotData) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(true);
    } else if constexpr (colorFormat == ColorFormat::RGB888) {
        const uint32 dotAddress = bitmapBaseAddress + dotOffset * sizeof(uint32);
        const uint32 dotData = VDP2ReadRendererVRAM<uint32>(dotAddress);
        pixel.color = Color888{.u32 = dotData};
        pixel.transparent = bgParams.enableTransparency && bit::extract<31>(dotData) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(true);
    }

    // Compute priority
    pixel.priority = bgParams.priorityNumber;
    if (bgParams.priorityMode == PriorityMode::PerCharacter || bgParams.priorityMode == PriorityMode::PerDot) {
        pixel.priority &= ~1;
        pixel.priority |= (uint8)bgParams.supplBitmapSpecialPriority;
    }

    return pixel;
}

template <uint32 colorMode>
FORCE_INLINE Color888 VDP::VDP2FetchCRAMColor(uint32 cramOffset, uint32 colorIndex) {
    static_assert(colorMode <= 2, "Invalid CRMD value");

    if constexpr (colorMode == 0) {
        // RGB 5:5:5, 1024 words
        const uint32 address = (cramOffset + colorIndex) * sizeof(uint16);
        return VDP2ReadRendererColor5to8(address & 0x7FE);
    } else if constexpr (colorMode == 1) {
        // RGB 5:5:5, 2048 words
        const uint32 address = (cramOffset + colorIndex) * sizeof(uint16);
        return VDP2ReadRendererColor5to8(address & 0xFFE);
    } else { // colorMode == 2
        // RGB 8:8:8, 1024 words
        const uint32 address = (cramOffset + colorIndex) * sizeof(uint32);
        const uint32 data = VDP2ReadRendererCRAM<uint32>(address & 0xFFC);
        return Color888{.u32 = data};
    }
}

FLATTEN FORCE_INLINE SpriteData VDP::VDP2FetchSpriteData(uint32 fbOffset) {
    const VDP2Regs &regs = VDP2GetRegs();

    const uint8 type = regs.spriteParams.type;
    if (type < 8) {
        return VDP2FetchWordSpriteData(fbOffset * sizeof(uint16), type);
    } else {
        return VDP2FetchByteSpriteData(fbOffset, type);
    }
}

FORCE_INLINE SpriteData VDP::VDP2FetchWordSpriteData(uint32 fbOffset, uint8 type) {
    assert(type < 8);

    const VDP2Regs &regs = VDP2GetRegs();

    const uint16 rawData = util::ReadBE<uint16>(&m_spriteFB[VDP1GetDisplayFBIndex()][fbOffset & 0x3FFFE]);

    SpriteData data{};
    switch (regs.spriteParams.type) {
    case 0x0:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 13>(rawData);
        data.priority = bit::extract<14, 15>(rawData);
        data.normalShadow = VDP2IsNormalShadow<10>(data.colorData);
        break;
    case 0x1:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 12>(rawData);
        data.priority = bit::extract<13, 15>(rawData);
        data.normalShadow = VDP2IsNormalShadow<10>(data.colorData);
        break;
    case 0x2:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 13>(rawData);
        data.priority = bit::extract<14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.normalShadow = VDP2IsNormalShadow<10>(data.colorData);
        break;
    case 0x3:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 12>(rawData);
        data.priority = bit::extract<13, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.normalShadow = VDP2IsNormalShadow<10>(data.colorData);
        break;
    case 0x4:
        data.colorData = bit::extract<0, 9>(rawData);
        data.colorCalcRatio = bit::extract<10, 12>(rawData);
        data.priority = bit::extract<13, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.normalShadow = VDP2IsNormalShadow<9>(data.colorData);
        break;
    case 0x5:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11>(rawData);
        data.priority = bit::extract<12, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.normalShadow = VDP2IsNormalShadow<10>(data.colorData);
        break;
    case 0x6:
        data.colorData = bit::extract<0, 9>(rawData);
        data.colorCalcRatio = bit::extract<10, 11>(rawData);
        data.priority = bit::extract<12, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.normalShadow = VDP2IsNormalShadow<9>(data.colorData);
        break;
    case 0x7:
        data.colorData = bit::extract<0, 8>(rawData);
        data.colorCalcRatio = bit::extract<9, 11>(rawData);
        data.priority = bit::extract<12, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.normalShadow = VDP2IsNormalShadow<8>(data.colorData);
        break;
    }
    return data;
}

FORCE_INLINE SpriteData VDP::VDP2FetchByteSpriteData(uint32 fbOffset, uint8 type) {
    assert(type >= 8);

    const VDP2Regs &regs = VDP2GetRegs();

    const uint8 rawData = m_spriteFB[VDP1GetDisplayFBIndex()][fbOffset & 0x3FFFF];

    SpriteData data{};
    switch (regs.spriteParams.type) {
    case 0x8:
        data.colorData = bit::extract<0, 6>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.normalShadow = VDP2IsNormalShadow<6>(data.colorData);
        break;
    case 0x9:
        data.colorData = bit::extract<0, 5>(rawData);
        data.colorCalcRatio = bit::extract<6>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.normalShadow = VDP2IsNormalShadow<5>(data.colorData);
        break;
    case 0xA:
        data.colorData = bit::extract<0, 5>(rawData);
        data.priority = bit::extract<6, 7>(rawData);
        data.normalShadow = VDP2IsNormalShadow<5>(data.colorData);
        break;
    case 0xB:
        data.colorData = bit::extract<0, 5>(rawData);
        data.colorCalcRatio = bit::extract<6, 7>(rawData);
        data.normalShadow = VDP2IsNormalShadow<5>(data.colorData);
        break;
    case 0xC:
        data.colorData = bit::extract<0, 7>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.normalShadow = VDP2IsNormalShadow<7>(data.colorData);
        break;
    case 0xD:
        data.colorData = bit::extract<0, 7>(rawData);
        data.colorCalcRatio = bit::extract<6>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.normalShadow = VDP2IsNormalShadow<7>(data.colorData);
        break;
    case 0xE:
        data.colorData = bit::extract<0, 7>(rawData);
        data.priority = bit::extract<6, 7>(rawData);
        data.normalShadow = VDP2IsNormalShadow<7>(data.colorData);
        break;
    case 0xF:
        data.colorData = bit::extract<0, 7>(rawData);
        data.colorCalcRatio = bit::extract<6, 7>(rawData);
        data.normalShadow = VDP2IsNormalShadow<7>(data.colorData);
        break;
    }
    return data;
}

template <uint32 colorDataBits>
FORCE_INLINE bool VDP::VDP2IsNormalShadow(uint16 colorData) {
    // Check against normal shadow pattern (LSB = 0, rest of the bits = 1)
    static constexpr uint16 kNormalShadowValue = ~(~0 << (colorDataBits + 1)) & ~1;
    return (colorData == kNormalShadowValue);
}

FORCE_INLINE uint32 VDP::VDP2GetY(uint32 y) const {
    const VDP2Regs &regs = VDP2GetRegs();

    if (regs.TVMD.LSMDn == InterlaceMode::DoubleDensity) {
        return (y << 1) | regs.TVSTAT.ODD;
    } else {
        return y;
    }
}

// -----------------------------------------------------------------------------
// Probe implementation

VDP::Probe::Probe(VDP &vdp)
    : m_vdp(vdp) {}

Dimensions VDP::Probe::GetResolution() const {
    return {m_vdp.m_HRes, m_vdp.m_VRes};
}

InterlaceMode VDP::Probe::GetInterlaceMode() const {
    return m_vdp.m_VDP2.TVMD.LSMDn;
}

} // namespace ymir::vdp
