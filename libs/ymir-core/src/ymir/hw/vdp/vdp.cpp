#include <ymir/hw/vdp/vdp.hpp>

#include <ymir/util/dev_log.hpp>

namespace ymir::vdp {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   vdp1
    //     vdp1_regs
    //   vdp2
    //     vdp2_regs

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

    struct vdp2 : public base {
        static constexpr std::string_view name = "VDP2";
    };

    struct vdp2_regs : public vdp2 {
        // static constexpr devlog::Level level = devlog::level::trace;
        static constexpr std::string_view name = "VDP2-Regs";
    };

} // namespace grp

// -----------------------------------------------------------------------------
// Debugger

template <bool debug>
FORCE_INLINE static void TraceBeginFrame(debug::IVDPTracer *tracer, const VDPState &state) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->BeginFrame(state);
        }
    }
}

// -----------------------------------------------------------------------------
// Implementation

VDP::VDP(core::Scheduler &scheduler, core::Configuration &config)
    : m_scheduler(scheduler) {

    config.system.videoStandard.Observe([&](VideoStandard videoStandard) { SetVideoStandard(videoStandard); });
    config.video.threadedVDP.Observe([&](bool value) { EnableThreadedVDP(value); });
    config.video.includeVDP1InRenderThread.Observe([&](bool value) { IncludeVDP1RenderInVDPThread(value); });

    m_phaseUpdateEvent = scheduler.RegisterEvent(core::events::VDPPhase, this, OnPhaseUpdateEvent);

    m_renderer.SetVDP1Callback({this, [](void *ctx) {
                                    auto &vdp = *static_cast<VDP *>(ctx);
                                    vdp.m_cbTriggerSpriteDrawEnd();
                                    vdp.m_cbVDP1FrameComplete();
                                }});

    Reset(true);
}

void VDP::Reset(bool hard) {
    m_state.Reset(hard);
    m_renderer.Reset(hard);

    BeginHPhaseActiveDisplay();
    BeginVPhaseActiveDisplay();

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
        [](uint32 address, void *ctx) -> uint8 {
            const uint16 value = cast(ctx).VDP1ReadReg<false>(address & ~1);
            return value >> ((~address & 1) * 8u);
        },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP1ReadReg<false>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP1ReadReg<false>(address + 0) << 16u;
            value |= cast(ctx).VDP1ReadReg<false>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) {
            uint16 currValue = cast(ctx).VDP1ReadReg<false>(address & ~1);
            const uint16 shift = (~address & 1) * 8u;
            const uint16 mask = ~(0xFF << shift);
            currValue = (currValue & mask) | (value << shift);
            cast(ctx).VDP1WriteReg<false>(address & ~1, currValue);
        },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteReg<false>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteReg<false>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteReg<false>(address + 2, value >> 0u);
        });

    bus.MapSideEffectFree(
        0x5D0'0000, 0x5D7'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 {
            const uint16 value = cast(ctx).VDP1ReadReg<true>(address & ~1);
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
            const uint16 value = cast(ctx).VDP2ReadReg(address & ~1);
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
    m_renderer.Advance<debug>(cycles);
}

template void VDP::Advance<false>(uint64 cycles);
template void VDP::Advance<true>(uint64 cycles);

void VDP::DumpVDP1VRAM(std::ostream &out) const {
    out.write((const char *)m_state.VRAM1.data(), m_state.VRAM1.size());
}

void VDP::DumpVDP2VRAM(std::ostream &out) const {
    out.write((const char *)m_state.VRAM2.data(), m_state.VRAM2.size());
}

void VDP::DumpVDP2CRAM(std::ostream &out) const {
    out.write((const char *)m_state.CRAM.data(), m_state.CRAM.size());
}

void VDP::DumpVDP1Framebuffers(std::ostream &out) const {
    const uint8 dispFB = m_state.displayFB;
    const uint8 drawFB = dispFB ^ 1;
    out.write((const char *)m_state.spriteFB[drawFB].data(), m_state.spriteFB[drawFB].size());
    out.write((const char *)m_state.spriteFB[dispFB].data(), m_state.spriteFB[dispFB].size());
    m_renderer.DumpVDP1AltFramebuffers(out);
}

void VDP::SaveState(state::VDPState &state) const {
    m_state.SaveState(state);
    m_renderer.SaveState(state);
}

bool VDP::ValidateState(const state::VDPState &state) const {
    if (!m_state.ValidateState(state)) {
        return false;
    }
    if (!m_renderer.ValidateState(state)) {
        return false;
    }
    return true;
}

void VDP::LoadState(const state::VDPState &state) {
    m_state.LoadState(state);
    m_renderer.LoadState(state);
}

void VDP::OnPhaseUpdateEvent(core::EventContext &eventContext, void *userContext) {
    auto &vdp = *static_cast<VDP *>(userContext);
    vdp.UpdatePhase();
    const uint64 cycles = vdp.GetPhaseCycles();
    eventContext.RescheduleFromPrevious(cycles);
}

void VDP::SetVideoStandard(VideoStandard videoStandard) {
    const bool pal = videoStandard == VideoStandard::PAL;
    if (m_state.regs2.TVSTAT.PAL != pal) {
        m_state.regs2.TVSTAT.PAL = pal;
        m_state.regs2.TVMDDirty = true;
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP1ReadVRAM(uint32 address) {
    address &= 0x7FFFF;
    return util::ReadBE<T>(&m_state.VRAM1[address]);
}

template <mem_primitive T>
FORCE_INLINE void VDP::VDP1WriteVRAM(uint32 address, T value) {
    address &= 0x7FFFF;
    util::WriteBE<T>(&m_state.VRAM1[address], value);
    m_renderer.VDP1WriteVRAM<T>(address, value);
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP1ReadFB(uint32 address) {
    address &= 0x3FFFF;
    return util::ReadBE<T>(&m_state.spriteFB[m_state.displayFB ^ 1][address]);
}

template <mem_primitive T>
FORCE_INLINE void VDP::VDP1WriteFB(uint32 address, T value) {
    address &= 0x3FFFF;
    util::WriteBE<T>(&m_state.spriteFB[m_state.displayFB ^ 1][address], value);
    m_renderer.VDP1WriteFB<T>(address, value);
}

template <bool peek>
FORCE_INLINE uint16 VDP::VDP1ReadReg(uint32 address) {
    address &= 0x7FFFF;
    return m_state.regs1.Read<peek>(address);
}

template <bool poke>
FORCE_INLINE void VDP::VDP1WriteReg(uint32 address, uint16 value) {
    address &= 0x7FFFF;
    m_state.regs1.Write<poke>(address, value);
    m_renderer.VDP1WriteReg<poke>(address, value);

    switch (address) {
    case 0x00:
        if constexpr (!poke) {
            devlog::trace<grp::vdp1_regs>("Write to TVM={:d}{:d}{:d}", m_state.regs1.hdtvEnable,
                                          m_state.regs1.fbRotEnable, m_state.regs1.pixel8Bits);
            devlog::trace<grp::vdp1_regs>("Write to VBE={:d}", m_state.regs1.vblankErase);
        }
        break;
    case 0x02:
        if constexpr (!poke) {
            devlog::trace<grp::vdp1_regs>("Write to DIE={:d} DIL={:d}", m_state.regs1.dblInterlaceEnable,
                                          m_state.regs1.dblInterlaceDrawLine);
            devlog::trace<grp::vdp1_regs>("Write to FCM={:d} FCT={:d} manualswap={:d} manualerase={:d}",
                                          m_state.regs1.fbSwapMode, m_state.regs1.fbSwapTrigger,
                                          m_state.regs1.fbManualSwap, m_state.regs1.fbManualErase);
        }
        break;
    case 0x04:
        if constexpr (!poke) {
            devlog::trace<grp::vdp1_regs>("Write to PTM={:d}", m_state.regs1.plotTrigger);
            if (m_state.regs1.plotTrigger == 0b01) {
                m_renderer.BeginVDP1();
            }
        }
        break;
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP2ReadVRAM(uint32 address) {
    // TODO: handle VRSIZE.VRAMSZ
    address &= 0x7FFFF;
    return util::ReadBE<T>(&m_state.VRAM2[address]);
}

template <mem_primitive T>
FORCE_INLINE void VDP::VDP2WriteVRAM(uint32 address, T value) {
    // TODO: handle VRSIZE.VRAMSZ
    address &= 0x7FFFF;
    util::WriteBE<T>(&m_state.VRAM2[address], value);
    m_renderer.VDP2WriteVRAM<T>(address, value);
}

template <mem_primitive T, bool peek>
FORCE_INLINE T VDP::VDP2ReadCRAM(uint32 address) {
    if constexpr (std::is_same_v<T, uint32>) {
        uint32 value = VDP2ReadCRAM<uint16, peek>(address + 0) << 16u;
        value |= VDP2ReadCRAM<uint16, peek>(address + 2) << 0u;
        return value;
    }

    address = MapCRAMAddress(address, m_state.regs2.vramControl.colorRAMMode);
    T value = util::ReadBE<T>(&m_state.CRAM[address]);
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

    address = MapCRAMAddress(address, m_state.regs2.vramControl.colorRAMMode);
    if constexpr (!poke) {
        devlog::trace<grp::vdp2_regs>("{}-bit VDP2 CRAM write to {:05X} = {:X}", sizeof(T) * 8, address, value);
    }
    util::WriteBE<T>(&m_state.CRAM[address], value);
    m_renderer.VDP2WriteCRAM<T>(address, value);

    if (m_state.regs2.vramControl.colorRAMMode == 0) {
        if constexpr (!poke) {
            devlog::trace<grp::vdp2_regs>("   replicated to {:05X}", address ^ 0x800);
        }
        util::WriteBE<T>(&m_state.CRAM[address ^ 0x800], value);
        m_renderer.VDP2WriteCRAM<T>(address ^ 0x800, value);
    }
}

FORCE_INLINE uint16 VDP::VDP2ReadReg(uint32 address) {
    address &= 0x1FF;
    return m_state.regs2.Read(address);
}

FORCE_INLINE void VDP::VDP2WriteReg(uint32 address, uint16 value) {
    address &= 0x1FF;
    devlog::trace<grp::vdp2_regs>("VDP2 register write to {:03X} = {:04X}", address, value);

    m_state.regs2.Write(address, value);
    m_renderer.VDP2WriteReg(address, value);

    switch (address) {
    case 0x000:
        devlog::trace<grp::vdp2_regs>("TVMD write: {:04X} - HRESO={:d} VRESO={:d} LSMD={:d} BDCLMD={:d} DISP={:d}{}",
                                      m_state.regs2.TVMD.u16, (uint16)m_state.regs2.TVMD.HRESOn,
                                      (uint16)m_state.regs2.TVMD.VRESOn, (uint16)m_state.regs2.TVMD.LSMDn,
                                      (uint16)m_state.regs2.TVMD.BDCLMD, (uint16)m_state.regs2.TVMD.DISP,
                                      (m_state.regs2.TVMDDirty ? " (dirty)" : ""));
        break;
    }
}

FORCE_INLINE void VDP::UpdatePhase() {
    auto nextPhase = static_cast<uint32>(m_state.HPhase) + 1;
    if (nextPhase == m_state.HTimings.size()) {
        nextPhase = 0;
    }

    m_state.HPhase = static_cast<HorizontalPhase>(nextPhase);
    switch (m_state.HPhase) {
    case HorizontalPhase::Active: BeginHPhaseActiveDisplay(); break;
    case HorizontalPhase::RightBorder: BeginHPhaseRightBorder(); break;
    case HorizontalPhase::Sync: BeginHPhaseSync(); break;
    case HorizontalPhase::VBlankOut: BeginHPhaseVBlankOut(); break;
    case HorizontalPhase::LeftBorder: BeginHPhaseLeftBorder(); break;
    case HorizontalPhase::LastDot: BeginHPhaseLastDot(); break;
    }
}

FORCE_INLINE uint64 VDP::GetPhaseCycles() const {
    return m_state.HTimings[static_cast<uint32>(m_state.HPhase)];
}

FORCE_INLINE void VDP::IncrementVCounter() {
    ++m_state.VCounter;
    while (m_state.VCounter >= m_state.VTimings[static_cast<uint32>(m_state.VPhase)]) {
        auto nextPhase = static_cast<uint32>(m_state.VPhase) + 1;
        if (nextPhase == m_state.VTimings.size()) {
            m_state.VCounter = 0;
            nextPhase = 0;
        }

        m_state.VPhase = static_cast<VerticalPhase>(nextPhase);
        switch (m_state.VPhase) {
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
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering horizontal active display phase", m_state.VCounter);
    if (m_state.VPhase == VerticalPhase::Active) {
        if (m_state.VCounter == 0) {
            devlog::trace<grp::base>("Begin VDP2 frame, VDP1 framebuffer {}", m_state.displayFB);
            m_renderer.BeginFrame();
            // TODO: debug flag
            TraceBeginFrame<true>(m_tracer, m_state);
        } else if (m_state.VCounter == 210) { // ~1ms before VBlank IN
            m_cbTriggerOptimizedINTBACKRead();
        }

        m_renderer.ProcessLine(m_state.VCounter);
    }
}

void VDP::BeginHPhaseRightBorder() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering right border phase", m_state.VCounter);

    devlog::trace<grp::base>("## HBlank IN {:3d}", m_state.VCounter);

    m_state.regs2.TVSTAT.HBLANK = 1;
    m_cbHBlank();

    // Start erasing if we just entered VBlank IN
    if (m_state.VCounter == m_state.VTimings[static_cast<uint32>(VerticalPhase::Active)]) {
        devlog::trace<grp::base>("## HBlank IN + VBlank IN  VBE={:d} manualerase={:d}", m_state.regs1.vblankErase,
                                 m_state.regs1.fbManualErase);

        m_renderer.ProcessVBlankHBlank();
    }

    // TODO: draw border
}

void VDP::BeginHPhaseSync() {
    IncrementVCounter();
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering horizontal sync phase", m_state.VCounter);
}

void VDP::BeginHPhaseVBlankOut() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering VBlank OUT horizontal phase", m_state.VCounter);

    if (m_state.VPhase == VerticalPhase::LastLine) {
        devlog::trace<grp::base>("## HBlank half + VBlank OUT  FCM={:d} FCT={:d} manualswap={:d} PTM={:d}",
                                 m_state.regs1.fbSwapMode, m_state.regs1.fbSwapTrigger, m_state.regs1.fbManualSwap,
                                 m_state.regs1.plotTrigger);

        m_renderer.ProcessVBlankOUT();
    }
}

void VDP::BeginHPhaseLeftBorder() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering left border phase", m_state.VCounter);

    m_state.regs2.TVSTAT.HBLANK = 0;

    // TODO: draw border
}

void VDP::BeginHPhaseLastDot() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering last dot phase", m_state.VCounter);

    // If we just entered the bottom blanking vertical phase, switch fields
    if (m_state.VCounter == m_state.VTimings[static_cast<uint32>(VerticalPhase::Active)]) {
        if (m_state.regs2.TVMD.LSMDn != InterlaceMode::None) {
            m_state.regs2.TVSTAT.ODD ^= 1;
            devlog::trace<grp::base>("Switched to {} field", (m_state.regs2.TVSTAT.ODD ? "odd" : "even"));
            m_renderer.ProcessEvenOddFieldSwitch();
        } else if (m_state.regs2.TVSTAT.ODD != 1) {
            m_state.regs2.TVSTAT.ODD = 1;
            m_renderer.ProcessEvenOddFieldSwitch();
        }
    }
}

// ----

void VDP::BeginVPhaseActiveDisplay() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering vertical active display phase", m_state.VCounter);
}

void VDP::BeginVPhaseBottomBorder() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering bottom border phase", m_state.VCounter);

    devlog::trace<grp::base>("## VBlank IN");

    m_state.regs2.TVSTAT.VBLANK = 1;
    m_cbVBlankStateChange(true);

    // TODO: draw border
}

void VDP::BeginVPhaseBlankingAndSync() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering blanking/vertical sync phase", m_state.VCounter);

    // End frame
    devlog::trace<grp::base>("End VDP2 frame");
    m_renderer.EndFrame();
}

void VDP::BeginVPhaseTopBorder() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering top border phase", m_state.VCounter);

    m_state.UpdateResolution<true>();

    // TODO: draw border
}

void VDP::BeginVPhaseLastLine() {
    devlog::trace<grp::base>("(VCNT = {:3d})  Entering last line phase", m_state.VCounter);

    devlog::trace<grp::base>("## VBlank OUT");

    m_state.regs2.TVSTAT.VBLANK = 0;
    m_cbVBlankStateChange(false);
}

// -----------------------------------------------------------------------------
// Probe implementation

VDP::Probe::Probe(VDP &vdp)
    : m_vdp(vdp) {}

Dimensions VDP::Probe::GetResolution() const {
    return {m_vdp.m_state.HRes, m_vdp.m_state.VRes};
}

InterlaceMode VDP::Probe::GetInterlaceMode() const {
    return m_vdp.m_state.regs2.TVMD.LSMDn;
}

} // namespace ymir::vdp
