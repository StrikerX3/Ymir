#include <ymir/hw/vdp/vdp.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/constexpr_for.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/scope_guard.hpp>
#include <ymir/util/thread_name.hpp>
#include <ymir/util/unreachable.hpp>

#include <cassert>
#include <limits>

#if defined(_M_X64) || defined(__x86_64__)
    #include <immintrin.h>
#elif defined(_M_ARM64) || defined(__aarch64__)
    #include <arm_neon.h>
#endif

namespace ymir::vdp {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   phase
    //   intr
    //     intr_hb
    //   vdp1
    //     vdp1_regs
    //     vdp1_cmd
    //     vdp1_render
    //   vdp2
    //     vdp2_regs
    //     vdp2_render
    //       vdp2_render_verbose

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "VDP";
    };

    struct phase : public base {
        static constexpr std::string_view name = "VDP-Phase";
    };

    struct intr : public base {
        static constexpr std::string_view name = "VDP-Interrupt";
    };

    struct intr_hb : public intr {
        static constexpr devlog::Level level = devlog::level::debug;
    };

    struct vdp1 : public base {
        static constexpr std::string_view name = "VDP1";
    };

    struct vdp1_regs : public vdp1 {
        static constexpr std::string_view name = "VDP1-Regs";
    };

    struct vdp1_cmd : public vdp1 {
        static constexpr std::string_view name = "VDP1-Command";
    };

    struct vdp1_render : public vdp1 {
        static constexpr std::string_view name = "VDP1-Render";
    };

    struct vdp2 : public base {
        static constexpr std::string_view name = "VDP2";
    };

    struct vdp2_regs : public vdp2 {
        static constexpr std::string_view name = "VDP2-Regs";
    };

    struct vdp2_render : public vdp2 {
        static constexpr std::string_view name = "VDP2-Render";
    };

    struct vdp2_render_verbose : public vdp2_render {
        static constexpr devlog::Level level = devlog::level::debug;
    };

} // namespace grp

VDP::VDP(core::Scheduler &scheduler, core::Configuration &config)
    : m_scheduler(scheduler) {

    config.system.videoStandard.Observe([&](VideoStandard videoStandard) { SetVideoStandard(videoStandard); });
    config.video.threadedVDP1.Observe([&](bool value) { EnableThreadedVDP1(value); });
    config.video.threadedVDP2.Observe([&](bool value) { EnableThreadedVDP2(value); });
    config.video.threadedDeinterlacer.Observe([&](bool value) { m_threadedDeinterlacer = value; });

    m_phaseUpdateEvent = scheduler.RegisterEvent(core::events::VDPPhase, this, OnPhaseUpdateEvent);

    UpdateFunctionPointers();

    m_layerRendered.fill(true);

    Reset(true);
}

VDP::~VDP() {
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::Shutdown());
        if (m_VDP1RenderThread.joinable()) {
            m_VDP1RenderThread.join();
        }
    }
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::Shutdown());
        if (m_VDP2RenderThread.joinable()) {
            m_VDP2RenderThread.join();
        }
        if (m_VDP2DeinterlaceRenderThread.joinable()) {
            m_VDP2DeinterlaceRenderThread.join();
        }
    }
}

void VDP::Reset(bool hard) {
    m_HRes = 320;
    m_VRes = 224;
    m_exclusiveMonitor = false;
    m_displayEnabled = false;
    m_borderColorMode = false;

    m_state.Reset(hard);
    if (hard) {
        m_CRAMCache.fill({});
    }

    m_VDP1TimingPenaltyCycles = 0;

    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::Reset());
    } else {
        m_framebuffer.fill(0xFF000000);
    }

    m_VDP1RenderState.Reset();

    m_layerEnabled.fill(false);
    for (auto &state : m_layerStates) {
        state[0].Reset();
        state[1].Reset();
    }
    m_spriteLayerAttrs[0].Reset();
    m_spriteLayerAttrs[1].Reset();
    for (auto &state : m_normBGLayerStates) {
        state.Reset();
    }
    for (auto &state : m_vramFetchers) {
        state[0].Reset();
        state[1].Reset();
    }
    for (auto &state : m_rotParamStates) {
        state.Reset();
    }
    m_lineBackLayerState.Reset();

    UpdateResolution<false>();

    BeginHPhaseActiveDisplay();
    BeginVPhaseActiveDisplay();

    VDP2UpdateEnabledBGs();

    m_scheduler.ScheduleFromNow(m_phaseUpdateEvent, GetPhaseCycles());
}

void VDP::MapMemory(sys::SH2Bus &bus) {
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
        });
    bus.MapNormal(
        0x5C0'0000, 0x5C7'FFFF, this,
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP1WriteVRAM<uint8, false>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteVRAM<uint16, false>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteVRAM<uint16, false>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteVRAM<uint16, false>(address + 2, value >> 0u);
        });
    bus.MapSideEffectFree(
        0x5C0'0000, 0x5C7'FFFF, this,
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).VDP1WriteVRAM<uint8, true>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).VDP1WriteVRAM<uint16, true>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).VDP1WriteVRAM<uint16, true>(address + 0, value >> 16u);
            cast(ctx).VDP1WriteVRAM<uint16, true>(address + 2, value >> 0u);
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
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadReg<false>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadReg<false>(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadReg<false>(address + 2) << 0u;
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
            const uint16 value = cast(ctx).VDP2ReadReg<true>(address & ~1);
            return value >> ((~address & 1) * 8u);
        },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).VDP2ReadReg<true>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).VDP2ReadReg<true>(address + 0) << 16u;
            value |= cast(ctx).VDP2ReadReg<true>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) {
            uint16 currValue = cast(ctx).VDP2ReadReg<true>(address & ~1);
            const uint16 shift = (~address & 1) * 8u;
            const uint16 mask = ~(0xFF << shift);
            currValue = (currValue & mask) | (value << shift);
            cast(ctx).VDP2WriteReg(address & ~1, currValue);
        });
}

void VDP::Advance(uint64 cycles) {
    if (m_VDP1RenderState.rendering) {
        if (cycles <= m_VDP1TimingPenaltyCycles) {
            m_VDP1TimingPenaltyCycles -= cycles;
            return;
        }

        // HACK: slow down VDP1 commands to avoid freezes on Virtua Racing and Dragon Ball Z
        // TODO: proper cycle counting
        static constexpr uint64 kCyclesPerCommand = 500; // FIXME: pulled out of thin air

        m_VDP1RenderState.cycleCount += cycles - m_VDP1TimingPenaltyCycles;
        const uint64 steps = m_VDP1RenderState.cycleCount / kCyclesPerCommand;
        m_VDP1RenderState.cycleCount %= kCyclesPerCommand;
        m_VDP1TimingPenaltyCycles = 0;

        for (uint64 i = 0; i < steps; i++) {
            (this->*m_fnVDP1ProcessCommand)();
        }
    }
}

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
    if (m_deinterlaceRender) {
        out.write((const char *)m_altSpriteFB[drawFB].data(), m_altSpriteFB[drawFB].size());
        out.write((const char *)m_altSpriteFB[dispFB].data(), m_altSpriteFB[dispFB].size());
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP1ReadVRAM(uint32 address) const {
    address &= 0x7FFFF;
    return util::ReadBE<T>(&m_state.VRAM1[address]);
}

template <mem_primitive T, bool poke>
FORCE_INLINE void VDP::VDP1WriteVRAM(uint32 address, T value) {
    address &= 0x7FFFF;
    util::WriteBE<T>(&m_state.VRAM1[address], value);
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::VRAMWrite<T>(address, value));
    }
    if (m_stallVDP1OnVRAMWrites && m_VDP1RenderState.rendering) {
        m_VDP1TimingPenaltyCycles += kVDP1TimingPenaltyPerWrite;
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP1ReadFB(uint32 address) const {
    address &= 0x3FFFF;
    return util::ReadBE<T>(&m_state.spriteFB[m_state.displayFB ^ 1][address]);
}

template <mem_primitive T>
FORCE_INLINE void VDP::VDP1WriteFB(uint32 address, T value) {
    address &= 0x3FFFF;
    util::WriteBE<T>(&m_state.spriteFB[m_state.displayFB ^ 1][address], value);
    if (m_deinterlaceRender) {
        util::WriteBE<T>(&m_altSpriteFB[m_state.displayFB ^ 1][address & 0x3FFFF], value);
    }
}

template <bool peek>
FORCE_INLINE uint16 VDP::VDP1ReadReg(uint32 address) const {
    address &= 0x7FFFF;
    return m_state.regs1.Read<peek>(address);
}

template <bool poke>
FORCE_INLINE void VDP::VDP1WriteReg(uint32 address, uint16 value) {
    address &= 0x7FFFF;
    m_state.regs1.Write<poke>(address, value);
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::RegWrite(address, value));
    }

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
            devlog::trace<grp::vdp1_regs>("Write to FCM={:d} FCT={:d}", m_state.regs1.fbSwapMode,
                                          m_state.regs1.fbSwapTrigger);
        }
        break;
    case 0x04:
        if constexpr (!poke) {
            devlog::trace<grp::vdp1_regs>("Write to PTM={:d}", m_state.regs1.plotTrigger);
            if (m_state.regs1.plotTrigger == 0b01) {
                VDP1BeginFrame();
            }
        }
        break;
    case 0x0C: // ENDR
        // TODO: schedule drawing termination after 30 cycles
        m_VDP1RenderState.rendering = false;
        m_VDP1TimingPenaltyCycles = 0;
        break;
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP2ReadVRAM(uint32 address) const {
    // TODO: handle VRSIZE.VRAMSZ
    address &= 0x7FFFF;
    return util::ReadBE<T>(&m_state.VRAM2[address]);
}

template <mem_primitive T>
FORCE_INLINE void VDP::VDP2WriteVRAM(uint32 address, T value) {
    // TODO: handle VRSIZE.VRAMSZ
    address &= 0x7FFFF;
    util::WriteBE<T>(&m_state.VRAM2[address], value);
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2VRAMWrite<T>(address, value));
    }
}

template <mem_primitive T, bool peek>
FORCE_INLINE T VDP::VDP2ReadCRAM(uint32 address) const {
    if constexpr (std::is_same_v<T, uint32>) {
        uint32 value = VDP2ReadCRAM<uint16, peek>(address + 0) << 16u;
        value |= VDP2ReadCRAM<uint16, peek>(address + 2) << 0u;
        return value;
    }

    address = MapCRAMAddress(address);
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
    } else {
        address = MapCRAMAddress(address);
        if constexpr (!poke) {
            devlog::trace<grp::vdp2_regs>("{}-bit VDP2 CRAM write to {:05X} = {:X}", sizeof(T) * 8, address, value);
        }
        util::WriteBE<T>(&m_state.CRAM[address], value);
        VDP2UpdateCRAMCache<T>(address);
        if (m_threadedVDP2Rendering) {
            m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2CRAMWrite<T>(address, value));
        }
        if (m_state.regs2.vramControl.colorRAMMode == 0) {
            if constexpr (!poke) {
                devlog::trace<grp::vdp2_regs>("   replicated to {:05X}", address ^ 0x800);
            }
            util::WriteBE<T>(&m_state.CRAM[address ^ 0x800], value);
            VDP2UpdateCRAMCache<T>(address);
            if (m_threadedVDP2Rendering) {
                m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2CRAMWrite<T>(address ^ 0x800, value));
            }
        }
    }
}

template <bool peek>
FORCE_INLINE uint16 VDP::VDP2ReadReg(uint32 address) const {
    address &= 0x1FF;
    return m_state.regs2.Read<peek>(address);
}

FORCE_INLINE void VDP::VDP2WriteReg(uint32 address, uint16 value) {
    address &= 0x1FF;
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2RegWrite(address, value));
    }
    m_state.regs2.Write(address, value);
    devlog::trace<grp::vdp2_regs>("VDP2 register write to {:03X} = {:04X}", address, value);

    switch (address) {
    case 0x000:
        devlog::trace<grp::vdp2_regs>("TVMD write: {:04X} - HRESO={:d} VRESO={:d} LSMD={:d} BDCLMD={:d} DISP={:d}{}",
                                      m_state.regs2.TVMD.u16, (uint16)m_state.regs2.TVMD.HRESOn,
                                      (uint16)m_state.regs2.TVMD.VRESOn, (uint16)m_state.regs2.TVMD.LSMDn,
                                      (uint16)m_state.regs2.TVMD.BDCLMD, (uint16)m_state.regs2.TVMD.DISP,
                                      (m_state.regs2.TVMDDirty ? " (dirty)" : ""));
        break;
    case 0x020: [[fallthrough]]; // BGON
    case 0x028: [[fallthrough]]; // CHCTLA
    case 0x02A:                  // CHCTLB
        if (m_threadedVDP2Rendering) {
            m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2UpdateEnabledBGs());
        } else {
            VDP2UpdateEnabledBGs();
        }
        break;

    case 0x074: [[fallthrough]]; // SCYIN0
    case 0x076:                  // SCYDN0
        if (!m_threadedVDP2Rendering) {
            m_normBGLayerStates[0].scrollAmountV = m_state.regs2.bgParams[1].scrollAmountV;
        }
        break;
    case 0x084: [[fallthrough]]; // SCYIN1
    case 0x086:                  // SCYDN1
        if (!m_threadedVDP2Rendering) {
            m_normBGLayerStates[1].scrollAmountV = m_state.regs2.bgParams[2].scrollAmountV;
        }
        break;
    case 0x092: // SCYN2
        if (!m_threadedVDP2Rendering) {
            m_normBGLayerStates[2].scrollAmountV = m_state.regs2.bgParams[3].scrollAmountV;
            m_normBGLayerStates[2].fracScrollY = 0;
        }
        break;
    case 0x096: // SCYN3
        if (!m_threadedVDP2Rendering) {
            m_normBGLayerStates[3].scrollAmountV = m_state.regs2.bgParams[4].scrollAmountV;
            m_normBGLayerStates[3].fracScrollY = 0;
        }
        break;
    }
}

void VDP::SaveState(state::VDPState &state) const {
    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::PreSaveStateSync());
        m_vdp1RenderingContext.preSaveSyncSignal.Wait();
        m_vdp1RenderingContext.preSaveSyncSignal.Reset();
    }

    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::PreSaveStateSync());
        m_vdp2RenderingContext.preSaveSyncSignal.Wait();
        m_vdp2RenderingContext.preSaveSyncSignal.Reset();
    }

    m_state.SaveState(state);

    state.VDP1TimingPenalty = m_VDP1TimingPenaltyCycles;

    state.renderer.vdp1State.sysClipH = m_VDP1RenderState.sysClipH;
    state.renderer.vdp1State.sysClipV = m_VDP1RenderState.sysClipV;
    state.renderer.vdp1State.doubleV = m_VDP1RenderState.doubleV;
    state.renderer.vdp1State.userClipX0 = m_VDP1RenderState.userClipX0;
    state.renderer.vdp1State.userClipY0 = m_VDP1RenderState.userClipY0;
    state.renderer.vdp1State.userClipX1 = m_VDP1RenderState.userClipX1;
    state.renderer.vdp1State.userClipY1 = m_VDP1RenderState.userClipY1;
    state.renderer.vdp1State.localCoordX = m_VDP1RenderState.localCoordX;
    state.renderer.vdp1State.localCoordY = m_VDP1RenderState.localCoordY;
    state.renderer.vdp1State.rendering = m_VDP1RenderState.rendering;
    state.renderer.vdp1State.doDisplayErase = m_VDP1RenderState.doDisplayErase;
    state.renderer.vdp1State.doVBlankErase = m_VDP1RenderState.doVBlankErase;
    state.renderer.vdp1State.eraseWriteValue = m_VDP1RenderState.eraseWriteValue;
    state.renderer.vdp1State.eraseX1 = m_VDP1RenderState.eraseX1;
    state.renderer.vdp1State.eraseY1 = m_VDP1RenderState.eraseY1;
    state.renderer.vdp1State.eraseX3 = m_VDP1RenderState.eraseX3;
    state.renderer.vdp1State.eraseY3 = m_VDP1RenderState.eraseY3;
    state.renderer.vdp1State.cycleCount = m_VDP1RenderState.cycleCount;
    state.renderer.vdp1State.cyclesSpent = m_VDP1RenderState.cyclesSpent;
    state.renderer.vdp1State.meshFB = m_VDP1RenderState.meshFB;

    for (size_t i = 0; i < 4; i++) {
        state.renderer.normBGLayerStates[i].fracScrollX = m_normBGLayerStates[i].fracScrollX;
        state.renderer.normBGLayerStates[i].fracScrollY = m_normBGLayerStates[i].fracScrollY;
        state.renderer.normBGLayerStates[i].scrollAmountV = m_normBGLayerStates[i].scrollAmountV;
        state.renderer.normBGLayerStates[i].scrollIncH = m_normBGLayerStates[i].scrollIncH;
        state.renderer.normBGLayerStates[i].lineScrollTableAddress = m_normBGLayerStates[i].lineScrollTableAddress;
        state.renderer.normBGLayerStates[i].vertCellScrollOffset = m_normBGLayerStates[i].vertCellScrollOffset;
        state.renderer.normBGLayerStates[i].vertCellScrollDelay = m_normBGLayerStates[i].vertCellScrollDelay;
        state.renderer.normBGLayerStates[i].mosaicCounterY = m_normBGLayerStates[i].mosaicCounterY;
    }

    for (size_t i = 0; i < 2; i++) {
        state.renderer.rotParamStates[i].pageBaseAddresses = m_rotParamStates[i].pageBaseAddresses;
        state.renderer.rotParamStates[i].Xst = m_rotParamStates[i].Xst;
        state.renderer.rotParamStates[i].Yst = m_rotParamStates[i].Yst;
        state.renderer.rotParamStates[i].KA = m_rotParamStates[i].KA;
    }

    state.renderer.lineBackLayerState.lineColor = m_lineBackLayerState.lineColor.u32;
    state.renderer.lineBackLayerState.backColor = m_lineBackLayerState.backColor.u32;

    auto copyChar = [&](state::VDPState::VDPRendererState::Character &dst, const Character &src) {
        dst.charNum = src.charNum;
        dst.palNum = src.palNum;
        dst.specColorCalc = src.specColorCalc;
        dst.specPriority = src.specPriority;
        dst.flipH = src.flipH;
        dst.flipV = src.flipV;
    };

    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < 6; j++) {
            copyChar(state.renderer.vramFetchers[i][j].currChar, m_vramFetchers[i][j].currChar);
            copyChar(state.renderer.vramFetchers[i][j].nextChar, m_vramFetchers[i][j].nextChar);
            state.renderer.vramFetchers[i][j].lastCharIndex = m_vramFetchers[i][j].lastCharIndex;
            state.renderer.vramFetchers[i][j].lastCellX = m_vramFetchers[i][j].lastCellX;
            state.renderer.vramFetchers[i][j].bitmapData = m_vramFetchers[i][j].bitmapData;
            state.renderer.vramFetchers[i][j].bitmapDataAddress = m_vramFetchers[i][j].bitmapDataAddress;
            state.renderer.vramFetchers[i][j].lastVCellScroll = m_vramFetchers[i][j].lastVCellScroll;
        }
    }

    state.renderer.vertCellScrollInc = m_vertCellScrollInc;

    state.renderer.displayFB = m_state.displayFB;

    state.displayEnabled = m_displayEnabled;
    state.borderColorMode = m_borderColorMode;
}

bool VDP::ValidateState(const state::VDPState &state) const {
    if (!m_state.ValidateState(state)) {
        return false;
    }
    return true;
}

void VDP::LoadState(const state::VDPState &state) {
    m_state.LoadState(state);

    for (uint32 address = 0; address < kVDP2CRAMSize; address += 2) {
        VDP2UpdateCRAMCache<uint16>(address);
    }
    VDP2UpdateEnabledBGs();

    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::PostLoadStateSync());
        m_vdp1RenderingContext.postLoadSyncSignal.Wait();
        m_vdp1RenderingContext.postLoadSyncSignal.Reset();
    }

    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::PostLoadStateSync());
        m_vdp2RenderingContext.postLoadSyncSignal.Wait();
        m_vdp2RenderingContext.postLoadSyncSignal.Reset();
    }

    m_VDP1TimingPenaltyCycles = state.VDP1TimingPenalty;

    m_VDP1RenderState.sysClipH = state.renderer.vdp1State.sysClipH;
    m_VDP1RenderState.sysClipV = state.renderer.vdp1State.sysClipV;
    m_VDP1RenderState.doubleV = state.renderer.vdp1State.doubleV;
    m_VDP1RenderState.userClipX0 = state.renderer.vdp1State.userClipX0;
    m_VDP1RenderState.userClipY0 = state.renderer.vdp1State.userClipY0;
    m_VDP1RenderState.userClipX1 = state.renderer.vdp1State.userClipX1;
    m_VDP1RenderState.userClipY1 = state.renderer.vdp1State.userClipY1;
    m_VDP1RenderState.localCoordX = state.renderer.vdp1State.localCoordX;
    m_VDP1RenderState.localCoordY = state.renderer.vdp1State.localCoordY;
    m_VDP1RenderState.rendering = state.renderer.vdp1State.rendering;
    m_VDP1RenderState.doDisplayErase = state.renderer.vdp1State.doDisplayErase;
    m_VDP1RenderState.doVBlankErase = state.renderer.vdp1State.doVBlankErase;
    m_VDP1RenderState.eraseWriteValue = state.renderer.vdp1State.eraseWriteValue;
    m_VDP1RenderState.eraseX1 = state.renderer.vdp1State.eraseX1;
    m_VDP1RenderState.eraseY1 = state.renderer.vdp1State.eraseY1;
    m_VDP1RenderState.eraseX3 = state.renderer.vdp1State.eraseX3;
    m_VDP1RenderState.eraseY3 = state.renderer.vdp1State.eraseY3;
    m_VDP1RenderState.cycleCount = state.renderer.vdp1State.cycleCount;
    m_VDP1RenderState.cyclesSpent = state.renderer.vdp1State.cyclesSpent;
    m_VDP1RenderState.meshFB = state.renderer.vdp1State.meshFB;

    for (size_t i = 0; i < 4; i++) {
        m_normBGLayerStates[i].fracScrollX = state.renderer.normBGLayerStates[i].fracScrollX;
        m_normBGLayerStates[i].fracScrollY = state.renderer.normBGLayerStates[i].fracScrollY;
        m_normBGLayerStates[i].scrollAmountV = state.renderer.normBGLayerStates[i].scrollAmountV;
        m_normBGLayerStates[i].scrollIncH = state.renderer.normBGLayerStates[i].scrollIncH;
        m_normBGLayerStates[i].lineScrollTableAddress = state.renderer.normBGLayerStates[i].lineScrollTableAddress;
        m_normBGLayerStates[i].vertCellScrollOffset = state.renderer.normBGLayerStates[i].vertCellScrollOffset;
        m_normBGLayerStates[i].vertCellScrollDelay = state.renderer.normBGLayerStates[i].vertCellScrollDelay;
        m_normBGLayerStates[i].mosaicCounterY = state.renderer.normBGLayerStates[i].mosaicCounterY;
    }

    for (size_t i = 0; i < 2; i++) {
        m_rotParamStates[i].pageBaseAddresses = state.renderer.rotParamStates[i].pageBaseAddresses;
        m_rotParamStates[i].Xst = state.renderer.rotParamStates[i].Xst;
        m_rotParamStates[i].Yst = state.renderer.rotParamStates[i].Yst;
        m_rotParamStates[i].KA = state.renderer.rotParamStates[i].KA;
    }

    m_lineBackLayerState.lineColor.u32 = state.renderer.lineBackLayerState.lineColor;
    m_lineBackLayerState.backColor.u32 = state.renderer.lineBackLayerState.backColor;

    auto copyChar = [&](Character &dst, const state::VDPState::VDPRendererState::Character &src) {
        dst.charNum = src.charNum;
        dst.palNum = src.palNum;
        dst.specColorCalc = src.specColorCalc;
        dst.specPriority = src.specPriority;
        dst.flipH = src.flipH;
        dst.flipV = src.flipV;
    };

    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < 6; j++) {
            copyChar(m_vramFetchers[i][j].currChar, state.renderer.vramFetchers[i][j].currChar);
            copyChar(m_vramFetchers[i][j].nextChar, state.renderer.vramFetchers[i][j].nextChar);
            m_vramFetchers[i][j].lastCharIndex = state.renderer.vramFetchers[i][j].lastCharIndex;
            m_vramFetchers[i][j].lastCellX = state.renderer.vramFetchers[i][j].lastCellX;
            m_vramFetchers[i][j].bitmapData = state.renderer.vramFetchers[i][j].bitmapData;
            m_vramFetchers[i][j].bitmapDataAddress = state.renderer.vramFetchers[i][j].bitmapDataAddress;
            m_vramFetchers[i][j].lastVCellScroll = state.renderer.vramFetchers[i][j].lastVCellScroll;
        }
    }

    m_vertCellScrollInc = state.renderer.vertCellScrollInc;

    m_state.displayFB = state.renderer.displayFB;
    m_vdp2RenderingContext.displayFB = state.renderer.displayFB;

    m_displayEnabled = state.displayEnabled;
    m_borderColorMode = state.borderColorMode;

    UpdateResolution<true>();

    switch (m_state.VPhase) {
    case VerticalPhase::Active: [[fallthrough]];
    case VerticalPhase::BottomBorder: [[fallthrough]];
    case VerticalPhase::BlankingAndSync: m_state.regs2.VCNTSkip = 0; break;
    case VerticalPhase::VCounterSkip: [[fallthrough]];
    case VerticalPhase::TopBorder: [[fallthrough]];
    case VerticalPhase::LastLine: m_state.regs2.VCNTSkip = m_VCounterSkip; break;
    }
}

void VDP::SetLayerEnabled(Layer layer, bool enabled) {
    m_layerRendered[static_cast<size_t>(layer)] = enabled;
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2UpdateEnabledBGs());
    } else {
        VDP2UpdateEnabledBGs();
    }
}

bool VDP::IsLayerEnabled(Layer layer) const {
    return m_layerRendered[static_cast<size_t>(layer)];
}

void VDP::OnPhaseUpdateEvent(core::EventContext &eventContext, void *userContext) {
    auto &vdp = *static_cast<VDP *>(userContext);
    vdp.UpdatePhase();
    const uint64 cycles = vdp.GetPhaseCycles();
    eventContext.Reschedule(cycles);
}

void VDP::SetVideoStandard(VideoStandard videoStandard) {
    const bool pal = videoStandard == VideoStandard::PAL;
    if (m_state.regs2.TVSTAT.PAL != pal) {
        m_state.regs2.TVSTAT.PAL = pal;
        m_state.regs2.TVMDDirty = true;
    }
}

void VDP::EnableThreadedVDP1(bool enable) {
    if (m_threadedVDP1Rendering == enable) {
        return;
    }

    devlog::debug<grp::vdp1>("{} threaded VDP1 rendering", (enable ? "Enabling" : "Disabling"));

    m_threadedVDP1Rendering = enable;
    if (enable) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::PostLoadStateSync());
        m_VDP1RenderThread = std::thread{[&] { VDP1RenderThread(); }};
        m_vdp1RenderingContext.postLoadSyncSignal.Wait();
        m_vdp1RenderingContext.postLoadSyncSignal.Reset();
    } else {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::Shutdown());
        if (m_VDP1RenderThread.joinable()) {
            m_VDP1RenderThread.join();
        }

        VDP1RenderEvent dummy{};
        while (m_vdp1RenderingContext.eventQueue.try_dequeue(dummy)) {
        }
    }
}

void VDP::EnableThreadedVDP2(bool enable) {
    if (m_threadedVDP2Rendering == enable) {
        return;
    }

    devlog::debug<grp::vdp2>("{} threaded VDP2 rendering", (enable ? "Enabling" : "Disabling"));

    m_threadedVDP2Rendering = enable;
    if (enable) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::PostLoadStateSync());
        m_VDP2RenderThread = std::thread{[&] { VDP2RenderThread(); }};
        m_VDP2DeinterlaceRenderThread = std::thread{[&] { VDP2DeinterlaceRenderThread(); }};
        m_vdp2RenderingContext.postLoadSyncSignal.Wait();
        m_vdp2RenderingContext.postLoadSyncSignal.Reset();
    } else {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::Shutdown());
        if (m_VDP2RenderThread.joinable()) {
            m_VDP2RenderThread.join();
        }
        if (m_VDP2DeinterlaceRenderThread.joinable()) {
            m_VDP2DeinterlaceRenderThread.join();
        }

        VDP2RenderEvent dummy{};
        while (m_vdp2RenderingContext.eventQueue.try_dequeue(dummy)) {
        }
    }
}

template <mem_primitive T>
FORCE_INLINE void VDP::VDP2UpdateCRAMCache(uint32 address) {
    address &= ~1;
    const Color555 color5{.u16 = util::ReadBE<uint16>(&m_state.CRAM[address])};
    m_CRAMCache[address / sizeof(uint16)] = ConvertRGB555to888(color5);
    if constexpr (std::is_same_v<T, uint32>) {
        const Color555 color5{.u16 = util::ReadBE<uint16>(&m_state.CRAM[address + 2])};
        m_CRAMCache[(address + 2) / sizeof(uint16)] = ConvertRGB555to888(color5);
    }
}

FORCE_INLINE void VDP::UpdatePhase() {
    auto nextPhase = static_cast<uint32>(m_state.HPhase) + 1;
    if (nextPhase == m_HTimings.size()) {
        nextPhase = 0;
    }

    m_state.HPhase = static_cast<HorizontalPhase>(nextPhase);
    switch (m_state.HPhase) {
    case HorizontalPhase::Active: BeginHPhaseActiveDisplay(); break;
    case HorizontalPhase::RightBorder: BeginHPhaseRightBorder(); break;
    case HorizontalPhase::Sync: BeginHPhaseSync(); break;
    case HorizontalPhase::LeftBorder: BeginHPhaseLeftBorder(); break;
    }
}

FORCE_INLINE uint64 VDP::GetPhaseCycles() const {
    return m_HTimings[static_cast<uint32>(m_state.HPhase)];
}

template <bool verbose>
void VDP::UpdateResolution() {
    if (!m_state.regs2.TVMDDirty) {
        return;
    }

    m_state.regs2.TVMDDirty = false;

    // Horizontal/vertical resolution tables
    // NTSC uses the first two vRes entries, PAL uses the full table, and exclusive monitors use 480 lines
    // For exclusive monitor: even hRes entries are valid for 31 KHz monitors, odd are for Hi-Vision
    static constexpr uint32 hRes[] = {320, 352, 640, 704};
    static constexpr uint32 vRes[] = {224, 240, 256, 256};

    const bool exclusiveMonitor = m_state.regs2.TVMD.HRESOn & 4;
    const bool interlaced = m_state.regs2.TVMD.IsInterlaced();
    m_HRes = hRes[m_state.regs2.TVMD.HRESOn & 3];
    m_VRes = exclusiveMonitor ? 480 : vRes[m_state.regs2.TVMD.VRESOn & (m_state.regs2.TVSTAT.PAL ? 3 : 1)];
    if (!exclusiveMonitor && interlaced) {
        // Interlaced modes double the vertical resolution
        m_VRes *= 2;
    }
    m_exclusiveMonitor = exclusiveMonitor;

    // Timing tables

    // Horizontal phase timings (cycles until):
    //   RBd = Right Border
    //   HSy = Horizontal Sync
    //   LBd = Left Border
    //   ADp = Active Display
    // NOTE: these timings specify the HCNT interval between phases
    // TODO: check exclusive monitor timings
    static constexpr std::array<std::array<uint32, 4>, 8> hTimings{{
        // RBd, HSy, LBd, ADp
        {320, 54, 26, 27},  // {320, 374, 400, 427}, // Normal Graphic A
        {352, 51, 29, 23},  // {352, 403, 432, 455}, // Normal Graphic B
        {640, 108, 52, 54}, // {640, 748, 800, 854}, // Hi-Res Graphic A
        {704, 102, 58, 46}, // {704, 806, 864, 910}, // Hi-Res Graphic B
        {160, 27, 13, 13},  // {160, 187, 200, 213}, // Exclusive Normal Graphic A (wild guess)
        {176, 11, 13, 12},  // {176, 187, 200, 212}, // Exclusive Normal Graphic B (wild guess)
        {320, 54, 26, 26},  // {320, 374, 400, 426}, // Exclusive Hi-Res Graphic A (wild guess)
        {352, 22, 26, 24},  // {352, 374, 400, 424}, // Exclusive Hi-Res Graphic B (wild guess)
    }};
    m_HTimings = hTimings[m_state.regs2.TVMD.HRESOn];

    // Derive right shift amount to be applied to HCNT<<1
    m_state.regs2.HCNTShift = m_state.regs2.TVMD.HRESOn <= 1   ? 1  // Normal modes: HCNT << 1
                              : m_state.regs2.TVMD.HRESOn >= 6 ? 2  // Excl. Hi-Res: HCNT >> 1
                                                               : 0; // Other modes:  HCNT unchanged

    // Derive HCNT mask to be applied to (HCNT<<1) >> HCNTShift
    m_state.regs2.HCNTMask = m_state.regs2.TVMD.HRESOn <= 1   ? 0x3FE  // Normal modes:  HCT0 invalid
                             : m_state.regs2.TVMD.HRESOn >= 6 ? 0x1FF  // Any Exclusive: HCT9 invalid
                                                              : 0x3FF; // Other modes:   HCNT unchanged

    // Vertical phase timings (to reach):
    //   BBd = Bottom Border
    //   BSy = Blanking and Vertical Sync
    //   VCS = VCNT skip
    //   TBd = Top Border
    //   LLn = Last Line
    //   ADp = Active Display
    // NOTE: these timings indicate the VCNT at which the specified phase begins
    // TODO: check exclusive monitor timings
    // TODO: interlaced mode timings for odd fields:
    // - normal modes: 1 less line
    // - exclusive modes: 2 more lines
    static constexpr std::array<std::array<std::array<std::array<uint32, 6>, 2>, 4>, 3> vTimingsNormal{{
        // NTSC
        {{
            // BBd, BSy, VCS, TBd, LLn, ADp
            {{
                {224, 232, 237, 255, 262, 263}, // even/progressive
                {224, 232, 237, 255, 261, 262}, // odd
            }},
            {{
                {240, 240, 245, 255, 262, 263},
                {240, 240, 245, 255, 261, 262},
            }},
            {{
                {224, 232, 237, 255, 262, 263},
                {224, 232, 237, 255, 261, 262},
            }},
            {{
                {240, 240, 245, 255, 262, 263},
                {240, 240, 245, 255, 261, 262},
            }},
        }},
        // PAL
        {{
            // BBd, BSy, VCS, TBd, LLn, ADp
            {{
                {224, 256, 259, 281, 312, 313},
                {224, 256, 259, 281, 311, 312},
            }},
            {{
                {240, 264, 267, 289, 312, 313},
                {240, 264, 267, 289, 311, 312},
            }},
            {{
                {256, 272, 275, 297, 312, 313},
                {256, 272, 275, 297, 311, 312},
            }},
            {{
                {256, 272, 275, 297, 312, 313},
                {256, 272, 275, 297, 311, 312},
            }},
        }},
    }};
    static constexpr std::array<std::array<std::array<uint32, 6>, 2>, 2> vTimingsExclusive{{
        // Exclusive monitor A (wild guess)
        {{
            // BBd, BSy, VCS, TBd, LLn, ADp
            {480, 496, 506, 509, 524, 525}, // even/progressive
            {480, 496, 506, 509, 526, 527}, // odd
        }},
        // Exclusive monitor B (wild guess)
        {{
            // BBd, BSy, VCS, TBd, LLn, ADp
            {480, 496, 506, 546, 561, 562},
            {480, 496, 506, 546, 563, 564},
        }},
    }};
    m_VTimings = exclusiveMonitor ? vTimingsExclusive[m_state.regs2.TVMD.HRESOn & 1]
                                  : vTimingsNormal[m_state.regs2.TVSTAT.PAL][m_state.regs2.TVMD.VRESOn];
    m_VTimingField = static_cast<uint32>(interlaced) & m_state.regs2.TVSTAT.ODD;

    // Adjust for dot clock
    const uint32 dotClockMult = (m_state.regs2.TVMD.HRESOn & 2) ? 2 : 4;
    for (auto &timing : m_HTimings) {
        timing *= dotClockMult;
    }

    // Compute cycles available for VBlank erase
    // TODO: penalty should be 200, but doing so results in less pixels than necessary being erased
    // Test cases:
    //   Game                Where                Reso.     Sprite bits
    //   Battle Garegga      Options menu         320x480   16
    //   Die Hard Arcade     Menus, in-game       704x240   8
    //   Guardian Heroes     Main menu, in-game   320x224   16
    //   Linkle Liver Story  In-game              320x224   16
    //   Powerslave          Menus, in-game       320x240   16
    //   Panzer Dragoon      FMV subtitles        352x224   16
    //   Sonic R             In-game              352x224   16
    static constexpr uint32 kVBEHorzPenalty = 113;
    static constexpr std::array<uint32, 8> kVBEHorzTimings{{
        1708 - kVBEHorzPenalty, // Normal Graphic A
        1820 - kVBEHorzPenalty, // Normal Graphic B
        1708 - kVBEHorzPenalty, // Hi-Res Graphic A
        1820 - kVBEHorzPenalty, // Hi-Res Graphic B
        852 - kVBEHorzPenalty,  // Exclusive Normal Graphic A
        848 - kVBEHorzPenalty,  // Exclusive Normal Graphic B
        852 - kVBEHorzPenalty,  // Exclusive Hi-Res Graphic A
        848 - kVBEHorzPenalty,  // Exclusive Hi-Res Graphic B
    }};
    static constexpr auto kVPActiveIndex = static_cast<uint32>(VerticalPhase::Active);
    static constexpr auto kVPLastLineIndex = static_cast<uint32>(VerticalPhase::LastLine);
    m_VBlankEraseCyclesPerLine = kVBEHorzTimings[m_state.regs2.TVMD.HRESOn];
    m_VBlankEraseLines = {
        m_VTimings[0][kVPLastLineIndex] - m_VTimings[0][kVPActiveIndex],
        m_VTimings[1][kVPLastLineIndex] - m_VTimings[1][kVPActiveIndex],
    };

    m_state.regs2.VCNTShift = m_state.regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity ? 1 : 0;

    // TODO: field skips must be handled per frame
    if (exclusiveMonitor) {
        const uint16 baseSkip = (m_state.regs2.TVMD.HRESOn & 1) ? 562 : 525;
        const uint16 fieldSkip = ~m_state.regs2.TVSTAT.ODD & static_cast<uint16>(interlaced);
        m_VCounterSkip = ((0x400 - baseSkip) >> 1u) - fieldSkip;
    } else {
        const uint16 baseSkip = m_state.regs2.TVSTAT.PAL ? 313 : 263;
        const uint16 fieldSkip = ~m_state.regs2.TVSTAT.ODD & static_cast<uint16>(interlaced);
        m_VCounterSkip = 0x200 - baseSkip + fieldSkip;
    }

    // Clear framebuffer to avoid artifacts when switching modes
    uint32 color = 0xFF000000;
    if (m_state.regs2.TVMD.BDCLMD) {
        color |= m_lineBackLayerState.backColor.u32;
    }
    std::fill_n(m_framebuffer.begin(), m_HRes * m_VRes, color);

    if constexpr (verbose) {
        devlog::info<grp::vdp2>("Screen resolution set to {}x{}", m_HRes, m_VRes);
        switch (m_state.regs2.TVMD.LSMDn) {
        case InterlaceMode::None: devlog::info<grp::vdp2>("Non-interlace mode"); break;
        case InterlaceMode::Invalid: devlog::info<grp::vdp2>("Invalid interlace mode"); break;
        case InterlaceMode::SingleDensity: devlog::info<grp::vdp2>("Single-density interlace mode"); break;
        case InterlaceMode::DoubleDensity: devlog::info<grp::vdp2>("Double-density interlace mode"); break;
        }
        devlog::info<grp::vdp2>("Dot clock mult = {}, display {}", dotClockMult, (m_displayEnabled ? "ON" : "OFF"));
    }
}

FORCE_INLINE void VDP::IncrementVCounter() {
    ++m_state.regs2.VCNT;
    while (m_state.regs2.VCNT >= m_VTimings[m_VTimingField][static_cast<uint32>(m_state.VPhase)]) {
        auto nextPhase = static_cast<uint32>(m_state.VPhase) + 1;
        if (nextPhase == m_VTimings[m_VTimingField].size()) {
            m_state.regs2.VCNT = 0;
            nextPhase = 0;
        }

        m_state.VPhase = static_cast<VerticalPhase>(nextPhase);
        switch (m_state.VPhase) {
        case VerticalPhase::Active: BeginVPhaseActiveDisplay(); break;
        case VerticalPhase::BottomBorder: BeginVPhaseBottomBorder(); break;
        case VerticalPhase::BlankingAndSync: BeginVPhaseBlankingAndSync(); break;
        case VerticalPhase::VCounterSkip: BeginVPhaseVCounterSkip(); break;
        case VerticalPhase::TopBorder: BeginVPhaseTopBorder(); break;
        case VerticalPhase::LastLine: BeginVPhaseLastLine(); break;
        }
    }
    devlog::trace<grp::base>("VCNT = {:3d}  phase = {:d}", m_state.regs2.VCNT, static_cast<uint32>(m_state.VPhase));
}

// ----

void VDP::BeginHPhaseActiveDisplay() {
    devlog::trace<grp::phase>("(VCNT = {:3d})  Entering horizontal active display phase", m_state.regs2.VCNT);
    if (m_state.VPhase == VerticalPhase::Active) {
        if (m_state.regs2.VCNT == m_VTimings[m_VTimingField][0] - 16) { // ~1ms before VBlank IN
            m_cbTriggerOptimizedINTBACKRead();
        }

        if (m_threadedVDP2Rendering) {
            m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2DrawLine(m_state.regs2.VCNT));
            VDP2CalcAccessPatterns(m_state.regs2);
        } else {
            const bool interlaced = m_state.regs2.TVMD.IsInterlaced();
            const uint32 y = m_state.regs2.VCNT;
            VDP2PrepareLine(y);
            (this->*m_fnVDP2DrawLine)(y, false);
            if (m_deinterlaceRender && interlaced) {
                (this->*m_fnVDP2DrawLine)(y, true);
            }
            VDP2FinishLine(y);
        }
    }
}

void VDP::BeginHPhaseRightBorder() {
    devlog::trace<grp::phase>("(VCNT = {:3d})  Entering right border phase", m_state.regs2.VCNT);

    devlog::trace<grp::intr_hb>("## HBlank IN {:3d}", m_state.regs2.VCNT);

    m_state.regs2.TVSTAT.HBLANK = 1;
    m_cbHBlankStateChange(true, m_state.regs2.TVSTAT.VBLANK);

    // Start erasing if we just entered VBlank IN
    if (m_state.regs2.VCNT == m_VTimings[m_VTimingField][static_cast<uint32>(VerticalPhase::Active)]) {
        devlog::trace<grp::intr>("## HBlank IN + VBlank IN  VBE={:d}", m_state.regs1.vblankErase);

        m_VDP1RenderState.doVBlankErase = m_state.regs1.vblankErase;

        // If we just entered the bottom blanking vertical phase, switch fields
        if (m_state.regs2.TVMD.LSMDn != InterlaceMode::None) {
            m_state.regs2.TVSTAT.ODD ^= 1;
            m_VTimingField = m_state.regs2.TVSTAT.ODD;
            devlog::trace<grp::vdp2_render>("Switched to {} field", (m_state.regs2.TVSTAT.ODD ? "odd" : "even"));
            if (m_threadedVDP2Rendering) {
                m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::OddField(m_state.regs2.TVSTAT.ODD));
            }
        } else {
            if (m_state.regs2.TVSTAT.ODD != 1) {
                m_state.regs2.TVSTAT.ODD = 1;
                m_VTimingField = 0;
                if (m_threadedVDP2Rendering) {
                    m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::OddField(m_state.regs2.TVSTAT.ODD));
                }
            }
        }
    }

    // TODO: draw border
}

void VDP::BeginHPhaseSync() {
    devlog::trace<grp::phase>("(VCNT = {:3d})  Entering horizontal sync phase", m_state.regs2.VCNT);

    // This phase intentionally does nothing to insert a gap between the two border phases
}

void VDP::BeginHPhaseLeftBorder() {
    devlog::trace<grp::phase>("(VCNT = {:3d})  Entering left border phase", m_state.regs2.VCNT);

    if (m_state.VPhase == VerticalPhase::LastLine) {
        auto &ctx1 = m_VDP1RenderState;

        devlog::trace<grp::intr>("## HBlank end + VBlank OUT  FCM={:d} FCT={:d} VBE={:d} PTM={:d} changed={}",
                                 m_state.regs1.fbSwapMode, m_state.regs1.fbSwapTrigger, m_state.regs1.vblankErase,
                                 m_state.regs1.plotTrigger, m_state.regs1.fbParamsChanged);

        bool erase = false;
        bool swap = false;

        if (!m_state.regs1.fbSwapMode) {
            // 1-cycle framebuffer erase+swap
            erase = true;
            swap = true;
        } else if (m_state.regs1.fbParamsChanged) {
            // Manual erase/swap
            if (m_state.regs1.fbSwapTrigger) {
                swap = true;
            } else {
                erase = true;
            }
        }

        // Clear manual erase/swap trigger
        m_state.regs1.fbParamsChanged = false;

        // Reset cycles spent by VDP1 this frame
        m_VDP1RenderState.cyclesSpent = 0;

        // End VBlank erase if in progress
        if (m_VDP1RenderState.doVBlankErase) {
            if (m_threadedVDP2Rendering) {
                m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP1EraseFramebuffer());
                m_vdp2RenderingContext.eraseFramebufferReadySignal.Wait();
                m_vdp2RenderingContext.eraseFramebufferReadySignal.Reset();
            }
            VDP1EraseFramebuffer<true>(m_VBlankEraseCyclesPerLine * m_VBlankEraseLines[m_VTimingField]);
        }

        if (erase) {
            ctx1.doDisplayErase = true;
        }
        if (swap) {
            VDP1SwapFramebuffer();
        }
    }

    m_state.regs2.TVSTAT.HBLANK = 0;
    if (m_state.VPhase == VerticalPhase::Active) {
        m_cbHBlankStateChange(false, m_state.regs2.TVSTAT.VBLANK);
    }

    IncrementVCounter();

    // TODO: draw border
}

// ----

void VDP::BeginVPhaseActiveDisplay() {
    devlog::trace<grp::phase>("(VCNT = {:3d})  Entering vertical active display phase", m_state.regs2.VCNT);

    m_state.regs2.VCNTSkip = 0;
}

void VDP::BeginVPhaseBottomBorder() {
    devlog::trace<grp::phase>("(VCNT = {:3d})  Entering bottom border phase", m_state.regs2.VCNT);

    devlog::trace<grp::intr>("## VBlank IN");

    m_state.regs2.TVSTAT.VBLANK = 1;
    m_cbVBlankStateChange(true);
    m_cbTriggerSMPCVBlankIN();

    // TODO: draw border
}

void VDP::BeginVPhaseBlankingAndSync() {
    devlog::trace<grp::phase>("(VCNT = {:3d})  Entering blanking/vertical sync phase", m_state.regs2.VCNT);

    // End frame
    devlog::trace<grp::vdp2_render>("End VDP2 frame");
    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2EndFrame());
        m_vdp2RenderingContext.renderFinishedSignal.Wait();
        m_vdp2RenderingContext.renderFinishedSignal.Reset();
    }
    m_cbFrameComplete(m_framebuffer.data(), m_HRes, m_VRes);

    // Begin erasing display framebuffer during display
    if (m_VDP1RenderState.doDisplayErase) {
        m_VDP1RenderState.doDisplayErase = false;
        // TODO: erase line by line instead of the entire framebuffer in one go
        if (m_threadedVDP2Rendering) {
            m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP1EraseFramebuffer());
            m_vdp2RenderingContext.eraseFramebufferReadySignal.Wait();
            m_vdp2RenderingContext.eraseFramebufferReadySignal.Reset();
        }
        // No need to count cycles here; there's always enough cycles in the display area to clear the entire screen
        VDP1EraseFramebuffer<false>();
    }
}

void VDP::BeginVPhaseVCounterSkip() {
    devlog::trace<grp::phase>("(VCNT = {:3d})  Entering vertical counter skip phase", m_state.regs2.VCNT);

    m_state.regs2.VCNTSkip = m_VCounterSkip;
}

void VDP::BeginVPhaseTopBorder() {
    devlog::trace<grp::phase>("(VCNT = {:3d})  Entering top border phase", m_state.regs2.VCNT);

    UpdateResolution<true>();

    // Latch TVMD flags
    m_displayEnabled = m_state.regs2.TVMD.DISP;
    m_borderColorMode = m_state.regs2.TVMD.BDCLMD;

    // TODO: draw border
}

void VDP::BeginVPhaseLastLine() {
    devlog::trace<grp::phase>("(VCNT = {:3d})  Entering last line phase", m_state.regs2.VCNT);

    devlog::trace<grp::intr>("## VBlank OUT");

    devlog::trace<grp::vdp2_render>("Begin VDP2 frame, VDP1 framebuffer {}", m_state.displayFB);

    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP2BeginFrame());
    } else {
        VDP2InitFrame();
    }

    m_state.regs2.TVSTAT.VBLANK = 0;
    m_cbVBlankStateChange(false);
}

// -----------------------------------------------------------------------------
// Rendering

void VDP::VDP1RenderThread() {
    util::SetCurrentThreadName("VDP1 render thread");

    auto &rctx = m_vdp1RenderingContext;

    std::array<VDP1RenderEvent, 64> events{};

    bool running = true;
    while (running) {
        const size_t count = rctx.DequeueEvents(events.begin(), events.size());

        for (size_t i = 0; i < count; ++i) {
            const auto &event = events[i];
            using EvtType = VDP1RenderEvent::Type;
            switch (event.type) {
            case EvtType::Reset: rctx.Reset(); break;

            case EvtType::SwapBuffers: rctx.swapBuffersSignal.Set(); break;
            case EvtType::Command: (this->*m_fnVDP1HandleCommand)(event.command.address, event.command.control); break;

            case EvtType::VRAMWriteByte: rctx.vdp1.VRAM[event.write.address] = event.write.value; break;
            case EvtType::VRAMWriteWord:
                util::WriteBE<uint16>(&rctx.vdp1.VRAM[event.write.address], event.write.value);
                break;
            case EvtType::RegWrite: rctx.vdp1.regs.Write<false>(event.write.address, event.write.value); break;

            case EvtType::PreSaveStateSync: rctx.preSaveSyncSignal.Set(); break;
            case EvtType::PostLoadStateSync:
                rctx.vdp1.regs = m_state.regs1;
                rctx.vdp1.VRAM = m_state.VRAM1;
                rctx.postLoadSyncSignal.Set();
                break;

            case EvtType::Shutdown: running = false; break;
            }
        }
    }
}

void VDP::VDP2RenderThread() {
    util::SetCurrentThreadName("VDP2 render thread");

    auto &rctx = m_vdp2RenderingContext;

    std::array<VDP2RenderEvent, 64> events{};

    bool running = true;
    while (running) {
        const size_t count = rctx.DequeueEvents(events.begin(), events.size());

        for (size_t i = 0; i < count; ++i) {
            const auto &event = events[i];
            using EvtType = VDP2RenderEvent::Type;
            switch (event.type) {
            case EvtType::Reset:
                rctx.Reset();
                m_framebuffer.fill(0xFF000000);
                break;
            case EvtType::OddField: rctx.vdp2.regs.TVSTAT.ODD = event.oddField.odd; break;
            case EvtType::VDP1EraseFramebuffer: rctx.eraseFramebufferReadySignal.Set(); break;
            case EvtType::VDP1SwapFramebuffer:
                rctx.displayFB ^= 1;
                rctx.framebufferSwapSignal.Set();
                break;

            case EvtType::VDP2BeginFrame: VDP2InitFrame(); break;
            case EvtType::VDP2UpdateEnabledBGs: VDP2UpdateEnabledBGs(); break;
            case EvtType::VDP2DrawLine: //
            {
                const bool deinterlaceRender = m_deinterlaceRender;
                const bool threadedDeinterlacer = m_threadedDeinterlacer;
                const bool interlaced = rctx.vdp2.regs.TVMD.IsInterlaced();
                VDP2PrepareLine(event.drawLine.vcnt);
                if (deinterlaceRender && interlaced && threadedDeinterlacer) {
                    rctx.deinterlaceY = event.drawLine.vcnt;
                    rctx.deinterlaceRenderBeginSignal.Set();
                }
                (this->*m_fnVDP2DrawLine)(event.drawLine.vcnt, false);
                if (deinterlaceRender && interlaced) {
                    if (threadedDeinterlacer) {
                        rctx.deinterlaceRenderEndSignal.Wait();
                        rctx.deinterlaceRenderEndSignal.Reset();
                    } else {
                        (this->*m_fnVDP2DrawLine)(event.drawLine.vcnt, true);
                    }
                }
                VDP2FinishLine(event.drawLine.vcnt);
                break;
            }
            case EvtType::VDP2EndFrame: rctx.renderFinishedSignal.Set(); break;

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
                    switch (event.write.address) {
                    case 0x074: [[fallthrough]]; // SCYIN0
                    case 0x076:                  // SCYDN0
                        m_normBGLayerStates[0].scrollAmountV = rctx.vdp2.regs.bgParams[1].scrollAmountV;
                        break;
                    case 0x084: [[fallthrough]]; // SCYIN1
                    case 0x086:                  // SCYDN1
                        m_normBGLayerStates[1].scrollAmountV = rctx.vdp2.regs.bgParams[2].scrollAmountV;
                        break;
                    case 0x092: // SCYN2
                        m_normBGLayerStates[2].scrollAmountV = rctx.vdp2.regs.bgParams[3].scrollAmountV;
                        m_normBGLayerStates[2].fracScrollY = 0;
                        break;
                    case 0x096: // SCYN3
                        m_normBGLayerStates[3].scrollAmountV = rctx.vdp2.regs.bgParams[4].scrollAmountV;
                        m_normBGLayerStates[3].fracScrollY = 0;
                        break;
                    }
                }
                break;

            case EvtType::PreSaveStateSync: rctx.preSaveSyncSignal.Set(); break;
            case EvtType::PostLoadStateSync:
                rctx.vdp2.regs = m_state.regs2;
                rctx.vdp2.VRAM = m_state.VRAM2;
                rctx.vdp2.CRAM = m_state.CRAM;
                rctx.postLoadSyncSignal.Set();
                VDP2UpdateEnabledBGs();
                for (uint32 addr = 0; addr < rctx.vdp2.CRAM.size(); addr += sizeof(uint16)) {
                    const uint16 colorValue = VDP2ReadRendererCRAM<uint16>(addr);
                    const Color555 color5{.u16 = colorValue};
                    rctx.vdp2.CRAMCache[addr / sizeof(uint16)] = ConvertRGB555to888(color5);
                }
                break;

            case EvtType::Shutdown:
                rctx.deinterlaceShutdown = true;
                rctx.deinterlaceRenderBeginSignal.Set();
                rctx.deinterlaceRenderEndSignal.Wait();
                rctx.deinterlaceRenderEndSignal.Reset();
                running = false;
                break;
            }
        }
    }
}

void VDP::VDP2DeinterlaceRenderThread() {
    util::SetCurrentThreadName("VDP deinterlace render thread");

    auto &rctx = m_vdp2RenderingContext;

    while (true) {
        rctx.deinterlaceRenderBeginSignal.Wait();
        rctx.deinterlaceRenderBeginSignal.Reset();
        if (rctx.deinterlaceShutdown) {
            rctx.deinterlaceShutdown = false;
            rctx.deinterlaceRenderEndSignal.Set();
            return;
        }

        (this->*m_fnVDP2DrawLine)(rctx.deinterlaceY, true);
        rctx.deinterlaceRenderEndSignal.Set();
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP1ReadRendererVRAM(uint32 address) {
    if (m_threadedVDP1Rendering) {
        return util::ReadBE<T>(&m_vdp1RenderingContext.vdp1.VRAM[address & 0x7FFFF]);
    } else {
        return VDP1ReadVRAM<T>(address);
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP2ReadRendererVRAM(uint32 address) {
    if (m_threadedVDP2Rendering) {
        // TODO: handle VRSIZE.VRAMSZ
        address &= 0x7FFFF;
        return util::ReadBE<T>(&m_vdp2RenderingContext.vdp2.VRAM[address]);
    } else {
        return VDP2ReadVRAM<T>(address);
    }
}

template <mem_primitive T>
FORCE_INLINE T VDP::VDP2ReadRendererCRAM(uint32 address) {
    if (m_threadedVDP2Rendering) {
        if constexpr (std::is_same_v<T, uint32>) {
            uint32 value = VDP2ReadRendererCRAM<uint16>(address + 0) << 16u;
            value |= VDP2ReadRendererCRAM<uint16>(address + 2) << 0u;
            return value;
        }

        address = MapRendererCRAMAddress(address);
        return util::ReadBE<T>(&m_vdp2RenderingContext.vdp2.CRAM[address]);
    } else {
        return VDP2ReadCRAM<T, false>(address);
    }
}

FORCE_INLINE std::array<uint8, kVDP2VRAMSize> &VDP::VDP2GetRendererVRAM() {
    return m_threadedVDP2Rendering ? m_vdp2RenderingContext.vdp2.VRAM : m_state.VRAM2;
}

FORCE_INLINE Color888 VDP::VDP2ReadRendererColor5to8(uint32 address) const {
    if (m_threadedVDP2Rendering) {
        return m_vdp2RenderingContext.vdp2.CRAMCache[(address / sizeof(uint16)) & 0x7FF];
    } else {
        return m_CRAMCache[(address / sizeof(uint16)) & 0x7FF];
    }
}

void VDP::UpdateFunctionPointers() {
    UpdateFunctionPointersTemplate(m_deinterlaceRender, m_transparentMeshes);
}

template <bool... t_features>
void VDP::UpdateFunctionPointersTemplate(bool feature, auto... features) {
    feature ? UpdateFunctionPointersTemplate<t_features..., true>(features...)
            : UpdateFunctionPointersTemplate<t_features..., false>(features...);
}

template <bool... t_features>
void VDP::UpdateFunctionPointersTemplate() {
    m_fnVDP1ProcessCommand = &VDP::VDP1ProcessCommand<t_features...>;
    m_fnVDP1HandleCommand = &VDP::VDP1Cmd_Handle<t_features...>;
    m_fnVDP2DrawLine = &VDP::VDP2DrawLine<t_features...>;
}

// -----------------------------------------------------------------------------
// VDP1

FORCE_INLINE VDP1Regs &VDP::VDP1GetRegs() {
    return m_state.regs1;
}

FORCE_INLINE const VDP1Regs &VDP::VDP1GetRegs() const {
    return m_state.regs1;
}

FORCE_INLINE uint8 VDP::VDP1GetDisplayFBIndex() const {
    return m_state.displayFB;
}

template <bool countCycles>
FORCE_INLINE void VDP::VDP1EraseFramebuffer(uint64 cycles) {
    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();
    auto &ctx = m_VDP1RenderState;

    devlog::trace<grp::vdp1_render>("Erasing framebuffer {} - {}x{} to {}x{} -> {:04X}  {}x{}  {}-bit",
                                    m_state.displayFB, ctx.eraseX1, ctx.eraseY1, ctx.eraseX3, ctx.eraseY3,
                                    ctx.eraseWriteValue, regs1.fbSizeH, regs1.fbSizeV, (regs1.pixel8Bits ? 8 : 16));

    const uint8 fbIndex = VDP1GetDisplayFBIndex();
    auto &fb = m_state.spriteFB[fbIndex];
    auto &altFB = m_altSpriteFB[fbIndex];
    [[maybe_unused]] auto &meshFB = ctx.meshFB[0][fbIndex];
    [[maybe_unused]] auto &altMeshFB = ctx.meshFB[1][fbIndex];

    const uint32 fbOffsetShift = regs1.eraseOffsetShift;

    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    // Vertical scale is doubled in double-interlace mode
    const uint32 scaleV = doubleDensity ? 1 : 0;

    // Constrain erase area to certain limits based on current resolution
    const uint32 maxH = (regs2.TVMD.HRESOn & 1) ? 428 : 400;
    const uint32 maxV = m_VRes >> scaleV;

    const uint32 x1 = std::min<uint32>(ctx.eraseX1, maxH);
    const uint32 x3 = std::min<uint32>(ctx.eraseX3, maxH);
    const uint32 y1 = std::min<uint32>(ctx.eraseY1, maxV) << scaleV;
    const uint32 y3 = std::min<uint32>(ctx.eraseY3, maxV) << scaleV;

    const bool mirror = m_deinterlaceRender && doubleDensity;

    static constexpr uint64 kCyclesPerWrite = 1;

    for (uint32 y = y1; y <= y3; y++) {
        const uint32 fbOffset = y << fbOffsetShift;
        for (uint32 x = x1; x < x3; x++) {
            const uint32 address = (fbOffset + x) * sizeof(uint16);
            util::WriteBE<uint16>(&fb[address & 0x3FFFE], ctx.eraseWriteValue);
            if (mirror) {
                util::WriteBE<uint16>(&altFB[address & 0x3FFFE], ctx.eraseWriteValue);
            }

            if (m_transparentMeshes) {
                util::WriteBE<uint16>(&meshFB[address & 0x3FFFE], 0);
                if (mirror) {
                    util::WriteBE<uint16>(&altMeshFB[address & 0x3FFFE], 0);
                }
            }

            if constexpr (countCycles) {
                if (cycles >= kCyclesPerWrite) {
                    cycles -= kCyclesPerWrite;
                } else {
                    devlog::trace<grp::vdp1_render>("Erase process ran out of cycles");
                    return;
                }
            }
        }
    }
}

FORCE_INLINE void VDP::VDP1SwapFramebuffer() {
    devlog::trace<grp::vdp1_render>("Swapping framebuffers - draw {}, display {}", m_state.displayFB,
                                    m_state.displayFB ^ 1);

    if (m_threadedVDP2Rendering) {
        m_vdp2RenderingContext.EnqueueEvent(VDP2RenderEvent::VDP1SwapFramebuffer());
        m_vdp2RenderingContext.framebufferSwapSignal.Wait();
        m_vdp2RenderingContext.framebufferSwapSignal.Reset();
    }

    if (m_threadedVDP1Rendering) {
        m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::SwapBuffers());
        m_vdp1RenderingContext.swapBuffersSignal.Wait();
        m_vdp1RenderingContext.swapBuffersSignal.Reset();
    }

    m_state.regs1.prevCommandAddress = m_state.regs1.currCommandAddress;
    m_state.regs1.prevFrameEnded = m_state.regs1.currFrameEnded;
    m_state.regs1.currFrameEnded = false;

    m_state.displayFB ^= 1;

    m_cbVDP1FramebufferSwap();

    if (bit::test<1>(m_state.regs1.plotTrigger)) {
        VDP1BeginFrame();
    }

    // TODO: latch PTM, EOS, DIE, DIL

    // Latch erase parameters
    m_VDP1RenderState.eraseWriteValue = m_state.regs1.eraseWriteValue;
    m_VDP1RenderState.eraseX1 = m_state.regs1.eraseX1;
    m_VDP1RenderState.eraseY1 = m_state.regs1.eraseY1;
    m_VDP1RenderState.eraseX3 = m_state.regs1.eraseX3;
    m_VDP1RenderState.eraseY3 = m_state.regs1.eraseY3;
}

void VDP::VDP1BeginFrame() {
    devlog::trace<grp::vdp1_render>("Begin VDP1 frame on framebuffer {}", VDP1GetDisplayFBIndex() ^ 1);

    // TODO: setup rendering
    // TODO: figure out VDP1 timings

    m_state.regs1.returnAddress = kVDP1NoReturn;
    m_state.regs1.currCommandAddress = 0;
    m_state.regs1.currFrameEnded = false;

    // HACK: insert a delay to dodge some timing issues with games that trigger drawing too early
    // (e.g.: Fighter's History Dynamite, Cyberbots - Fullmetal Madness)
    m_VDP1TimingPenaltyCycles += 1500;

    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();
    m_VDP1RenderState.doubleV =
        m_deinterlaceRender && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;

    m_VDP1RenderState.rendering = true;
}

void VDP::VDP1EndFrame() {
    devlog::trace<grp::vdp1_render>("End VDP1 frame on framebuffer {}", VDP1GetDisplayFBIndex() ^ 1);
    m_VDP1RenderState.rendering = false;
    m_VDP1TimingPenaltyCycles = 0;

    m_state.regs1.currFrameEnded = true;
    m_cbTriggerSpriteDrawEnd();
    m_cbVDP1DrawFinished();
}

template <bool deinterlace, bool transparentMeshes>
void VDP::VDP1ProcessCommand() {
    if (!m_VDP1RenderState.rendering) {
        return;
    }
    if (m_VDP1RenderState.cyclesSpent >= kVDP1CycleBudgetPerFrame) {
        return;
    }

    auto &cmdAddress = m_state.regs1.currCommandAddress;

    const VDP1Command::Control control{.u16 = VDP1ReadVRAM<uint16>(cmdAddress)};
    devlog::trace<grp::vdp1_cmd>("Processing command {:04X} @ {:05X}", control.u16, cmdAddress);
    if (control.end) [[unlikely]] {
        devlog::trace<grp::vdp1_cmd>("End of command list");
        VDP1EndFrame();
    } else if (!control.skip) {
        if (!control.IsValid()) [[unlikely]] {
            devlog::debug<grp::vdp1_cmd>("Invalid command {:X}; aborting", static_cast<uint16>(control.command));
            VDP1EndFrame();
            return;
        }

        if (m_threadedVDP1Rendering) {
            m_vdp1RenderingContext.EnqueueEvent(VDP1RenderEvent::Command(cmdAddress, control));
        } else {
            VDP1Cmd_Handle<deinterlace, transparentMeshes>(cmdAddress, control);
        }

        m_VDP1RenderState.cyclesSpent += VDP1CalcCommandTiming(cmdAddress, control);
    }

    // Go to the next command
    {
        using enum VDP1Command::JumpType;

        switch (control.jumpMode) {
        case Next: cmdAddress += 0x20; break;
        case Assign: {
            cmdAddress = (VDP1ReadVRAM<uint16>(cmdAddress + 0x02) << 3u) & ~0x1F;
            devlog::trace<grp::vdp1_cmd>("Jump to {:05X}", cmdAddress);

            // HACK: Sonic R attempts to jump back to 0 in some cases
            if (cmdAddress == 0) {
                devlog::warn<grp::vdp1_cmd>("Possible infinite loop detected; aborting");
                VDP1EndFrame();
                return;
            }
            break;
        }
        case Call: {
            // Nested calls seem to not update the return address
            if (m_state.regs1.returnAddress == kVDP1NoReturn) {
                m_state.regs1.returnAddress = cmdAddress + 0x20;
            }
            cmdAddress = (VDP1ReadVRAM<uint16>(cmdAddress + 0x02) << 3u) & ~0x1F;
            devlog::trace<grp::vdp1_cmd>("Call {:05X}", cmdAddress);
            break;
        }
        case Return: {
            // Return seems to only return if there was a previous Call
            if (m_state.regs1.returnAddress != kVDP1NoReturn) {
                cmdAddress = m_state.regs1.returnAddress;
                m_state.regs1.returnAddress = kVDP1NoReturn;
            } else {
                cmdAddress += 0x20;
            }
            devlog::trace<grp::vdp1_cmd>("Return to {:05X}", cmdAddress);
            break;
        }
        }
        cmdAddress &= 0x7FFFF;
    }
}

template <bool deinterlace>
FORCE_INLINE bool VDP::VDP1IsPixelClipped(CoordS32 coord, bool userClippingEnable, bool clippingMode) const {
    if (VDP1IsPixelSystemClipped<deinterlace>(coord)) {
        return true;
    }
    if (userClippingEnable) {
        // clippingMode = false -> draw inside, reject outside
        // clippingMode = true -> draw outside, reject inside
        // The function returns true if the pixel is clipped, therefore we want to reject pixels that return the
        // opposite of clippingMode on that function.
        if (VDP1IsPixelUserClipped<deinterlace>(coord) != clippingMode) {
            return true;
        }
    }
    return false;
}

template <bool deinterlace>
FORCE_INLINE bool VDP::VDP1IsPixelUserClipped(CoordS32 coord) const {
    auto [x, y] = coord;
    const auto &ctx = m_VDP1RenderState;
    if (x < ctx.userClipX0 || x > ctx.userClipX1) {
        return true;
    }
    if (y < (ctx.userClipY0 << ctx.doubleV) || y > (ctx.userClipY1 << ctx.doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace>
FORCE_INLINE bool VDP::VDP1IsPixelSystemClipped(CoordS32 coord) const {
    auto [x, y] = coord;
    const auto &ctx = m_VDP1RenderState;
    if (x < 0 || x > ctx.sysClipH) {
        return true;
    }
    if (y < 0 || y > (ctx.sysClipV << ctx.doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace>
FORCE_INLINE bool VDP::VDP1IsLineSystemClipped(CoordS32 coord1, CoordS32 coord2) const {
    auto [x1, y1] = coord1;
    auto [x2, y2] = coord2;
    const auto &ctx = m_VDP1RenderState;
    if (x1 < 0 && x2 < 0) {
        return true;
    }
    if (y1 < 0 && y2 < 0) {
        return true;
    }
    if (x1 > ctx.sysClipH && x2 > ctx.sysClipH) {
        return true;
    }
    if (y1 > (ctx.sysClipV << ctx.doubleV) && y2 > (ctx.sysClipV << ctx.doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace>
bool VDP::VDP1IsQuadSystemClipped(CoordS32 coord1, CoordS32 coord2, CoordS32 coord3, CoordS32 coord4) const {
    auto [x1, y1] = coord1;
    auto [x2, y2] = coord2;
    auto [x3, y3] = coord3;
    auto [x4, y4] = coord4;
    const auto &ctx = m_VDP1RenderState;
    if (x1 < 0 && x2 < 0 && x3 < 0 && x4 < 0) {
        return true;
    }
    if (y1 < 0 && y2 < 0 && y3 < 0 && y4 < 0) {
        return true;
    }
    if (x1 > ctx.sysClipH && x2 > ctx.sysClipH && x3 > ctx.sysClipH && x4 > ctx.sysClipH) {
        return true;
    }
    if (y1 > (ctx.sysClipV << ctx.doubleV) && y2 > (ctx.sysClipV << ctx.doubleV) &&
        y3 > (ctx.sysClipV << ctx.doubleV) && y4 > (ctx.sysClipV << ctx.doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace, bool transparentMeshes>
FORCE_INLINE bool VDP::VDP1PlotPixel(CoordS32 coord, const VDP1PixelParams &pixelParams) {
    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();

    auto [x, y] = coord;

    // Reject pixels outside of clipping area
    if (VDP1IsPixelClipped<deinterlace>(coord, pixelParams.mode.userClippingEnable, pixelParams.mode.clippingMode)) {
        return false;
    }

    if constexpr (!transparentMeshes) {
        if (pixelParams.mode.meshEnable && ((x ^ y) & 1)) {
            return true;
        }
    }

    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;
    const bool altFB = deinterlace && doubleDensity && (y & 1);
    if (doubleDensity) {
        if (!deinterlace && regs1.dblInterlaceEnable && (y & 1) != regs1.dblInterlaceDrawLine) {
            return true;
        }
    }
    if ((deinterlace && doubleDensity) || regs1.dblInterlaceEnable) {
        y >>= 1;
    }

    // TODO: pixelParams.mode.preClippingDisable

    uint32 fbOffset = y * regs1.fbSizeH + x;
    const auto fbIndex = VDP1GetDisplayFBIndex() ^ 1;
    auto &drawFB = (altFB ? m_altSpriteFB : m_state.spriteFB)[fbIndex];
    if (regs1.pixel8Bits) {
        fbOffset &= 0x3FFFF;
        // TODO: what happens if pixelParams.mode.colorCalcBits/gouraudEnable != 0?
        if (pixelParams.mode.msbOn) {
            drawFB[fbOffset] |= 0x80;
        } else if (transparentMeshes && pixelParams.mode.meshEnable) {
            m_VDP1RenderState.meshFB[altFB][fbIndex][fbOffset] = pixelParams.color;
        } else {
            drawFB[fbOffset] = pixelParams.color;
            if constexpr (transparentMeshes) {
                m_VDP1RenderState.meshFB[altFB][fbIndex][fbOffset] = 0;
            }
        }
    } else {
        fbOffset = (fbOffset * sizeof(uint16)) & 0x3FFFE;
        uint8 *pixel = &drawFB[fbOffset];

        if (pixelParams.mode.msbOn) {
            *pixel |= 0x80;
        } else {
            Color555 srcColor{.u16 = pixelParams.color};
            Color555 dstColor{.u16 = util::ReadBE<uint16>(pixel)};

            // Apply color calculations
            //
            // In all cases where calculation is done, the raw color data to be drawn ("original graphic") or from
            // the background are interpreted as 5:5:5 RGB.

            if (pixelParams.mode.gouraudEnable) {
                // Apply gouraud shading to source color
                srcColor = pixelParams.gouraud.Blend(srcColor);
            }

            switch (pixelParams.mode.colorCalcBits) {
            case 0: // Replace
                dstColor = srcColor;
                break;
            case 1: // Shadow
                // Halve destination luminosity if it's not transparent
                if (dstColor.msb) {
                    dstColor.r >>= 1u;
                    dstColor.g >>= 1u;
                    dstColor.b >>= 1u;
                }
                break;
            case 2: // Half-luminance
                // Draw original graphic with halved luminance
                dstColor.r = srcColor.r >> 1u;
                dstColor.g = srcColor.g >> 1u;
                dstColor.b = srcColor.b >> 1u;
                dstColor.msb = srcColor.msb;
                break;
            case 3: // Half-transparency
                // If background is not transparent, blend half of original graphic and half of background
                // Otherwise, draw original graphic as is
                if (dstColor.msb) {
                    dstColor.r = (srcColor.r + dstColor.r) >> 1u;
                    dstColor.g = (srcColor.g + dstColor.g) >> 1u;
                    dstColor.b = (srcColor.b + dstColor.b) >> 1u;
                } else {
                    dstColor = srcColor;
                }
                break;
            }

            if (transparentMeshes && pixelParams.mode.meshEnable) {
                util::WriteBE<uint16>(&m_VDP1RenderState.meshFB[altFB][fbIndex][fbOffset], dstColor.u16);
            } else {
                util::WriteBE<uint16>(pixel, dstColor.u16);
                if constexpr (transparentMeshes) {
                    util::WriteBE<uint16>(&m_VDP1RenderState.meshFB[altFB][fbIndex][fbOffset], 0);
                }
            }
        }
    }
    return true;
}

template <bool antiAlias, bool deinterlace, bool transparentMeshes>
FORCE_INLINE bool VDP::VDP1PlotLine(CoordS32 coord1, CoordS32 coord2, VDP1LineParams &lineParams) {
    if (VDP1IsLineSystemClipped<deinterlace>(coord1, coord2)) {
        return false;
    }

    LineStepper line{coord1, coord2, antiAlias};
    auto &ctx = m_VDP1RenderState;
    const uint32 skipSteps = line.SystemClip(ctx.sysClipH, (ctx.sysClipV << ctx.doubleV) | ctx.doubleV);

    VDP1PixelParams pixelParams{
        .mode = lineParams.mode,
        .color = lineParams.color,
    };
    if (pixelParams.mode.gouraudEnable) {
        pixelParams.gouraud.Setup(line.Length() + 1, lineParams.gouraudLeft, lineParams.gouraudRight);
        pixelParams.gouraud.Skip(skipSteps);
    }

    bool aa = false;
    bool plotted = false;
    for (line.Step(); line.CanStep(); aa = line.Step()) {
        bool plottedPixel = VDP1PlotPixel<deinterlace, transparentMeshes>(line.Coord(), pixelParams);
        if constexpr (antiAlias) {
            if (aa) {
                plottedPixel |= VDP1PlotPixel<deinterlace, transparentMeshes>(line.AACoord(), pixelParams);
            }
        }
        if (plottedPixel) {
            plotted = true;
        } else if (plotted) {
            // No more pixels can be drawn past this point
            break;
        }

        if (pixelParams.mode.gouraudEnable) {
            pixelParams.gouraud.Step();
        }
    }

    return plotted;
}

template <bool deinterlace, bool transparentMeshes>
bool VDP::VDP1PlotTexturedLine(CoordS32 coord1, CoordS32 coord2, VDP1TexturedLineParams &lineParams) {
    if (VDP1IsLineSystemClipped<deinterlace>(coord1, coord2)) {
        return false;
    }

    const VDP1Regs &regs1 = VDP1GetRegs();
    auto &ctx = m_VDP1RenderState;

    const uint32 charSizeH = lineParams.charSizeH;
    const auto mode = lineParams.mode;
    const auto control = lineParams.control;
    if (mode.colorMode == 5) {
        // Force-align character address in 16 bpp RGB mode
        lineParams.charAddr &= ~0xF;
    }

    const uint32 v = lineParams.texVStepper.Value();

    LineStepper line{coord1, coord2, true};
    const uint32 skipSteps = line.SystemClip(ctx.sysClipH, (ctx.sysClipV << ctx.doubleV) | ctx.doubleV);

    VDP1PixelParams pixelParams{
        .mode = mode,
    };
    if (mode.gouraudEnable) {
        assert(lineParams.gouraudLeft != nullptr);
        assert(lineParams.gouraudRight != nullptr);
        pixelParams.gouraud.Setup(line.Length() + 1, lineParams.gouraudLeft->Value(), lineParams.gouraudRight->Value());
        pixelParams.gouraud.Skip(skipSteps);
    }

    sint32 uStart = 0;
    sint32 uEnd = charSizeH - 1;
    if (control.flipH) {
        std::swap(uStart, uEnd);
    }
    const bool useHighSpeedShrink = mode.highSpeedShrink && line.Length() < charSizeH - 1;

    TextureStepper uStepper;
    uStepper.Setup(line.Length() + 1, uStart, uEnd, useHighSpeedShrink, regs1.evenOddCoordSelect);
    uStepper.SkipPixels(skipSteps);

    uint16 color = 0;
    bool transparent = true;
    bool hasEndCode = false;
    int endCodeCount = useHighSpeedShrink ? std::numeric_limits<int>::min() : 0;

    auto readTexel = [&] {
        const uint32 u = uStepper.Value();

        const uint32 charIndex = u + v * charSizeH;

        auto processEndCode = [&](bool endCode) {
            if (endCode && !mode.endCodeDisable) {
                hasEndCode = true;
                ++endCodeCount;
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
            color |= lineParams.colorBank & 0xFFF0;
            break;
        case 1: // 4 bpp, 16 colors, lookup table mode
            color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + (charIndex >> 1));
            color = (color >> ((~u & 1) * 4)) & 0xF;
            processEndCode(color == 0xF);
            transparent = color == 0x0;
            color = VDP1ReadRendererVRAM<uint16>(color * sizeof(uint16) + lineParams.colorBank * 8);
            break;
        case 2: // 8 bpp, 64 colors, bank mode
            color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + charIndex);
            processEndCode(color == 0xFF);
            transparent = color == 0x00;
            color &= 0x3F;
            color |= lineParams.colorBank & 0xFFC0;
            break;
        case 3: // 8 bpp, 128 colors, bank mode
            color = VDP1ReadRendererVRAM<uint8>(lineParams.charAddr + charIndex);
            processEndCode(color == 0xFF);
            transparent = color == 0x00;
            color &= 0x7F;
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
            transparent = !bit::test<15>(color);
            break;
        }
    };

    readTexel();

    bool aa = false;
    bool plotted = false;
    for (line.Step(); line.CanStep(); aa = line.Step()) {
        // Load new texels if U coordinate changed
        while (uStepper.ShouldStepTexel()) {
            uStepper.StepTexel();
            readTexel();

            if (endCodeCount == 2) {
                break;
            }
        }
        if (endCodeCount == 2) {
            break;
        }
        uStepper.StepPixel();

        if (hasEndCode || (transparent && !mode.transparentPixelDisable)) {
            // Check if the transparent pixel is in-bounds
            if (!VDP1IsPixelClipped<deinterlace>(line.Coord(), mode.userClippingEnable, mode.clippingMode)) {
                plotted = true;
                continue;
            }
            if (aa && !VDP1IsPixelClipped<deinterlace>(line.Coord(), mode.userClippingEnable, mode.clippingMode)) {
                plotted = true;
                continue;
            }

            // At this point the pixel is clipped. Bail out if there have been in-bounds pixels before, as no more
            // pixels can be drawn past this point.
            if (plotted) {
                break;
            }

            // Otherwise, continue to the next pixel
            continue;
        }

        pixelParams.color = color;

        bool plottedPixel = VDP1PlotPixel<deinterlace, transparentMeshes>(line.Coord(), pixelParams);
        if (aa) {
            plottedPixel |= VDP1PlotPixel<deinterlace, transparentMeshes>(line.AACoord(), pixelParams);
        }
        if (plottedPixel) {
            plotted = true;
        } else if (plotted) {
            // No more pixels can be drawn past this point
            break;
        }

        if (mode.gouraudEnable) {
            pixelParams.gouraud.Step();
        }
    }

    if (endCodeCount == 2 && !plotted) {
        // Check that the line is indeed entirely out of bounds.
        // End codes cut the line short, so if it happens to cut the line before it managed to plot a pixel in-bounds,
        // the optimization could interrupt rendering the rest of the quad.
        for (; line.CanStep(); aa = line.Step()) {
            if (!VDP1IsPixelClipped<deinterlace>(line.Coord(), mode.userClippingEnable, mode.clippingMode)) {
                plotted = true;
                break;
            }
            if (aa && !VDP1IsPixelClipped<deinterlace>(line.Coord(), mode.userClippingEnable, mode.clippingMode)) {
                plotted = true;
                break;
            }
        }
    }

    return plotted;
}

template <bool deinterlace, bool transparentMeshes>
FORCE_INLINE void VDP::VDP1PlotTexturedQuad(uint32 cmdAddress, VDP1Command::Control control, VDP1Command::Size size,
                                            CoordS32 coordA, CoordS32 coordB, CoordS32 coordC, CoordS32 coordD) {
    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
        return;
    }

    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};
    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const uint32 charAddr = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x08) * 8u;

    const uint32 charSizeH = size.H * 8;
    const uint32 charSizeV = size.V;

    devlog::trace<grp::vdp1_cmd>("Textured quad parameters: color={:04X} mode={:04X} size={:2d}x{:<2d} char={:05X}",
                                 color, mode.u16, charSizeH, charSizeV, charAddr);

    VDP1TexturedLineParams lineParams{
        .control = control,
        .mode = mode,
        .colorBank = color,
        .charAddr = charAddr,
        .charSizeH = charSizeH,
        .charSizeV = charSizeV,
    };

    const bool flipV = control.flipV;
    QuadStepper quad{coordA, coordB, coordC, coordD};

    if (mode.gouraudEnable) {
        const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

        Color555 colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)};
        Color555 colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)};
        Color555 colorC{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)};
        Color555 colorD{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)};

        devlog::trace<grp::vdp1_cmd>("[{:05X}] Gouraud colors: ({},{},{}) ({},{},{}) ({},{},{}) ({},{},{})",
                                     gouraudTable, (uint8)colorA.r, (uint8)colorA.g, (uint8)colorA.b, (uint8)colorB.r,
                                     (uint8)colorB.g, (uint8)colorB.b, (uint8)colorC.r, (uint8)colorC.g,
                                     (uint8)colorC.b, (uint8)colorD.r, (uint8)colorD.g, (uint8)colorD.b);

        quad.SetupGouraud(colorA, colorB, colorC, colorD);
        lineParams.gouraudLeft = &quad.LeftEdge().Gouraud();
        lineParams.gouraudRight = &quad.RightEdge().Gouraud();
    }

    quad.SetupTexture(lineParams.texVStepper, charSizeV, flipV);

    // Optimization for the case where the quad goes outside the system clipping area.
    // Skip rendering the rest of the quad when a line is clipped after plotting at least one line.
    // The first few lines of the quad could also be clipped; that is accounted for by requiring at least one
    // plotted line. The point is to skip the calculations once the quad iterator reaches a point where no more lines
    // can be plotted because they all sit outside the system clip area.
    bool plottedLine = false;

    // Interpolate linearly over edges A-D and B-C
    for (; quad.CanStep(); quad.Step()) {
        // Plot lines between the interpolated points
        const CoordS32 coordL = quad.LeftEdge().Coord();
        const CoordS32 coordR = quad.RightEdge().Coord();
        while (lineParams.texVStepper.ShouldStepTexel()) {
            lineParams.texVStepper.StepTexel();
        }
        lineParams.texVStepper.StepPixel();
        if (VDP1PlotTexturedLine<deinterlace, transparentMeshes>(coordL, coordR, lineParams)) {
            plottedLine = true;
        } else if (plottedLine) {
            // No more lines can be drawn past this point
            break;
        }
    }
}

FORCE_INLINE uint64 VDP::VDP1CalcCommandTiming(uint32 cmdAddress, VDP1Command::Control control) {
    uint64 cycles = 0;

    const sint32 doubleV = m_VDP1RenderState.doubleV;

    // HACK: rough cost estimates

    auto lineTiming = [&](CoordS32 coordA, CoordS32 coordB) -> uint32 {
        const uint32 width = abs(coordB.x() - coordA.x());
        const uint32 height = abs(coordB.y() - coordA.y());
        return std::max(width, height);
    };

    auto quadTiming = [&](CoordS32 coordA, CoordS32 coordB, CoordS32 coordC, CoordS32 coordD) -> uint32 {
        uint32 quadCycles = 0;
        QuadStepper quad{coordA, coordB, coordC, coordD};
        for (; quad.CanStep(); quad.Step()) {
            const CoordS32 coordL = quad.LeftEdge().Coord();
            const CoordS32 coordR = quad.RightEdge().Coord();
            quadCycles += lineTiming(coordL, coordR);
        }
        return quadCycles;
    };

    auto simpleQuadTiming = [&](uint32 width, uint32 height) -> uint32 {
        return (width * height) << static_cast<uint32>(doubleV);
    };

    using enum VDP1Command::CommandType;

    switch (control.command) {
    case DrawNormalSprite: //
    {
        const VDP1Command::Size size{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0A)};
        const uint32 charSizeH = size.H * 8;
        const uint32 charSizeV = size.V;
        cycles += simpleQuadTiming(std::max(charSizeH, 1u), std::max(charSizeV, 1u));
        break;
    }

    case DrawScaledSprite: //
    {
        uint32 width;
        const uint8 zoomPointH = bit::extract<0, 1>(control.zoomPoint);
        if (zoomPointH == 0) {
            const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
            const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));
            width = abs(xc - xa);
        } else {
            const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10));
            width = abs(xb);
        }

        uint32 height;
        const uint8 zoomPointV = bit::extract<2, 3>(control.zoomPoint);
        if (zoomPointV == 0) {
            const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
            const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));
            height = abs(yc - ya);
        } else {
            const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12));
            height = abs(yb);
        }

        cycles += simpleQuadTiming(width, height);
        break;
    }
    case DrawDistortedSprite: [[fallthrough]];
    case DrawDistortedSpriteAlt: [[fallthrough]];
    case DrawPolygon: //
    {
        const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
        const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
        const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10));
        const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12));
        const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));
        const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));
        const sint32 xd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x18));
        const sint32 yd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1A));

        const CoordS32 coordA{xa, ya << doubleV};
        const CoordS32 coordB{xb, yb << doubleV};
        const CoordS32 coordC{xc, yc << doubleV};
        const CoordS32 coordD{xd, yd << doubleV};

        cycles += quadTiming(coordA, coordB, coordC, coordD);
        break;
    }

    case DrawPolylines: [[fallthrough]];
    case DrawPolylinesAlt: //
    {
        const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
        const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
        const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10));
        const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12));
        const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));
        const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));
        const sint32 xd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x18));
        const sint32 yd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1A));

        const CoordS32 coordA{xa, ya << doubleV};
        const CoordS32 coordB{xb, yb << doubleV};
        const CoordS32 coordC{xc, yc << doubleV};
        const CoordS32 coordD{xd, yd << doubleV};

        cycles += lineTiming(coordA, coordB);
        cycles += lineTiming(coordB, coordC);
        cycles += lineTiming(coordC, coordD);
        cycles += lineTiming(coordD, coordA);
        break;
    }
    case DrawLine: //
    {
        const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
        const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
        const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10));
        const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12));

        const CoordS32 coordA{xa, ya << doubleV};
        const CoordS32 coordB{xb, yb << doubleV};

        cycles += lineTiming(coordA, coordB);
        break;
    }

    default: break;
    }

    return cycles;
}

template <bool deinterlace, bool transparentMeshes>
FORCE_INLINE void VDP::VDP1Cmd_Handle(uint32 cmdAddress, VDP1Command::Control control) {
    using enum VDP1Command::CommandType;

    switch (control.command) {
    case DrawNormalSprite: VDP1Cmd_DrawNormalSprite<deinterlace, transparentMeshes>(cmdAddress, control); break;
    case DrawScaledSprite: VDP1Cmd_DrawScaledSprite<deinterlace, transparentMeshes>(cmdAddress, control); break;
    case DrawDistortedSprite: [[fallthrough]];
    case DrawDistortedSpriteAlt:
        VDP1Cmd_DrawDistortedSprite<deinterlace, transparentMeshes>(cmdAddress, control);
        break;

    case DrawPolygon: VDP1Cmd_DrawPolygon<deinterlace, transparentMeshes>(cmdAddress, control); break;
    case DrawPolylines: [[fallthrough]];
    case DrawPolylinesAlt: VDP1Cmd_DrawPolylines<deinterlace, transparentMeshes>(cmdAddress, control); break;
    case DrawLine: VDP1Cmd_DrawLine<deinterlace, transparentMeshes>(cmdAddress, control); break;

    case UserClipping: [[fallthrough]];
    case UserClippingAlt: VDP1Cmd_SetUserClipping(cmdAddress); break;
    case SystemClipping: VDP1Cmd_SetSystemClipping(cmdAddress); break;
    case SetLocalCoordinates: VDP1Cmd_SetLocalCoordinates(cmdAddress); break;
    }
}

template <bool deinterlace, bool transparentMeshes>
void VDP::VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const VDP1Command::Size size{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0A)};
    const uint32 charSizeH = size.H * 8;
    const uint32 charSizeV = size.V;

    auto &ctx = m_VDP1RenderState;
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;

    const sint32 lx = xa;                                // left X
    const sint32 ty = ya;                                // top Y
    const sint32 rx = xa + std::max(charSizeH, 1u) - 1u; // right X
    const sint32 by = ya + std::max(charSizeV, 1u) - 1u; // bottom Y

    const sint32 doubleV = ctx.doubleV;

    const CoordS32 coordA{lx, ty << doubleV};
    const CoordS32 coordB{rx, ty << doubleV};
    const CoordS32 coordC{rx, by << doubleV};
    const CoordS32 coordD{lx, by << doubleV};

    devlog::trace<grp::vdp1_cmd>("[{:05X}] Draw normal sprite: {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d}",
                                 cmdAddress, lx, ty, rx, ty, rx, by, lx, by);

    VDP1PlotTexturedQuad<deinterlace, transparentMeshes>(cmdAddress, control, size, coordA, coordB, coordC, coordD);
}

template <bool deinterlace, bool transparentMeshes>
void VDP::VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const VDP1Command::Size size{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0A)};

    auto &ctx = m_VDP1RenderState;
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));

    // Calculated quad coordinates
    sint32 qxa = xa;
    sint32 qya = ya;
    sint32 qxb = xa;
    sint32 qyb = ya;
    sint32 qxc = xa;
    sint32 qyc = ya;
    sint32 qxd = xa;
    sint32 qyd = ya;

    const uint8 zoomPointH = bit::extract<0, 1>(control.zoomPoint);
    const uint8 zoomPointV = bit::extract<2, 3>(control.zoomPoint);

    if (zoomPointH == 0) {
        const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));

        qxb = xc;
        qxc = xc;
    } else {
        const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10));

        switch (zoomPointH) {
        case 1:
            qxb += xb;
            qxc += xb;
            break;
        case 2:
            qxa -= xb >> 1;
            qxb += (xb + 1) >> 1;
            qxc += (xb + 1) >> 1;
            qxd -= xb >> 1;
            break;
        case 3:
            qxa -= xb;
            qxd -= xb;
            break;
        }
    }

    if (zoomPointV == 0) {
        const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));

        qyc = yc;
        qyd = yc;
    } else {
        const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12));

        switch (zoomPointV) {
        case 1:
            qyc += yb;
            qyd += yb;
            break;
        case 2:
            qya -= yb >> 1;
            qyb -= yb >> 1;
            qyc += (yb + 1) >> 1;
            qyd += (yb + 1) >> 1;
            break;
        case 3:
            qya -= yb;
            qyb -= yb;
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

    const sint32 doubleV = ctx.doubleV;

    const CoordS32 coordA{qxa, qya << doubleV};
    const CoordS32 coordB{qxb, qyb << doubleV};
    const CoordS32 coordC{qxc, qyc << doubleV};
    const CoordS32 coordD{qxd, qyd << doubleV};

    devlog::trace<grp::vdp1_cmd>("[{:05X}] Draw scaled sprite: {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d}",
                                 cmdAddress, qxa, qya, qxb, qyb, qxc, qyc, qxd, qyd);

    VDP1PlotTexturedQuad<deinterlace, transparentMeshes>(cmdAddress, control, size, coordA, coordB, coordC, coordD);
}

template <bool deinterlace, bool transparentMeshes>
void VDP::VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const VDP1Command::Size size{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0A)};

    auto &ctx = m_VDP1RenderState;
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const sint32 xc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14)) + ctx.localCoordX;
    const sint32 yc = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16)) + ctx.localCoordY;
    const sint32 xd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x18)) + ctx.localCoordX;
    const sint32 yd = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1A)) + ctx.localCoordY;

    const sint32 doubleV = ctx.doubleV;
    const CoordS32 coordA{xa, ya << doubleV};
    const CoordS32 coordB{xb, yb << doubleV};
    const CoordS32 coordC{xc, yc << doubleV};
    const CoordS32 coordD{xd, yd << doubleV};

    devlog::trace<grp::vdp1_cmd>("[{:05X}] Draw distorted sprite: {:6d}x{:<6d} {:6d}x{:<6d} {:6d}x{:<6d} {:6d}x{:<6d}",
                                 cmdAddress, xa, ya, xb, yb, xc, yc, xd, yd);

    VDP1PlotTexturedQuad<deinterlace, transparentMeshes>(cmdAddress, control, size, coordA, coordB, coordC, coordD);
}

template <bool deinterlace, bool transparentMeshes>
void VDP::VDP1Cmd_DrawPolygon(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    auto &ctx = m_VDP1RenderState;
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

    const sint32 doubleV = ctx.doubleV;
    const CoordS32 coordA{xa, ya << doubleV};
    const CoordS32 coordB{xb, yb << doubleV};
    const CoordS32 coordC{xc, yc << doubleV};
    const CoordS32 coordD{xd, yd << doubleV};

    devlog::trace<grp::vdp1_cmd>("[{:05X}] Draw polygon: {:6d}x{:<6d} {:6d}x{:<6d} {:6d}x{:<6d} {:6d}x{:<6d}, color "
                                 "{:04X}, gouraud table {:05X}, CMDPMOD = {:04X}",
                                 cmdAddress, xa, ya, xb, yb, xc, yc, xd, yd, color, gouraudTable, mode.u16);

    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
        return;
    }

    VDP1LineParams lineParams{
        .mode = mode,
        .color = color,
    };

    QuadStepper quad{coordA, coordB, coordC, coordD};

    if (mode.gouraudEnable) {
        Color555 colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)};
        Color555 colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)};
        Color555 colorC{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)};
        Color555 colorD{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)};

        devlog::trace<grp::vdp1_cmd>("Gouraud colors: ({},{},{}) ({},{},{}) ({},{},{}) ({},{},{})", (uint8)colorA.r,
                                     (uint8)colorA.g, (uint8)colorA.b, (uint8)colorB.r, (uint8)colorB.g,
                                     (uint8)colorB.b, (uint8)colorC.r, (uint8)colorC.g, (uint8)colorC.b,
                                     (uint8)colorD.r, (uint8)colorD.g, (uint8)colorD.b);

        quad.SetupGouraud(colorA, colorB, colorC, colorD);
    }

    // Optimization for the case where the quad goes outside the system clipping area.
    // Skip rendering the rest of the quad when a line is clipped after plotting at least one line.
    // The first few lines of the quad could also be clipped; that is accounted for by requiring at least one
    // plotted line. The point is to skip the calculations once the quad iterator reaches a point where no more lines
    // can be plotted because they all sit outside the system clip area.
    bool plottedLine = false;

    // Interpolate linearly over edges A-D and B-C
    for (; quad.CanStep(); quad.Step()) {
        const CoordS32 coordL = quad.LeftEdge().Coord();
        const CoordS32 coordR = quad.RightEdge().Coord();

        // Plot lines between the interpolated points
        if (mode.gouraudEnable) {
            lineParams.gouraudLeft = quad.LeftEdge().GouraudValue();
            lineParams.gouraudRight = quad.RightEdge().GouraudValue();
        }
        if (VDP1PlotLine<true, deinterlace, transparentMeshes>(coordL, coordR, lineParams)) {
            plottedLine = true;
        } else if (plottedLine) {
            // No more lines can be drawn past this point
            break;
        }
    }
}

template <bool deinterlace, bool transparentMeshes>
void VDP::VDP1Cmd_DrawPolylines(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    auto &ctx = m_VDP1RenderState;
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

    const sint32 doubleV = ctx.doubleV;
    const CoordS32 coordA{xa, ya << doubleV};
    const CoordS32 coordB{xb, yb << doubleV};
    const CoordS32 coordC{xc, yc << doubleV};
    const CoordS32 coordD{xd, yd << doubleV};

    devlog::trace<grp::vdp1_cmd>(
        "[{:05X}] Draw polylines: {}x{} - {}x{} - {}x{} - {}x{}, color {:04X}, gouraud table {:05X}, CMDPMOD = {:04X}",
        cmdAddress, xa, ya, xb, yb, xc, yc, xd, yd, color, gouraudTable >> 3u, mode.u16);

    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
        return;
    }

    VDP1LineParams lineParams{
        .mode = mode,
        .color = color,
    };

    const Color555 A{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)};
    const Color555 B{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)};
    const Color555 C{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 4u)};
    const Color555 D{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 6u)};
    devlog::trace<grp::vdp1_cmd>("Gouraud colors: ({},{},{}) ({},{},{}) ({},{},{}) ({},{},{})", (uint8)A.r, (uint8)A.g,
                                 (uint8)A.b, (uint8)B.r, (uint8)B.g, (uint8)B.b, (uint8)C.r, (uint8)C.g, (uint8)C.b,
                                 (uint8)D.r, (uint8)D.g, (uint8)D.b);

    if (mode.gouraudEnable) {
        lineParams.gouraudLeft = A;
        lineParams.gouraudRight = B;
    }
    VDP1PlotLine<false, deinterlace, transparentMeshes>(coordA, coordB, lineParams);
    if (mode.gouraudEnable) {
        lineParams.gouraudLeft = B;
        lineParams.gouraudRight = C;
    }
    VDP1PlotLine<false, deinterlace, transparentMeshes>(coordB, coordC, lineParams);
    if (mode.gouraudEnable) {
        lineParams.gouraudLeft = C;
        lineParams.gouraudRight = D;
    }
    VDP1PlotLine<false, deinterlace, transparentMeshes>(coordC, coordD, lineParams);
    if (mode.gouraudEnable) {
        lineParams.gouraudLeft = D;
        lineParams.gouraudRight = A;
    }
    VDP1PlotLine<false, deinterlace, transparentMeshes>(coordD, coordA, lineParams);
}

template <bool deinterlace, bool transparentMeshes>
void VDP::VDP1Cmd_DrawLine(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    auto &ctx = m_VDP1RenderState;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};

    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const sint32 doubleV = ctx.doubleV;
    const CoordS32 coordA{xa, ya << doubleV};
    const CoordS32 coordB{xb, yb << doubleV};

    devlog::trace<grp::vdp1_cmd>(
        "[{:05X}] Draw line: {}x{} - {}x{}, color {:04X}, gouraud table {:05X}, CMDPMOD = {:04X}", cmdAddress, xa, ya,
        xb, yb, color, gouraudTable, mode.u16);

    if (VDP1IsLineSystemClipped<deinterlace>(coordA, coordB)) {
        return;
    }

    VDP1LineParams lineParams{
        .mode = mode,
        .color = color,
    };

    if (mode.gouraudEnable) {
        const Color555 colorA{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 0u)};
        const Color555 colorB{.u16 = VDP1ReadRendererVRAM<uint16>(gouraudTable + 2u)};

        lineParams.gouraudLeft = colorA;
        lineParams.gouraudRight = colorB;

        devlog::trace<grp::vdp1_cmd>("Gouraud colors: ({},{},{}) ({},{},{})", (uint8)colorA.r, (uint8)colorA.g,
                                     (uint8)colorA.b, (uint8)colorB.r, (uint8)colorB.g, (uint8)colorB.b);
    }

    VDP1PlotLine<false, deinterlace, transparentMeshes>(coordA, coordB, lineParams);
}

void VDP::VDP1Cmd_SetSystemClipping(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderState;
    ctx.sysClipH = bit::extract<0, 9>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));
    ctx.sysClipV = bit::extract<0, 8>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));
    devlog::trace<grp::vdp1_cmd>("[{:05X}] Set system clipping: {}x{}", cmdAddress, ctx.sysClipH, ctx.sysClipV);
}

void VDP::VDP1Cmd_SetUserClipping(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderState;
    ctx.userClipX0 = bit::extract<0, 9>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
    ctx.userClipY0 = bit::extract<0, 8>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
    ctx.userClipX1 = bit::extract<0, 9>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));
    ctx.userClipY1 = bit::extract<0, 8>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));
    devlog::trace<grp::vdp1_cmd>("[{:05X}] Set user clipping: {}x{} - {}x{}", cmdAddress, ctx.userClipX0,
                                 ctx.userClipY0, ctx.userClipX1, ctx.userClipY1);
}

void VDP::VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderState;
    ctx.localCoordX = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
    ctx.localCoordY = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
    devlog::trace<grp::vdp1_cmd>("[{:05X}] Set local coordinates: {}x{}", cmdAddress, ctx.localCoordX, ctx.localCoordY);
}

// -----------------------------------------------------------------------------
// VDP2

FORCE_INLINE VDP2Regs &VDP::VDP2GetRegs() {
    if (m_threadedVDP2Rendering) {
        return m_vdp2RenderingContext.vdp2.regs;
    } else {
        return m_state.regs2;
    }
}

FORCE_INLINE const VDP2Regs &VDP::VDP2GetRegs() const {
    if (m_threadedVDP2Rendering) {
        return m_vdp2RenderingContext.vdp2.regs;
    } else {
        return m_state.regs2;
    }
}

FORCE_INLINE std::array<uint8, kVDP2VRAMSize> &VDP::VDP2GetVRAM() {
    if (m_threadedVDP2Rendering) {
        return m_vdp2RenderingContext.vdp2.VRAM;
    } else {
        return m_state.VRAM2;
    }
}

void VDP::VDP2InitFrame() {
    const VDP2Regs &regs2 = VDP2GetRegs();
    if (!regs2.bgEnabled[5]) {
        VDP2InitNormalBG<0>();
    }
    VDP2InitNormalBG<1>();
    VDP2InitNormalBG<2>();
    VDP2InitNormalBG<3>();
}

template <uint32 index>
FORCE_INLINE void VDP::VDP2InitNormalBG() {
    static_assert(index < 4, "Invalid NBG index");

    const VDP2Regs &regs2 = VDP2GetRegs();
    const BGParams &bgParams = regs2.bgParams[index + 1];
    NormBGLayerState &bgState = m_normBGLayerStates[index];
    bgState.fracScrollX = 0;
    bgState.fracScrollY = 0;
    bgState.scrollAmountV = bgParams.scrollAmountV;
    if (!m_deinterlaceRender && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && regs2.TVSTAT.ODD) {
        bgState.fracScrollY += bgParams.scrollIncV;
    }

    bgState.scrollIncH = bgParams.scrollIncH;
    bgState.mosaicCounterY = 0;
    if constexpr (index < 2) {
        bgState.lineScrollTableAddress = bgParams.lineScrollTableAddress;
    }
}

FORCE_INLINE void VDP::VDP2UpdateRotationPageBaseAddresses(VDP2Regs &regs2) {
    for (int index = 0; index < 2; index++) {
        if (!regs2.bgEnabled[index + 4]) {
            continue;
        }

        BGParams &bgParams = regs2.bgParams[index];
        if (!bgParams.rbgPageBaseAddressesDirty) {
            continue;
        }
        bgParams.rbgPageBaseAddressesDirty = false;

        const bool cellSizeShift = bgParams.cellSizeShift;
        const bool twoWordChar = bgParams.twoWordChar;

        for (int param = 0; param < 2; param++) {
            const RotationParams &rotParam = regs2.rotParams[param];
            auto &pageBaseAddresses = m_rotParamStates[param].pageBaseAddresses;
            const uint16 plsz = rotParam.plsz;
            for (int plane = 0; plane < 16; plane++) {
                const uint32 mapIndex = rotParam.mapIndices[plane];
                pageBaseAddresses[index][plane] = CalcPageBaseAddress(cellSizeShift, twoWordChar, plsz, mapIndex);
            }
        }
    }
}

void VDP::VDP2UpdateEnabledBGs() {
    const VDP2Regs &regs2 = VDP2GetRegs();

    // Sprite layer is always enabled, unless forcibly disabled
    m_layerEnabled[0] = m_layerRendered[0];

    if (regs2.bgEnabled[4] && regs2.bgEnabled[5]) {
        m_layerEnabled[1] = m_layerRendered[1]; // RBG0
        m_layerEnabled[2] = m_layerRendered[2]; // RBG1
        m_layerEnabled[3] = false;              // EXBG
        m_layerEnabled[4] = false;              // not used
        m_layerEnabled[5] = false;              // not used
    } else {
        // Certain color format settings on NBG0 and NBG1 restrict which BG layers can be enabled
        // - NBG1 is disabled when NBG0 uses 8:8:8 RGB
        // - NBG2 is disabled when NBG0 uses 2048 color palette or any RGB format
        // - NBG3 is disabled when NBG0 uses 8:8:8 RGB or NBG1 uses 2048 color palette or 5:5:5 RGB color format
        // Additionally, NBG0 and RBG1 are mutually exclusive. If RBG1 is enabled, it takes place of NBG0.
        const ColorFormat colorFormatNBG0 = regs2.bgParams[1].colorFormat;
        const ColorFormat colorFormatNBG1 = regs2.bgParams[2].colorFormat;
        const bool disableNBG1 = colorFormatNBG0 == ColorFormat::RGB888;
        const bool disableNBG2 = colorFormatNBG0 == ColorFormat::Palette2048 ||
                                 colorFormatNBG0 == ColorFormat::RGB555 || colorFormatNBG0 == ColorFormat::RGB888;
        const bool disableNBG3 = colorFormatNBG0 == ColorFormat::RGB888 ||
                                 colorFormatNBG1 == ColorFormat::Palette2048 || colorFormatNBG1 == ColorFormat::RGB555;

        m_layerEnabled[1] = m_layerRendered[1] && regs2.bgEnabled[4];                         // RBG0
        m_layerEnabled[2] = m_layerRendered[2] && (regs2.bgEnabled[0] || regs2.bgEnabled[5]); // NBG0/RBG1
        m_layerEnabled[3] = m_layerRendered[3] && regs2.bgEnabled[1] && !disableNBG1;         // NBG1/EXBG
        m_layerEnabled[4] = m_layerRendered[4] && regs2.bgEnabled[2] && !disableNBG2;         // NBG2
        m_layerEnabled[5] = m_layerRendered[5] && regs2.bgEnabled[3] && !disableNBG3;         // NBG3
    }
}

FORCE_INLINE void VDP::VDP2UpdateLineScreenScrollParams(uint32 y) {
    const VDP2Regs &regs2 = VDP2GetRegs();

    for (uint32 i = 0; i < 2; ++i) {
        const BGParams &bgParams = regs2.bgParams[i + 1];
        NormBGLayerState &bgState = m_normBGLayerStates[i];
        VDP2UpdateLineScreenScroll(y, bgParams, bgState);
    }
}

FORCE_INLINE void VDP::VDP2UpdateLineScreenScroll(uint32 y, const BGParams &bgParams, NormBGLayerState &bgState) {
    if ((y & ((1u << bgParams.lineScrollInterval) - 1)) != 0) {
        return;
    }

    uint32 address = bgState.lineScrollTableAddress;
    auto read = [&] {
        const uint32 value = VDP2ReadRendererVRAM<uint32>(address);
        address += sizeof(uint32);
        return value;
    };

    const VDP2Regs &regs = VDP2GetRegs();
    size_t count = 1;
    if (regs.TVMD.LSMDn == InterlaceMode::DoubleDensity && (y > 0 || (!m_deinterlaceRender && regs.TVSTAT.ODD))) {
        ++count;
    }
    for (size_t i = 0; i < count; ++i) {
        if (bgParams.lineScrollXEnable) {
            bgState.fracScrollX = bit::extract<8, 26>(read());
        }
        if (bgParams.lineScrollYEnable) {
            bgState.fracScrollY = bit::extract<8, 26>(read());
        }
        if (bgParams.lineZoomEnable) {
            bgState.scrollIncH = bit::extract<8, 18>(read());
        }
    }
    bgState.lineScrollTableAddress = address;
}

FORCE_INLINE void VDP::VDP2CalcRotationParameterTables(uint32 y) {
    VDP1Regs &regs1 = VDP1GetRegs();
    VDP2Regs &regs2 = VDP2GetRegs();

    const uint32 baseAddress = regs2.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
    const bool readAll = y == 0;
    const auto &vram2 = VDP2GetVRAM();

    for (int i = 0; i < 2; i++) {
        RotationParams &params = regs2.rotParams[i];
        RotationParamState &state = m_rotParamStates[i];

        const bool readXst = readAll || params.readXst;
        const bool readYst = readAll || params.readYst;
        const bool readKAst = readAll || params.readKAst;

        // Tables are located at the base address 0x80 bytes apart
        RotationParamTable t{};
        const uint32 address = baseAddress + i * 0x80;
        t.ReadFrom(&vram2[address & 0x7FFFF]);

        // Calculate parameters

        if (readXst) {
            state.Xst = t.Xst;
            params.readXst = false;
        } else {
            state.Xst += t.deltaXst;
        }
        if (readYst) {
            state.Yst = t.Yst;
            params.readYst = false;
        } else {
            state.Yst += t.deltaYst;
        }
        if (readKAst) {
            state.KA = params.coeffTableAddressOffset + t.KAst;
            params.readKAst = false;
        } else {
            state.KA += t.dKAst;
        }

        // Transformed starting screen coordinates
        // 10*(10-10) + 10*(10-10) + 10*(10-10) = 20 frac bits
        // 14*(23-24) + 14*(23-24) + 14*(23-24) = 38 total bits
        // reduce to 10 frac bits
        const sint32 Xsp = (static_cast<sint64>(t.A) * (state.Xst - (t.Px << 10)) +
                            static_cast<sint64>(t.B) * (state.Yst - (t.Py << 10)) +
                            static_cast<sint64>(t.C) * (t.Zst - (t.Pz << 10))) >>
                           10;
        const sint32 Ysp = (static_cast<sint64>(t.D) * (state.Xst - (t.Px << 10)) +
                            static_cast<sint64>(t.E) * (state.Yst - (t.Py << 10)) +
                            static_cast<sint64>(t.F) * (t.Zst - (t.Pz << 10))) >>
                           10;

        // Transformed view coordinates
        // 10*(0-0) + 10*(0-0) + 10*(0-0) + 10 + 10 = 10+10+10 + 10+10 = 10 frac bits
        // 14*(14-14) + 14*(14-14) + 14*(14-14) + 24 + 24 = 28+28+28 + 24+24 = 28 total bits
        /***/ sint32 Xp = (t.A * (t.Px - t.Cx) + t.B * (t.Py - t.Cy) + t.C * (t.Pz - t.Cz)) + (t.Cx << 10) + t.Mx;
        const sint32 Yp = (t.D * (t.Px - t.Cx) + t.E * (t.Py - t.Cy) + t.F * (t.Pz - t.Cz)) + (t.Cy << 10) + t.My;

        // Screen coordinate increments per Hcnt
        // 10*10 + 10*10 = 20 + 20 = 20 frac bits
        // 14*13 + 14*13 = 27 + 27 = 27 total bits
        // reduce to 10 frac bits
        const sint32 scrXIncH = (t.A * t.deltaX + t.B * t.deltaY) >> 10;
        const sint32 scrYIncH = (t.D * t.deltaX + t.E * t.deltaY) >> 10;

        // Scaling factors (8.16)
        sint64 kx = t.kx;
        sint64 ky = t.ky;

        // Current screen coordinates (18.10) and coefficient address (16.10)
        sint32 scrX = Xsp;
        sint32 scrY = Ysp;
        uint32 KA = state.KA;

        // Current sprite coordinates (13.10)
        sint32 sprX;
        sint32 sprY;
        if (regs1.fbRotEnable) {
            sprX = t.Xst + y * t.deltaXst;
            sprY = t.Yst + y * t.deltaYst;
        }

        const bool doubleResH = regs2.TVMD.HRESOn & 0b010;
        const uint32 xShift = doubleResH ? 1 : 0;
        const uint32 maxX = m_HRes >> xShift;

        // Use per-dot coefficient if reading from CRAM or if any of the VRAM banks was designated as coefficient data
        const bool perDotCoeff = regs2.vramControl.perDotRotationCoeffs;

        // Precompute line color data parameters
        const LineBackScreenParams &lineParams = regs2.lineScreenParams;
        const uint32 line = lineParams.perLine ? y : 0;
        const uint32 lineColorAddress = lineParams.baseAddress + line * sizeof(uint16);
        const uint32 baseLineColorData = bit::extract<7, 10>(VDP2ReadRendererVRAM<uint16>(lineColorAddress)) << 7;

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
                case ViewpointX: Xp = coeff.value << 2; break;
                }

                // Compute line colors
                if (params.coeffUseLineColorData) {
                    const uint32 cramAddress = baseLineColorData | coeff.lineColorData;
                    state.lineColor[x] = VDP2ReadRendererColor5to8(cramAddress * sizeof(uint16));
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
            // (16*10) + 10 = 26 + 10
            // reduce 26 to 10 frac bits
            // remove frac bits from result
            state.screenCoords[x].x() = (((kx * scrX) >> 16) + Xp) >> 10;
            state.screenCoords[x].y() = (((ky * scrY) >> 16) + Yp) >> 10;

            // Increment screen coordinates and coefficient table address by Hcnt
            scrX += scrXIncH;
            scrY += scrYIncH;

            if (regs1.fbRotEnable) {
                // Store sprite coordinates
                state.spriteCoords[x].x() = sprX >> 10ll;
                state.spriteCoords[x].y() = sprY >> 10ll;

                // Increment sprite coordinates by Hcnt
                sprX += t.deltaX;
                sprY += t.deltaY;
            }
        }
    }
}

template <bool deinterlace, bool altField>
FORCE_INLINE void VDP::VDP2CalcWindows(uint32 y) {
    const VDP2Regs &regs = VDP2GetRegs();

    y = VDP2GetY<deinterlace>(y) ^ altField;

    // Calculate window for NBGs and RBGs
    for (int i = 0; i < 5; i++) {
        auto &bgParams = regs.bgParams[i];
        auto &bgWindow = m_bgWindows[altField][i];

        VDP2CalcWindow<altField>(y, bgParams.windowSet, regs.windowParams, std::span{bgWindow}.first(m_HRes));
    }

    // Calculate window for rotation parameters
    VDP2CalcWindow<altField>(y, regs.commonRotParams.windowSet, regs.windowParams,
                             std::span{m_rotParamsWindow[altField]}.first(m_HRes));

    // Calculate window for color calculations
    VDP2CalcWindow<altField>(y, regs.colorCalcParams.windowSet, regs.windowParams,
                             std::span{m_colorCalcWindow[altField]}.first(m_HRes));
}

template <bool altField, bool hasSpriteWindow>
FORCE_INLINE void VDP::VDP2CalcWindow(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                                      const std::array<WindowParams, 2> &windowParams, std::span<bool> windowState) {
    // If no windows are enabled, consider the pixel outside of windows
    if (!std::any_of(windowSet.enabled.begin(), windowSet.enabled.end(), std::identity{})) {
        std::fill(windowState.begin(), windowState.end(), false);
        return;
    }

    if (windowSet.logic == WindowLogic::And) {
        VDP2CalcWindowLogic<altField, false>(y, windowSet, windowParams, windowState);
    } else {
        VDP2CalcWindowLogic<altField, true>(y, windowSet, windowParams, windowState);
    }
}

template <bool altField, bool logicOR, bool hasSpriteWindow>
FORCE_INLINE void VDP::VDP2CalcWindowLogic(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                                           const std::array<WindowParams, 2> &windowParams,
                                           std::span<bool> windowState) {
    // Initialize to all inside if using AND logic or all outside if using OR logic
    std::fill(windowState.begin(), windowState.end(), !logicOR);

    const VDP2Regs &regs2 = VDP2GetRegs();
    const uint16 doubleV = regs2.TVMD.LSMDn == InterlaceMode::SingleDensity;

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
        // state  inverted  result   st!=inv
        // false  false     outside  false
        // true   false     inside   true
        // false  true      inside   true
        // true   true      outside  false
        //
        // Short-circuiting rules for lines outside the vertical window range:
        // # logic  inverted  outcome
        // 1   AND  false     fill with outside
        // 2   AND  true      skip - window has no effect on this line
        // 3    OR  false     skip - window has no effect on this line
        // 4    OR  true      fill with inside

        const auto sy = static_cast<sint16>(y);
        const auto startY = static_cast<sint16>(windowParam.startY) << doubleV;
        const auto endY = static_cast<sint16>(windowParam.endY) << doubleV;
        if (sy < startY || sy > endY) {
            if (logicOR == inverted) {
                // Cases 1 and 4
                std::fill(windowState.begin(), windowState.end(), logicOR);
                return;
            } else {
                // Cases 2 and 3
                continue;
            }
        }

        sint16 startX = windowParam.startX;
        sint16 endX = windowParam.endX;

        // Read line window if enabled
        if (windowParam.lineWindowTableEnable) {
            const uint32 address = windowParam.lineWindowTableAddress + y * sizeof(uint16) * 2;
            startX = VDP2ReadRendererVRAM<uint16>(address + 0);
            endX = VDP2ReadRendererVRAM<uint16>(address + 2);
        }

        // Some games set out-of-range window parameters and expect them to work.
        // It seems like window coordinates should be signed...
        //
        // Panzer Dragoon 2 Zwei:
        //   0000 to FFFE -> empty window
        //   FFFE to 02C0 -> full line
        //
        // Panzer Dragoon Saga:
        //   0000 to FFFF -> empty window
        //
        // Snatcher:
        //   FFFC to 0286 -> full line
        //
        // Handle these cases here
        if (startX < 0) {
            startX = 0;
        }
        if (endX < 0) {
            if (startX >= endX) {
                startX = 0x3FF;
            }
            endX = 0;
        }

        // For normal screen modes, X coordinates don't use bit 0
        if (VDP2GetRegs().TVMD.HRESOn < 2) {
            startX >>= 1;
            endX >>= 1;
        }

        // Fill in horizontal coordinate
        if (inverted != logicOR) {
            // - fill [startX..endX] with outside if using AND logic and inverted
            // - fill [startX..endX] with inside if using OR logic and not inverted
            if (startX < windowState.size()) {
                endX = std::min<sint16>(endX, windowState.size() - 1);
                if (startX <= endX) {
                    std::fill(windowState.begin() + startX, windowState.begin() + endX + 1, logicOR);
                }
            }
        } else {
            // Fill complement of [startX..endX] with outside if using AND logic or inside if using OR logic
            startX = std::min<sint16>(startX, windowState.size());
            std::fill_n(windowState.begin(), startX, logicOR);
            if (endX < windowState.size()) {
                std::fill(windowState.begin() + endX + 1, windowState.end(), logicOR);
            }
        }
    }

    // Check sprite window
    if constexpr (hasSpriteWindow) {
        if (windowSet.enabled[2]) {
            const bool inverted = windowSet.inverted[2];
            for (uint32 x = 0; x < m_HRes; x++) {
                if constexpr (logicOR) {
                    windowState[x] |= m_spriteLayerAttrs[altField].shadowOrWindow[x] != inverted;
                } else {
                    windowState[x] &= m_spriteLayerAttrs[altField].shadowOrWindow[x] != inverted;
                }
            }
        }
    }
}

FORCE_INLINE void VDP::VDP2CalcAccessPatterns(VDP2Regs &regs2) {
    if (!regs2.accessPatternsDirty) [[likely]] {
        return;
    }
    regs2.accessPatternsDirty = false;

    // Some games set up illegal access patterns that cause NBG2/NBG3 character pattern reads to be delayed, shifting
    // all graphics on those backgrounds one tile to the right.
    const bool hires = (regs2.TVMD.HRESOn & 6) != 0;

    // Clear bitmap delay flags
    for (uint32 bgIndex = 0; bgIndex < 4; ++bgIndex) {
        regs2.bgParams[bgIndex + 1].vramDataOffset.fill(0);
    }

    // Build access pattern masks for NBG0-3 PNs and CPs.
    // Bits 0-7 correspond to T0-T7.
    std::array<uint8, 4> pn = {0, 0, 0, 0}; // pattern name access masks
    std::array<uint8, 4> cp = {0, 0, 0, 0}; // character pattern access masks

    // First CP access timing slot per NBG. 0xFF means no accesses found.
    std::array<uint8, 4> firstCPAccessTiming = {0xFF, 0xFF, 0xFF, 0xFF};

    // First CP access VRAM chip per NBG. 0xFF means no accesses found.
    std::array<uint8, 4> firstCPAccessVRAMIndex = {0xFF, 0xFF, 0xFF, 0xFF};

    // First CP access found per NBG per bank.
    std::array<std::array<bool, 4>, 4> firstCPAccessFound = {{
        {false, false, false, false},
        {false, false, false, false},
        {false, false, false, false},
        {false, false, false, false},
    }};

    for (uint8 i = 0; i < 8; ++i) {
        for (uint8 bankIndex = 0; bankIndex < regs2.cyclePatterns.timings.size(); ++bankIndex) {
            const auto &bank = regs2.cyclePatterns.timings[bankIndex];
            if (bankIndex == 1 && !regs2.vramControl.partitionVRAMA) {
                continue;
            }
            if (bankIndex == 3 && !regs2.vramControl.partitionVRAMB) {
                continue;
            }

            const auto timing = bank[i];
            switch (timing) {
            case CyclePatterns::PatNameNBG0: [[fallthrough]];
            case CyclePatterns::PatNameNBG1: [[fallthrough]];
            case CyclePatterns::PatNameNBG2: [[fallthrough]];
            case CyclePatterns::PatNameNBG3: //
            {
                const uint8 bgIndex = static_cast<uint8>(timing) - static_cast<uint8>(CyclePatterns::PatNameNBG0);
                pn[bgIndex] |= 1u << i;
                break;
            }

            case CyclePatterns::CharPatNBG0: [[fallthrough]];
            case CyclePatterns::CharPatNBG1: [[fallthrough]];
            case CyclePatterns::CharPatNBG2: [[fallthrough]];
            case CyclePatterns::CharPatNBG3: //
            {
                const uint8 bgIndex = static_cast<uint8>(timing) - static_cast<uint8>(CyclePatterns::CharPatNBG0);
                cp[bgIndex] |= 1u << i;

                // TODO: find the correct rules for bitmap accesses
                //
                // Test cases:
                //
                // clang-format off
                //  # Res  ZM  Color  Bnk  CP mapping    Delay?  Game screen
                //  1 hi   1x  pal256  A   CP0 01..      no      Capcom Generation - Dai-5-shuu Kakutouka-tachi, art screens
                //                     B   CP0 ..23      yes     Capcom Generation - Dai-5-shuu Kakutouka-tachi, art screens
                //  2 hi   1x  pal256  B0  CP1 01..      no      3D Baseball, in-game (team nameplates during intro)
                //                     B1  CP1 ..23      no      3D Baseball, in-game (team nameplates during intro)
                //  3 hi   1x  pal256  A   CP0 01..      no      Doukyuusei - if, title screen
                //                     B   CP1 ..23      no      Doukyuusei - if, title screen
                //  4 hi   1x  pal256  A0  CP0 01..      no      Duke Nukem 3D, Netlink pages
                //                     A1  CP0 01..      no      Duke Nukem 3D, Netlink pages
                //                     B0  CP0 01..      no      Duke Nukem 3D, Netlink pages
                //                     B1  CP0 01..      no      Duke Nukem 3D, Netlink pages
                //  5 hi   1x  pal256  A   CP0 0123      no      Baroque Report, art screens
                //                     B   CP0 0123      no      Baroque Report, art screens
                //  6 hi   1x  pal256  A0  CP0 0123      no      Sonic Jam, art gallery
                //                     A1  CP0 0123      no      Sonic Jam, art gallery
                //                     B0  CP0 0123      no      Sonic Jam, art gallery
                //                     B1  CP0 0123      no      Sonic Jam, art gallery
                //  7 hi   1x  rgb555  A   CP0 0123      no      Steam Heart's, title screen
                //                     B   CP0 0123      no      Steam Heart's, title screen
                //  8 lo   1x  pal16       CP? 0123....  no      Groove on Fight, scrolling background in Options screen
                //  9 lo   1x  pal256      CP? 01......  no      Mr. Bones, in-game graphics
                // 10 lo   1x  pal256      CP? 01......  no      DoDonPachi, title screen background
                // 11 lo   1x  pal256      CP? 01......  no      Jung Rhythm, title screen
                // 12 lo   1x  pal256      CP? 01......  no      The Need for Speed, menus
                // 13 lo   1x  pal256      CP? ..23....  no      The Legend of Oasis, in-game HUD
                // 14 lo   1x  rgb555      CP? 0123....  no      Jung Rhythm, title screen
                // 15 lo   1x  rgb888      CP? 01234567  no      Street Fighter Zero 3, Capcom logo FMV
                // clang-format on
                //
                // Seems like the "delay" is caused by configuring out-of-phase reads for an NBG in different banks.
                // In case #1, CP0 is assigned to T0-T1 on bank A and T2-T3 on bank B. This is out of phase and on
                // different VRAM chips, so bank B reads are delayed.
                // In case #2, CP1 is assigned to T0-T1 on bank B0 and T2-T3 on bank B1. Despite being out of phase,
                // they're accessed on the same VRAM chip, so there is no delay.
                // In case #3 we have the same display settings but CP0 gets two cycles and CP1 gets two cycles.
                // These cause no "delay" because they're different NBGs.
                // Case #4 has no delay because all reads for the same NBG are assigned to the same cycle slot.
                // Cases #5 and #6 include more reads than necessary for the NBG, but because they all start on the same
                // slot, no delay occurs.

                // FIXME: bitmap delay seems to only apply to hi-res modes
                auto &bgParams = regs2.bgParams[bgIndex + 1];
                if (!bgParams.bitmap || hires) {
                    const uint8 vramIndex = bankIndex >> 1u;
                    if (firstCPAccessTiming[bgIndex] == 0xFF) {
                        firstCPAccessTiming[bgIndex] = i;
                        firstCPAccessVRAMIndex[bgIndex] = vramIndex;
                    } else if (!firstCPAccessFound[bgIndex][bankIndex] && i > firstCPAccessTiming[bgIndex] &&
                               vramIndex != firstCPAccessVRAMIndex[bgIndex]) {
                        bgParams.vramDataOffset[bankIndex] = 8u;
                    }
                    firstCPAccessFound[bgIndex][bankIndex] = true;
                }
                break;
            }

            default: break;
            }
        }

        // Stop at T3 if in hi-res mode
        if (hires && i == 3) {
            break;
        }
    }

    // Apply delays to the NBGs
    for (uint32 i = 0; i < 4; ++i) {
        auto &bgParams = regs2.bgParams[i + 1];
        bgParams.charPatDelay = false;
        const uint8 bgCP = cp[i];
        const uint8 bgPN = pn[i];

        // Skip bitmap NBGs as they're handled above
        if (bgParams.bitmap) {
            continue;
        }

        // Skip NBGs without any assigned accesses
        if (bgPN == 0 || bgCP == 0) {
            continue;
        }

        // Skip NBG0 and NBG1 if the pattern name access happens on T0
        if (i < 2 && bit::test<0>(bgPN)) {
            continue;
        }

        // Apply the delay
        if (bgPN == 0) {
            bgParams.charPatDelay = true;
        } else if (hires) {
            // Valid character pattern access masks per timing for high resolution modes
            static constexpr uint8 kPatterns[2][4] = {
                // 1x1 character patterns
                // T0      T1      T2      T3
                {0b0111, 0b1110, 0b1101, 0b1011},

                // 2x2 character patterns
                // T0      T1      T2      T3
                {0b0111, 0b1110, 0b1100, 0b1000},
            };

            for (uint8 pnIndex = 0; pnIndex < 4; ++pnIndex) {
                // Delay happens when either:
                // - CP access happens entirely before PN access
                // - CP access occurs in illegal time slot
                if ((bgPN & (1u << pnIndex)) != 0 &&
                    (bgCP < bgPN || (bgCP & kPatterns[bgParams.cellSizeShift][pnIndex]) != bgCP)) {
                    bgParams.charPatDelay = true;
                    break;
                }
            }
        } else {
            // Valid character pattern access masks per timing for normal resolution modes
            static constexpr uint8 kPatterns[8] = {
                //   T0          T1          T2          T3          T4          T5          T6          T7
                0b11110111, 0b11101111, 0b11001111, 0b10001111, 0b00001111, 0b00001110, 0b00001100, 0b00001000,
            };

            for (uint8 pnIndex = 0; pnIndex < 8; ++pnIndex) {
                if ((bgPN & (1u << pnIndex)) != 0) {
                    bgParams.charPatDelay = (bgCP & kPatterns[pnIndex]) == 0;
                    break;
                }
            }
        }
    }

    // Translate VRAM access cycles and rotation data bank selectors into read "permissions" for pattern name tables and
    // character pattern tables in each VRAM bank.
    const bool rbg0Enabled = regs2.bgEnabled[4];
    const bool rbg1Enabled = regs2.bgEnabled[5];

    for (uint32 bank = 0; bank < 4; ++bank) {
        const RotDataBankSel rotDataBankSel = regs2.vramControl.GetRotDataBankSel(bank);

        // RBG0
        if (rbg0Enabled && (!rbg1Enabled || bank < 2)) {
            regs2.bgParams[0].patNameAccess[bank] = rotDataBankSel == RotDataBankSel::PatternName;
            regs2.bgParams[0].charPatAccess[bank] = rotDataBankSel == RotDataBankSel::Character;
        } else {
            regs2.bgParams[0].patNameAccess[bank] = false;
            regs2.bgParams[0].charPatAccess[bank] = false;
        }

        // RBG1
        if (rbg1Enabled) {
            regs2.bgParams[1].patNameAccess[bank] = bank == 3;
            regs2.bgParams[1].charPatAccess[bank] = bank == 2;
        } else {
            regs2.bgParams[1].patNameAccess[bank] = false;
            regs2.bgParams[1].charPatAccess[bank] = false;
        }

        // NBG0-3
        for (uint32 nbg = 0; nbg < 4; ++nbg) {
            auto &bgParams = regs2.bgParams[nbg + 1];
            bgParams.patNameAccess[bank] = false;
            bgParams.charPatAccess[bank] = false;

            // Skip disabled NBGs
            if (!regs2.bgEnabled[nbg]) {
                continue;
            }
            // Skip NBGs 2 and 3 if RBG1 is enabled
            if (rbg1Enabled && bank >= 2u) {
                continue;
            }
            // Skip NBGs if RBG0 is enabled and the current bank is assigned to it
            if (rbg0Enabled && rotDataBankSel != RotDataBankSel::Unused) {
                continue;
            }

            // Determine how many character pattern accesses are needed for this NBG

            // Start with a base count of 1
            uint8 expectedCount = 1;

            // Apply ZMCTL modifiers
            // FIXME: Applying these disables background graphics in Baku Baku Animal - World Zookeeper
            /*if ((nbg == 0 && regs2.ZMCTL.N0ZMQT) || (nbg == 1 && regs2.ZMCTL.N1ZMQT)) {
                expectedCount *= 4;
            } else if ((nbg == 0 && regs2.ZMCTL.N0ZMHF) || (nbg == 1 && regs2.ZMCTL.N1ZMHF)) {
                expectedCount *= 2;
            }*/

            // Apply color format modifiers
            switch (bgParams.colorFormat) {
            case ColorFormat::Palette16: break;
            case ColorFormat::Palette256: expectedCount *= 2; break;
            case ColorFormat::Palette2048: expectedCount *= 4; break;
            case ColorFormat::RGB555: expectedCount *= 4; break;
            case ColorFormat::RGB888: expectedCount *= 8; break;
            }

            // Check for maximum 8 cycles on normal resolution, 4 cycles on high resolution/exclusive monitor modes
            const uint32 max = hires ? 4 : 8;
            if (expectedCount > max) [[unlikely]] {
                continue;
            }

            // Check that the background has the required number of accesses
            const uint8 numCPs = std::popcount(cp[nbg]);
            if (numCPs < expectedCount) {
                continue;
            }
            if constexpr (devlog::trace_enabled<grp::vdp2_regs>) {
                if (numCPs > expectedCount) {
                    devlog::trace<grp::vdp2_regs>("NBG{} has more CP accesses than needed ({} > {})", nbg, numCPs,
                                                  expectedCount);
                }
            }

            // Enable pattern name and character pattern accesses for the bank
            for (uint32 index = 0; index < max; ++index) {
                const auto timing = regs2.cyclePatterns.timings[bank][index];
                if (timing == CyclePatterns::PatNameNBG0 + nbg) {
                    bgParams.patNameAccess[bank] = true;
                } else if (timing == CyclePatterns::CharPatNBG0 + nbg
                           // HACK: allow bitmap data access during SH-2 cycles. Probably wrong.
                           // Fixes flickering FMVs in Shin Kaitei Gunkan and Lunar - Silver Star Story
                           || (bgParams.bitmap && timing == CyclePatterns::CPU)) {
                    bgParams.charPatAccess[bank] = true;
                }
            }
        }
    }

    // Combine unpartitioned parameters
    if (!regs2.vramControl.partitionVRAMA) {
        for (uint32 i = 0; i < 5; i++) {
            regs2.bgParams[i].charPatAccess[1] = regs2.bgParams[i].charPatAccess[0];
            regs2.bgParams[i].patNameAccess[1] = regs2.bgParams[i].patNameAccess[0];
            regs2.bgParams[i].vramDataOffset[1] = regs2.bgParams[i].vramDataOffset[0];
        }
    }
    if (!regs2.vramControl.partitionVRAMB) {
        for (uint32 i = 0; i < 5; i++) {
            regs2.bgParams[i].charPatAccess[3] = regs2.bgParams[i].charPatAccess[2];
            regs2.bgParams[i].patNameAccess[3] = regs2.bgParams[i].patNameAccess[2];
            regs2.bgParams[i].vramDataOffset[3] = regs2.bgParams[i].vramDataOffset[2];
        }
    }

    // Translate VRAM access cycles for vertical cell scroll data into increment and offset for NBG0 and NBG1.
    //
    // Some games set up "illegal" access patterns which we have to honor. This is an approximation of the real
    // thing, since this VDP emulator does not actually perform the accesses described by the CYCxn registers.
    //
    // Vertical cell scroll reads are subject to a one-cycle delay if they happen on the following timing slots:
    //   NBG0: T3-T7
    //   NBG1: T4-T7

    m_vertCellScrollInc = 0;
    uint32 vcellAccessOffset = 0;

    // Update cycle accesses
    for (uint32 bank = 0; bank < 4; ++bank) {
        for (uint32 slotIndex = 0; slotIndex < 8; ++slotIndex) {
            const auto access = regs2.cyclePatterns.timings[bank][slotIndex];
            switch (access) {
            case CyclePatterns::VCellScrollNBG0:
                if (regs2.bgParams[1].verticalCellScrollEnable) {
                    m_vertCellScrollInc += sizeof(uint32);
                    m_normBGLayerStates[0].vertCellScrollOffset = vcellAccessOffset;
                    m_normBGLayerStates[0].vertCellScrollDelay = slotIndex >= 3;
                    m_normBGLayerStates[0].vertCellScrollRepeat = slotIndex >= 2;
                    vcellAccessOffset += sizeof(uint32);
                }
                break;
            case CyclePatterns::VCellScrollNBG1:
                if (regs2.bgParams[2].verticalCellScrollEnable) {
                    m_vertCellScrollInc += sizeof(uint32);
                    m_normBGLayerStates[1].vertCellScrollOffset = vcellAccessOffset;
                    m_normBGLayerStates[1].vertCellScrollDelay = slotIndex >= 3;
                    vcellAccessOffset += sizeof(uint32);
                }
                break;
            default: break;
            }
        }
    }
}

FORCE_INLINE void VDP::VDP2PrepareLine(uint32 y) {
    // Don't waste time processing anything if the display is disabled
    // TODO: check if this is how the real VDP2 behaves
    if (!m_displayEnabled) [[unlikely]] {
        return;
    }

    VDP2Regs &regs2 = VDP2GetRegs();

    VDP2CalcAccessPatterns(regs2);
    if (regs2.bgEnabled[4] || regs2.bgEnabled[5]) {
        VDP2CalcRotationParameterTables(y);
    }
    VDP2UpdateRotationPageBaseAddresses(regs2);
    VDP2DrawLineColorAndBackScreens(y);
    VDP2UpdateLineScreenScrollParams(y);

    for (auto &field : m_vramFetchers) {
        for (auto &fetcher : field) {
            fetcher.lastCharIndex = 0xFFFFFFFF;     // force-fetch first character
            fetcher.lastCellX = 0xFF;               // align 2x2 char fetcher
            fetcher.bitmapDataAddress = 0xFFFFFFFF; // force-fetch first bitmap chunk
        }
    }
}

FORCE_INLINE void VDP::VDP2FinishLine(uint32 y) {
    const VDP2Regs &regs2 = VDP2GetRegs();
    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    // Update NBG coordinates
    for (uint32 i = 0; i < 4; ++i) {
        const BGParams &bgParams = regs2.bgParams[i + 1];
        NormBGLayerState &bgState = m_normBGLayerStates[i];
        bgState.fracScrollY += bgParams.scrollIncV;
        // Update the vertical scroll coordinate twice in double-density interlaced mode.
        // If deinterlacing, the second increment is done after rendering the alternate scanline.
        if (doubleDensity) {
            bgState.fracScrollY += bgParams.scrollIncV;
        }

        // Increment mosaic counter
        if (bgParams.mosaicEnable) {
            ++bgState.mosaicCounterY;
            if (bgState.mosaicCounterY >= regs2.mosaicV) {
                bgState.mosaicCounterY = 0;
            }
        }
    }
}

template <bool deinterlace, bool transparentMeshes>
void VDP::VDP2DrawLine(uint32 y, bool altField) {
    devlog::trace<grp::vdp2_render_verbose>("Drawing line {} {} field", y, (altField ? "alt" : "main"));

    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();

    using FnDrawLayer = void (VDP::*)(uint32 y);

    // Lookup table of sprite drawing functions
    // Indexing: [colorMode][rotate][altField]
    static constexpr auto fnDrawSprite = [] {
        std::array<std::array<std::array<FnDrawLayer, 2>, 2>, 4> arr{};

        util::constexpr_for<2 * 2 * 4>([&](auto index) {
            const uint32 cmIndex = bit::extract<0, 1>(index());
            const uint32 rotIndex = bit::extract<2>(index());
            const uint32 altFieldIndex = bit::extract<3>(index());

            const uint32 colorMode = cmIndex <= 2 ? cmIndex : 2;
            const bool rotate = rotIndex;
            const bool altField = altFieldIndex;
            arr[cmIndex][rotate][altFieldIndex] =
                &VDP::VDP2DrawSpriteLayer<colorMode, rotate, altField, transparentMeshes>;
        });

        return arr;
    }();

    const uint32 colorMode = regs2.vramControl.colorRAMMode;
    const bool rotate = regs1.fbRotEnable;
    const bool interlaced = regs2.TVMD.IsInterlaced();

    // Calculate window for sprite layer
    if (altField) {
        VDP2CalcWindow<true>(VDP2GetY<deinterlace>(y) ^ static_cast<uint32>(altField), regs2.spriteParams.windowSet,
                             regs2.windowParams, std::span{m_spriteLayerAttrs[altField].window}.first(m_HRes));
    } else {
        VDP2CalcWindow<false>(VDP2GetY<deinterlace>(y) ^ static_cast<uint32>(altField), regs2.spriteParams.windowSet,
                              regs2.windowParams, std::span{m_spriteLayerAttrs[altField].window}.first(m_HRes));
    }

    // Draw sprite layer
    (this->*fnDrawSprite[colorMode][rotate][altField])(y);

    // Calculate window state for all other layers
    if (altField) {
        VDP2CalcWindows<deinterlace, true>(y);
    } else {
        VDP2CalcWindows<deinterlace, false>(y);
    }

    // Draw background layers
    if (regs2.bgEnabled[4] && regs2.bgEnabled[5]) {
        VDP2DrawRotationBG<0>(y, colorMode, altField); // RBG0
        VDP2DrawRotationBG<1>(y, colorMode, altField); // RBG1
    } else {
        VDP2DrawRotationBG<0>(y, colorMode, altField); // RBG0
        VDP2DrawRotationBG<1>(y, colorMode, altField); // RBG1
        if (interlaced) {
            VDP2DrawNormalBG<0, deinterlace>(y, colorMode, altField); // NBG0
            VDP2DrawNormalBG<1, deinterlace>(y, colorMode, altField); // NBG1
            VDP2DrawNormalBG<2, deinterlace>(y, colorMode, altField); // NBG2
            VDP2DrawNormalBG<3, deinterlace>(y, colorMode, altField); // NBG3
        } else {
            VDP2DrawNormalBG<0, false>(y, colorMode, altField); // NBG0
            VDP2DrawNormalBG<1, false>(y, colorMode, altField); // NBG1
            VDP2DrawNormalBG<2, false>(y, colorMode, altField); // NBG2
            VDP2DrawNormalBG<3, false>(y, colorMode, altField); // NBG3
        }
    }

    // Compose image
    VDP2ComposeLine<deinterlace, transparentMeshes>(y, altField);
}

FORCE_INLINE void VDP::VDP2DrawLineColorAndBackScreens(uint32 y) {
    const VDP2Regs &regs = VDP2GetRegs();

    // Read line color screen color
    const LineBackScreenParams &lineParams = regs.lineScreenParams;
    if (lineParams.perLine || y == 0) {
        const uint32 address = lineParams.baseAddress + y * sizeof(uint16);
        const uint32 cramAddress = VDP2ReadRendererVRAM<uint16>(address) * sizeof(uint16);
        m_lineBackLayerState.lineColor = VDP2ReadRendererColor5to8(cramAddress);
    }

    // Read back screen color
    const LineBackScreenParams &backParams = regs.backScreenParams;
    if (backParams.perLine || y == 0) {
        const uint32 address = backParams.baseAddress + y * sizeof(Color555);
        const Color555 color555{.u16 = VDP2ReadRendererVRAM<uint16>(address)};
        m_lineBackLayerState.backColor = ConvertRGB555to888(color555);
    }
}

template <uint32 colorMode, bool rotate, bool altField, bool transparentMeshes>
NO_INLINE void VDP::VDP2DrawSpriteLayer(uint32 y) {
    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();

    // VDP1 scaling:
    // 2x horz resolution: VDP1 TVM=000 and VDP2 HRESO=01x
    // 1/2x horz readout:  VDP1 TVM=001 and VDP2 HRESO=00x
    const bool doubleResH =
        !regs1.hdtvEnable && !regs1.fbRotEnable && !regs1.pixel8Bits && (regs2.TVMD.HRESOn & 0b110) == 0b010;
    const bool halfResH =
        !regs1.hdtvEnable && !regs1.fbRotEnable && regs1.pixel8Bits && (regs2.TVMD.HRESOn & 0b110) == 0b000;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 xSpriteShift = halfResH ? 1 : 0;
    const uint32 maxX = m_HRes >> xShift;

    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    const SpriteParams &params = regs2.spriteParams;
    auto &layerState = m_layerStates[altField][0];
    auto &layerAttrs = m_spriteLayerAttrs[altField];

    const uint8 fbIndex = VDP1GetDisplayFBIndex();
    const auto &spriteFB = doubleDensity && altField ? m_altSpriteFB[fbIndex] : m_state.spriteFB[fbIndex];

    [[maybe_unused]] auto &meshLayerState = m_meshLayerState[altField];
    [[maybe_unused]] auto &meshLayerAttrs = m_meshLayerAttrs[altField];
    [[maybe_unused]] const auto &meshFB = m_VDP1RenderState.meshFB[altField][fbIndex];

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;

        uint32 spriteFBOffset;
        if constexpr (rotate) {
            const auto &rotParamState = m_rotParamStates[0];
            const auto &coord = rotParamState.spriteCoords[x];
            if (coord.x() < 0 || coord.x() >= regs1.fbSizeH || coord.y() < 0 || coord.y() >= regs1.fbSizeV) {
                layerState.pixels.transparent[xx] = true;
                layerAttrs.shadowOrWindow[xx] = false;
                if (doubleResH) {
                    layerState.pixels.CopyPixel(xx, xx + 1);
                    layerAttrs.CopyAttrs(xx, xx + 1);
                }
                if constexpr (transparentMeshes) {
                    meshLayerState.pixels.transparent[xx] = true;
                    meshLayerAttrs.shadowOrWindow[xx] = false;
                    if (doubleResH) {
                        meshLayerState.pixels.CopyPixel(xx, xx + 1);
                        meshLayerAttrs.CopyAttrs(xx, xx + 1);
                    }
                }
                continue;
            }
            spriteFBOffset = coord.x() + coord.y() * regs1.fbSizeH;
        } else {
            spriteFBOffset = (x << xSpriteShift) + y * regs1.fbSizeH;
        }

        VDP2DrawSpritePixel<colorMode, altField, transparentMeshes, false>(xx, params, spriteFB, spriteFBOffset);
        if (doubleResH) {
            layerState.pixels.CopyPixel(xx, xx + 1);
            layerAttrs.CopyAttrs(xx, xx + 1);
        }

        if constexpr (transparentMeshes) {
            VDP2DrawSpritePixel<colorMode, altField, transparentMeshes, true>(xx, params, meshFB, spriteFBOffset);
            if (doubleResH) {
                meshLayerState.pixels.CopyPixel(xx, xx + 1);
                meshLayerAttrs.CopyAttrs(xx, xx + 1);
            }
        }
    }
}

template <uint32 colorMode, bool altField, bool transparentMeshes, bool applyMesh>
FORCE_INLINE void VDP::VDP2DrawSpritePixel(uint32 x, const SpriteParams &params, const SpriteFB &spriteFB,
                                           uint32 spriteFBOffset) {
    // This implies that if transparentMeshes is false, applyMesh will be always false
    static_assert(transparentMeshes || !applyMesh, "applyMesh cannot be set when transparentMeshes is disabled");

    // When applyMesh is true, the pixel to be drawn is from the transparent mesh layer.
    // In this case, the following changes happen:
    // - Transparent pixels are skipped as they have no effect on the final picture.
    // - Opaque pixels drawn on top of existing pixels on the sprite layer are averaged together.
    // - Opaque pixels drawn on transparent pixels will become translucent and enable the transparentMesh attribute.
    // Transparent mesh pixels are handled separately from the rest of the rendering pipeline.

    auto &layerState = applyMesh ? m_meshLayerState[altField] : m_layerStates[altField][0];
    auto &layerAttrs = applyMesh ? m_meshLayerAttrs[altField] : m_spriteLayerAttrs[altField];

    // NOTE: intentionally using the base sprite layer here as the windows are not computed for the mesh layer
    if (m_spriteLayerAttrs[altField].window[x]) {
        layerState.pixels.transparent[x] = true;
        layerAttrs.shadowOrWindow[x] = false;
        return;
    }

    if (params.mixedFormat) {
        const uint16 spriteDataValue = util::ReadBE<uint16>(&spriteFB[(spriteFBOffset * sizeof(uint16)) & 0x3FFFE]);
        if (bit::test<15>(spriteDataValue)) {
            // RGB data

            // Transparent if:
            // - Using byte-sized sprite types (0x8 to 0xF) and the lower 8 bits are all zero
            // - Using word-sized sprite types that have the shadow/sprite window bit (types 0x2 to 0x7), sprite
            //   window is enabled, and the lower 15 bits are all zero
            if (params.type >= 8) {
                if (bit::extract<0, 7>(spriteDataValue) == 0) {
                    layerState.pixels.transparent[x] = true;
                    layerAttrs.shadowOrWindow[x] = false;
                    return;
                }
            } else if (params.type >= 2) {
                if (params.useSpriteWindow && bit::extract<0, 14>(spriteDataValue) == 0) {
                    layerState.pixels.transparent[x] = true;
                    layerAttrs.shadowOrWindow[x] = false;
                    return;
                }
            }

            layerState.pixels.color[x] = ConvertRGB555to888(Color555{spriteDataValue});
            layerState.pixels.transparent[x] = false;
            layerState.pixels.priority[x] = params.priorities[0];

            layerAttrs.colorCalcRatio[x] = params.colorCalcRatios[0];
            layerAttrs.shadowOrWindow[x] = false;
            layerAttrs.normalShadow[x] = false;
            return;
        }
    }

    // Palette data
    const SpriteData spriteData = VDP2FetchSpriteData<applyMesh>(spriteFB, spriteFBOffset);

    // Handle sprite window
    if (params.useSpriteWindow && params.spriteWindowEnabled &&
        spriteData.shadowOrWindow != params.spriteWindowInverted) {
        layerState.pixels.transparent[x] = true;
        layerAttrs.shadowOrWindow[x] = true;
        return;
    }

    const uint32 colorIndex = params.colorDataOffset + spriteData.colorData;
    const Color888 color = VDP2FetchCRAMColor<colorMode>(0, colorIndex);

    layerState.pixels.color[x] = color;
    layerState.pixels.transparent[x] = spriteData.special == SpriteData::Special::Transparent;
    layerState.pixels.priority[x] = params.priorities[spriteData.priority];

    layerAttrs.colorCalcRatio[x] = params.colorCalcRatios[spriteData.colorCalcRatio];
    layerAttrs.shadowOrWindow[x] = spriteData.shadowOrWindow;
    layerAttrs.normalShadow[x] = spriteData.special == SpriteData::Special::Shadow;
}

template <uint32 bgIndex, bool deinterlace>
FORCE_INLINE void VDP::VDP2DrawNormalBG(uint32 y, uint32 colorMode, bool altField) {
    static_assert(bgIndex < 4, "Invalid NBG index");

    using FnDraw = void (VDP::*)(uint32 y, const BGParams &, LayerState &, const NormBGLayerState &, VRAMFetcher &,
                                 std::span<const bool>, bool);

    // Lookup table of scroll BG drawing functions
    // Indexing: [charMode][fourCellChar][colorFormat][colorMode]
    static constexpr auto fnDrawScroll = [] {
        std::array<std::array<std::array<std::array<FnDraw, 4>, 8>, 2>, 3> arr{};

        util::constexpr_for<3 * 2 * 8 * 4>([&](auto index) {
            const uint32 fcc = bit::extract<0>(index());
            const uint32 cf = bit::extract<1, 3>(index());
            const uint32 clm = bit::extract<4, 5>(index());
            const uint32 chm = bit::extract<6, 7>(index());

            const auto chmEnum = static_cast<CharacterMode>(chm);
            const auto cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = clm <= 2 ? clm : 2;
            arr[chm][fcc][cf][clm] =
                &VDP::VDP2DrawNormalScrollBG<chmEnum, fcc, cfEnum, colorMode, bgIndex <= 1, deinterlace>;
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

            const auto cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = cm <= 2 ? cm : 2;
            arr[cf][cm] = &VDP::VDP2DrawNormalBitmapBG<cfEnum, colorMode, bgIndex <= 1, deinterlace>;
        });

        return arr;
    }();

    if (!m_layerEnabled[bgIndex + 2]) {
        return;
    }

    const VDP2Regs &regs = VDP2GetRegs();

    if constexpr (bgIndex == 0) {
        // NBG0 and RBG1 are mutually exclusive
        if (regs.bgEnabled[5]) {
            return;
        }
    }

    const BGParams &bgParams = regs.bgParams[bgIndex + 1];
    LayerState &layerState = m_layerStates[altField][bgIndex + 2];
    const NormBGLayerState &bgState = m_normBGLayerStates[bgIndex];
    VRAMFetcher &vramFetcher = m_vramFetchers[altField][bgIndex];
    auto windowState = std::span<const bool>{m_bgWindows[altField][bgIndex + 1]}.first(m_HRes);

    const uint32 cf = static_cast<uint32>(bgParams.colorFormat);
    if (bgParams.bitmap) {
        (this->*fnDrawBitmap[cf][colorMode])(y, bgParams, layerState, bgState, vramFetcher, windowState, altField);
    } else {
        const bool twc = bgParams.twoWordChar;
        const bool fcc = bgParams.cellSizeShift;
        const bool exc = bgParams.extChar;
        const uint32 chm = static_cast<uint32>(twc   ? CharacterMode::TwoWord
                                               : exc ? CharacterMode::OneWordExtended
                                                     : CharacterMode::OneWordStandard);
        (this->*fnDrawScroll[chm][fcc][cf][colorMode])(y, bgParams, layerState, bgState, vramFetcher, windowState,
                                                       altField);
    }
}

template <uint32 bgIndex>
FORCE_INLINE void VDP::VDP2DrawRotationBG(uint32 y, uint32 colorMode, bool altField) {
    static_assert(bgIndex < 2, "Invalid RBG index");

    using FnDrawScroll =
        void (VDP::*)(uint32 y, const BGParams &, LayerState &, VRAMFetcher &, std::span<const bool>, bool);
    using FnDrawBitmap = void (VDP::*)(uint32 y, const BGParams &, LayerState &, std::span<const bool>, bool);

    // Lookup table of scroll BG drawing functions
    // Indexing: [charMode][fourCellChar][colorFormat][colorMode]
    static constexpr auto fnDrawScroll = [] {
        std::array<std::array<std::array<std::array<FnDrawScroll, 4>, 8>, 2>, 3> arr{};

        util::constexpr_for<3 * 2 * 8 * 4>([&](auto index) {
            const uint32 fcc = bit::extract<0>(index());
            const uint32 cf = bit::extract<1, 3>(index());
            const uint32 clm = bit::extract<4, 5>(index());
            const uint32 chm = bit::extract<6, 7>(index());

            const auto chmEnum = static_cast<CharacterMode>(chm);
            const auto cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = clm <= 2 ? clm : 2;
            arr[chm][fcc][cf][clm] = &VDP::VDP2DrawRotationScrollBG<bgIndex, chmEnum, fcc, cfEnum, colorMode>;
        });

        return arr;
    }();

    // Lookup table of bitmap BG drawing functions
    // Indexing: [colorFormat][colorMode]
    static constexpr auto fnDrawBitmap = [] {
        std::array<std::array<FnDrawBitmap, 4>, 8> arr{};

        util::constexpr_for<8 * 4>([&](auto index) {
            const uint32 cf = bit::extract<0, 2>(index());
            const uint32 cm = bit::extract<3, 4>(index());

            const auto cfEnum = static_cast<ColorFormat>(cf <= 4 ? cf : 4);
            const uint32 colorMode = cm <= 2 ? cm : 2;
            arr[cf][cm] = &VDP::VDP2DrawRotationBitmapBG<bgIndex, cfEnum, colorMode>;
        });

        return arr;
    }();

    if (!m_layerEnabled[bgIndex + 1]) {
        return;
    }

    const VDP2Regs &regs = VDP2GetRegs();
    const BGParams &bgParams = regs.bgParams[bgIndex];
    LayerState &layerState = m_layerStates[altField][bgIndex + 1];
    VRAMFetcher &vramFetcher = m_vramFetchers[altField][bgIndex + 4];
    auto windowState = std::span<const bool>{m_bgWindows[altField][bgIndex]}.first(m_HRes);

    const uint32 cf = static_cast<uint32>(bgParams.colorFormat);
    if (bgParams.bitmap) {
        (this->*fnDrawBitmap[cf][colorMode])(y, bgParams, layerState, windowState, altField);
    } else {
        const bool twc = bgParams.twoWordChar;
        const bool fcc = bgParams.cellSizeShift;
        const bool exc = bgParams.extChar;
        const uint32 chm = static_cast<uint32>(twc   ? CharacterMode::TwoWord
                                               : exc ? CharacterMode::OneWordExtended
                                                     : CharacterMode::OneWordStandard);
        (this->*fnDrawScroll[chm][fcc][cf][colorMode])(y, bgParams, layerState, vramFetcher, windowState, altField);
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

// Tests if an array of uint8 values are all zeroes
FORCE_INLINE bool AllZeroU8(std::span<const uint8> values) {

#if defined(_M_X64) || defined(__x86_64__)

    #if defined(__AVX__)
    // 32 at a time
    for (; values.size() >= 32; values = values.subspan(32)) {
        const __m256i vec32 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(values.data()));

        // Test if all bits are 0
        if (!_mm256_testz_si256(vec32, vec32)) {
            return false;
        }
    }
    #endif

    #if defined(__SSE2__)
    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        __m128i vec16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(values.data()));

        // Compare to zero
        vec16 = _mm_cmpeq_epi8(vec16, _mm_setzero_si128());

        // Extract MSB all into a 16-bit mask, if any bit is clear, then we have a true value
        if (_mm_movemask_epi8(vec16) != 0xFFFF) {
            return false;
        }
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // 64 at a time
    for (; values.size() >= 64; values = values.subspan(64)) {
        const uint8x16x4_t vec64 = vld1q_u8_x4(reinterpret_cast<const uint8 *>(values.data()));

        // If the largest value is not zero, we have a true value
        if ((vmaxvq_u8(vec64.val[0]) != 0u) || (vmaxvq_u8(vec64.val[1]) != 0u) || (vmaxvq_u8(vec64.val[2]) != 0u) ||
            (vmaxvq_u8(vec64.val[3]) != 0u)) {
            return false;
        }
    }

    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        const uint8x16_t vec16 = vld1q_u8(reinterpret_cast<const uint8 *>(values.data()));

        // If the largest value is not zero, we have a true value
        if (vmaxvq_u8(vec16) != 0u) {
            return false;
        }
    }
#elif defined(__clang__) || defined(__GNUC__)
    // 16 at a time
    for (; values.size() >= sizeof(__int128); values = values.subspan(sizeof(__int128))) {
        const __int128 &vec16 = *reinterpret_cast<const __int128 *>(values.data());

        if (vec16 != __int128(0)) {
            return false;
        }
    }
#endif

    // 8 at a time
    for (; values.size() >= sizeof(uint64); values = values.subspan(sizeof(uint64))) {
        const uint64 &vec8 = *reinterpret_cast<const uint64 *>(values.data());

        if (vec8 != 0ull) {
            return false;
        }
    }

    // 4 at a time
    for (; values.size() >= sizeof(uint32); values = values.subspan(sizeof(uint32))) {
        const uint32 &vec4 = *reinterpret_cast<const uint32 *>(values.data());

        if (vec4 != 0u) {
            return false;
        }
    }

    for (const uint8 &value : values) {
        if (value != 0u) {
            return false;
        }
    }
    return true;
}

// Tests if an array of bool values are all true
FORCE_INLINE bool AllBool(std::span<const bool> values) {

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX__)
    // 32 at a time
    for (; values.size() >= 32; values = values.subspan(32)) {
        __m256i vec32 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(values.data()));

        // Move bit 0 into the MSB
        vec32 = _mm256_slli_epi64(vec32, 7);

        // Extract 32 MSBs into a 32-bit mask, if any bit is zero, then we have a false value
        if (_mm256_movemask_epi8(vec32) != 0xFFFF'FFFF) {
            return false;
        }
    }
    #endif

    #if defined(__SSE2__)
    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        __m128i vec16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(values.data()));

        // Move bit 0 into the MSB
        vec16 = _mm_slli_epi64(vec16, 7);

        // Extract 16 MSBs into a 32-bit mask, if any bit is zero, then we have a false value
        if (_mm_movemask_epi8(vec16) != 0xFFFF) {
            return false;
        }
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // 64 at a time
    for (; values.size() >= 64; values = values.subspan(64)) {
        const uint8x16x4_t vec64 = vld1q_u8_x4(reinterpret_cast<const uint8 *>(values.data()));

        // If the smallest value is zero, then we have a false value
        if ((vminvq_u8(vec64.val[0]) == 0u) || (vminvq_u8(vec64.val[1]) == 0u) || (vminvq_u8(vec64.val[2]) == 0u) ||
            (vminvq_u8(vec64.val[3]) == 0u)) {
            return false;
        }
    }
    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        const uint8x16_t vec16 = vld1q_u8(reinterpret_cast<const uint8 *>(values.data()));

        // If the smallest value is zero, then we have a false value
        if (vminvq_u8(vec16) == 0u) {
            return false;
        }
    }
#elif defined(__clang__) || defined(__GNUC__)
    // 16 at a time
    for (; values.size() >= sizeof(__int128); values = values.subspan(sizeof(__int128))) {
        const __int128 &vec16 = *reinterpret_cast<const __int128 *>(values.data());

        if (vec16 != __int128((__int128(0x01'01'01'01'01'01'01'01) << 64) | 0x01'01'01'01'01'01'01'01)) {
            return false;
        }
    }
#endif

    // 8 at a time
    for (; values.size() >= sizeof(uint64); values = values.subspan(sizeof(uint64))) {
        const uint64 &vec8 = *reinterpret_cast<const uint64 *>(values.data());

        if (vec8 != 0x01'01'01'01'01'01'01'01) {
            return false;
        }
    }

    // 4 at a time
    for (; values.size() >= sizeof(uint32); values = values.subspan(sizeof(uint32))) {
        const uint32 &vec4 = *reinterpret_cast<const uint32 *>(values.data());

        if (vec4 != 0x01'01'01'01) {
            return false;
        }
    }

    for (const bool &value : values) {
        if (!value) {
            return false;
        }
    }
    return true;
}

// Tests if an any element in an array of bools are true
FORCE_INLINE bool AnyBool(std::span<const bool> values) {
#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX__)
    // 32 at a time
    for (; values.size() >= 32; values = values.subspan(32)) {
        __m256i vec32 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(values.data()));

        // Move bit 0 into the MSB
        vec32 = _mm256_slli_epi64(vec32, 7);

        // Extract MSB into a 32-bit mask, if any bit is set, then we have a true value
        if (_mm256_movemask_epi8(vec32) != 0u) {
            return true;
        }
    }
    #endif
    #if defined(__SSE2__)
    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        __m128i vec16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(values.data()));

        // Move bit 0 into the MSB
        vec16 = _mm_slli_epi64(vec16, 7);

        // Extract MSB into a 16-bit mask, if any bit is set, then we have a true value
        if (_mm_movemask_epi8(vec16) != 0u) {
            return true;
        }
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // 64 at a time
    for (; values.size() >= 64; values = values.subspan(64)) {
        const uint8x16x4_t vec64 = vld1q_u8_x4(reinterpret_cast<const uint8 *>(values.data()));

        // If the smallest value is not zero, then we have a true value
        if ((vmaxvq_u8(vec64.val[0]) != 0u) || (vmaxvq_u8(vec64.val[1]) != 0u) || (vmaxvq_u8(vec64.val[2]) != 0u) ||
            (vmaxvq_u8(vec64.val[3]) != 0u)) {
            return true;
        }
    }

    // 16 at a time
    for (; values.size() >= 16; values = values.subspan(16)) {
        const uint8x16_t vec16 = vld1q_u8(reinterpret_cast<const uint8 *>(values.data()));

        // If the smallest value is not zero, then we have a true value
        if (vmaxvq_u8(vec16) != 0u) {
            return true;
        }
    }
#elif defined(__clang__) || defined(__GNUC__)
    // 16 at a time
    for (; values.size() >= sizeof(__int128); values = values.subspan(sizeof(__int128))) {
        const __int128 &vec16 = *reinterpret_cast<const __int128 *>(values.data());

        if (vec16) {
            return true;
        }
    }
#endif

    // 8 at a time
    for (; values.size() >= sizeof(uint64); values = values.subspan(sizeof(uint64))) {
        const uint64 &vec8 = *reinterpret_cast<const uint64 *>(values.data());

        if (vec8) {
            return true;
        }
    }

    // 4 at a time
    for (; values.size() >= sizeof(uint32); values = values.subspan(sizeof(uint32))) {
        const uint32 &vec4 = *reinterpret_cast<const uint32 *>(values.data());

        if (vec4) {
            return true;
        }
    }

    for (const bool &value : values) {
        if (value) {
            return true;
        }
    }
    return false;
}

FORCE_INLINE void Color888ShadowMasked(const std::span<Color888> pixels, const std::span<const bool, kMaxResH> mask) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < pixels.size(); i += 8) {
        // Load eight mask bytes into 32-bit lanes of 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        const __m256i pixel_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&pixels[i]));

        __m256i shadowed_x8 = _mm256_srli_epi32(pixel_x8, 1);
        shadowed_x8 = _mm256_and_si256(shadowed_x8, _mm256_set1_epi8(0x7F));

        // Blend with mask
        const __m256i dstColor_x8 = _mm256_blendv_epi8(pixel_x8, shadowed_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&pixels[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < pixels.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i pixel_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&pixels[i]));

        __m128i shadowed_x4 = _mm_srli_epi64(pixel_x4, 1);

        shadowed_x4 = _mm_and_si128(shadowed_x4, _mm_set1_epi8(0x7F));

        // Blend with mask
        const __m128i dstColor_x4 =
            _mm_or_si128(_mm_and_si128(mask_x4, shadowed_x4), _mm_andnot_si128(mask_x4, pixel_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&pixels[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < pixels.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        const uint32x4_t pixel_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&pixels[i]));
        const uint32x4_t shadowed_x4 = vshrq_n_u8(pixel_x4, 1);

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, shadowed_x4, pixel_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&pixels[i]), dstColor_x4);
    }
#endif

    for (; i < pixels.size(); i++) {
        Color888 &pixel = pixels[i];
        if (mask[i]) {
            pixel.u32 >>= 1;
            pixel.u32 &= 0x7F'7F'7F'7F;
        }
    }
}

FORCE_INLINE void Color888SatAddMasked(const std::span<Color888> dest, const std::span<const bool, kMaxResH> mask,
                                       const std::span<const Color888, kMaxResH> topColors,
                                       const std::span<const Color888, kMaxResH> btmColors) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)

    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < dest.size(); i += 8) {
        // Load eight mask bytes into 32-bit lanes of 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        const __m256i topColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&topColors[i]));
        const __m256i btmColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&btmColors[i]));

        // Pack back into 8-bit values, be sure to truncate to avoid saturation
        __m256i dstColor_x8 = _mm256_adds_epu8(topColor_x8, btmColor_x8);

        // Blend with mask
        dstColor_x8 = _mm256_blendv_epi8(topColor_x8, dstColor_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&dest[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i topColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&topColors[i]));
        const __m128i btmColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&btmColors[i]));

        // Saturated add
        __m128i dstColor_x4 = _mm_adds_epu8(topColor_x4, btmColor_x4);

        // Blend with mask
        dstColor_x4 = _mm_or_si128(_mm_and_si128(mask_x4, dstColor_x4), _mm_andnot_si128(mask_x4, topColor_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&dest[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        const uint32x4_t topColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&topColors[i]));
        const uint32x4_t btmColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&btmColors[i]));

        // Saturated add
        const uint32x4_t add_x4 = vqaddq_u8(topColor_x4, btmColor_x4);

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, add_x4, topColor_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&dest[i]), dstColor_x4);
    }
#endif

    for (; i < dest.size(); i++) {
        const Color888 &topColor = topColors[i];
        const Color888 &btmColor = btmColors[i];
        Color888 &dstColor = dest[i];
        if (mask[i]) {
            dstColor.r = std::min<uint16>(topColor.r + btmColor.r, 255u);
            dstColor.g = std::min<uint16>(topColor.g + btmColor.g, 255u);
            dstColor.b = std::min<uint16>(topColor.b + btmColor.b, 255u);
        } else {
            dstColor = topColor;
        }
    }
}

FORCE_INLINE void Color888SelectMasked(const std::span<Color888> dest, const std::span<const bool, kMaxResH> mask,
                                       const std::span<const Color888> topColors,
                                       const std::span<const Color888, kMaxResH> btmColors) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < dest.size(); i += 8) {
        // Load eight mask bytes into 32-bit lanes of 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        const __m256i topColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&topColors[i]));
        const __m256i btmColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&btmColors[i]));

        // Blend with mask
        const __m256i dstColor_x8 = _mm256_blendv_epi8(topColor_x8, btmColor_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&dest[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i topColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&topColors[i]));
        const __m128i btmColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&btmColors[i]));

        // Blend with mask
        const __m128i dstColor_x4 =
            _mm_or_si128(_mm_and_si128(mask_x4, btmColor_x4), _mm_andnot_si128(mask_x4, topColor_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&dest[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        const uint32x4_t topColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&topColors[i]));
        const uint32x4_t btmColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&btmColors[i]));

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, btmColor_x4, topColor_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&dest[i]), dstColor_x4);
    }
#endif

    for (; i < dest.size(); i++) {
        dest[i] = mask[i] ? btmColors[i] : topColors[i];
    }
}

FORCE_INLINE void Color888AverageMasked(const std::span<Color888> dest, const std::span<const bool, kMaxResH> mask,
                                        const std::span<const Color888> topColors,
                                        const std::span<const Color888, kMaxResH> btmColors) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < dest.size(); i += 8) {
        // Load eight mask bytes into 32-bit lanes of 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        const __m256i topColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&topColors[i]));
        const __m256i btmColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&btmColors[i]));

        const __m256i average_x8 = _mm256_add_epi32(
            _mm256_srli_epi32(_mm256_and_si256(_mm256_xor_si256(topColor_x8, btmColor_x8), _mm256_set1_epi8(0xFE)), 1),
            _mm256_and_si256(topColor_x8, btmColor_x8));

        // Blend with mask
        const __m256i dstColor_x8 = _mm256_blendv_epi8(topColor_x8, average_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&dest[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i topColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&topColors[i]));
        const __m128i btmColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&btmColors[i]));

        const __m128i average_x4 = _mm_add_epi32(
            _mm_srli_epi32(_mm_and_si128(_mm_xor_si128(topColor_x4, btmColor_x4), _mm_set1_epi8(0xFE)), 1),
            _mm_and_si128(topColor_x4, btmColor_x4));

        // Blend with mask
        const __m128i dstColor_x4 =
            _mm_or_si128(_mm_and_si128(mask_x4, average_x4), _mm_andnot_si128(mask_x4, topColor_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&dest[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        const uint32x4_t topColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&topColors[i]));
        const uint32x4_t btmColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&btmColors[i]));

        // Halving average
        const uint32x4_t average_x4 = vhaddq_u8(topColor_x4, btmColor_x4);

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, average_x4, topColor_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&dest[i]), dstColor_x4);
    }
#endif

    for (; i < dest.size(); i++) {
        const Color888 &topColor = topColors[i];
        const Color888 &btmColor = btmColors[i];
        Color888 &dstColor = dest[i];
        if (mask[i]) {
            dstColor = AverageRGB888(topColor, btmColor);
        } else {
            dstColor = topColor;
        }
    }
}

FORCE_INLINE void Color888CompositeRatioPerPixelMasked(const std::span<Color888> dest, const std::span<const bool> mask,
                                                       const std::span<const Color888, kMaxResH> topColors,
                                                       const std::span<const Color888, kMaxResH> btmColors,
                                                       const std::span<const uint8, kMaxResH> ratios) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX2__)
    // Eight pixels at a time
    for (; (i + 8) < dest.size(); i += 8) {
        // Load eightmask values and expand each byte into 32-bit 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        // Load eight ratios and widen each byte into 32-bit lanes
        // Put each byte into a 32-bit lane
        __m256i ratio_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(&ratios[i]));
        // Repeat the byte
        ratio_x8 = _mm256_mullo_epi32(ratio_x8, _mm256_set1_epi32(0x01'01'01'01));

        const __m256i topColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&topColors[i]));
        const __m256i btmColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&btmColors[i]));

        // Expand to 16-bit values
        __m256i ratio16lo_x8 = _mm256_unpacklo_epi8(ratio_x8, _mm256_setzero_si256());
        __m256i ratio16hi_x8 = _mm256_unpackhi_epi8(ratio_x8, _mm256_setzero_si256());

        const __m256i topColor16lo = _mm256_unpacklo_epi8(topColor_x8, _mm256_setzero_si256());
        const __m256i btmColor16lo = _mm256_unpacklo_epi8(btmColor_x8, _mm256_setzero_si256());

        const __m256i topColor16hi = _mm256_unpackhi_epi8(topColor_x8, _mm256_setzero_si256());
        const __m256i btmColor16hi = _mm256_unpackhi_epi8(btmColor_x8, _mm256_setzero_si256());

        // Lerp
        const __m256i dstColor16lo = _mm256_add_epi16(
            btmColor16lo,
            _mm256_srli_epi16(_mm256_mullo_epi16(_mm256_sub_epi16(topColor16lo, btmColor16lo), ratio16lo_x8), 5));
        const __m256i dstColor16hi = _mm256_add_epi16(
            btmColor16hi,
            _mm256_srli_epi16(_mm256_mullo_epi16(_mm256_sub_epi16(topColor16hi, btmColor16hi), ratio16hi_x8), 5));

        // Pack back into 8-bit values, be sure to truncate to avoid saturation
        __m256i dstColor_x8 = _mm256_packus_epi16(_mm256_and_si256(dstColor16lo, _mm256_set1_epi16(0xFF)),
                                                  _mm256_and_si256(dstColor16hi, _mm256_set1_epi16(0xFF)));

        // Blend with mask
        dstColor_x8 = _mm256_blendv_epi8(topColor_x8, dstColor_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&dest[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i topColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&topColors[i]));
        const __m128i btmColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&btmColors[i]));

        // Load four ratios and splat each byte into 32-bit lanes
        __m128i ratio_x4 = _mm_loadu_si32(&ratios[i]);
        ratio_x4 = _mm_unpacklo_epi8(ratio_x4, ratio_x4);
        ratio_x4 = _mm_unpacklo_epi16(ratio_x4, ratio_x4);

        // Expand to 16-bit values
        const __m128i ratio16lo_x4 = _mm_unpacklo_epi8(ratio_x4, _mm_setzero_si128());
        const __m128i ratio16hi_x4 = _mm_unpackhi_epi8(ratio_x4, _mm_setzero_si128());

        const __m128i topColor16lo = _mm_unpacklo_epi8(topColor_x4, _mm_setzero_si128());
        const __m128i btmColor16lo = _mm_unpacklo_epi8(btmColor_x4, _mm_setzero_si128());

        const __m128i topColor16hi = _mm_unpackhi_epi8(topColor_x4, _mm_setzero_si128());
        const __m128i btmColor16hi = _mm_unpackhi_epi8(btmColor_x4, _mm_setzero_si128());

        // Composite
        const __m128i dstColor16lo = _mm_add_epi16(
            btmColor16lo, _mm_srli_epi16(_mm_mullo_epi16(_mm_sub_epi16(topColor16lo, btmColor16lo), ratio16lo_x4), 5));
        const __m128i dstColor16hi = _mm_add_epi16(
            btmColor16hi, _mm_srli_epi16(_mm_mullo_epi16(_mm_sub_epi16(topColor16hi, btmColor16hi), ratio16hi_x4), 5));

        // Pack back into 8-bit values, be sure to truncate to avoid saturation
        __m128i dstColor_x4 = _mm_packus_epi16(_mm_and_si128(dstColor16lo, _mm_set1_epi16(0xFF)),
                                               _mm_and_si128(dstColor16hi, _mm_set1_epi16(0xFF)));

        // Blend with mask
        dstColor_x4 = _mm_or_si128(_mm_and_si128(mask_x4, dstColor_x4), _mm_andnot_si128(mask_x4, topColor_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&dest[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        // Load four ratios and splat each byte into 32-bit lanes
        uint32x4_t ratio_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(ratios.data() + i), vdupq_n_u32(0), 0);
        // 8 -> 16
        ratio_x4 = vzip1q_u8(ratio_x4, ratio_x4);
        // 16 -> 32
        ratio_x4 = vzip1q_u16(ratio_x4, ratio_x4);

        const uint32x4_t topColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&topColors[i]));
        const uint32x4_t btmColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&btmColors[i]));

        const uint16x8_t topColor16lo = vmovl_u8(vget_low_u8(topColor_x4));
        const uint16x8_t btmColor16lo = vmovl_u8(vget_low_u8(btmColor_x4));

        const uint16x8_t topColor16hi = vmovl_high_u8(topColor_x4);
        const uint16x8_t btmColor16hi = vmovl_high_u8(btmColor_x4);

        // Composite
        int16x8_t composite16lo = vsubq_s16(topColor16lo, btmColor16lo);
        int16x8_t composite16hi = vsubq_s16(topColor16hi, btmColor16hi);

        composite16lo = vmulq_u16(composite16lo, vmovl_u8(vget_low_s8(ratio_x4)));
        composite16hi = vmulq_u16(composite16hi, vmovl_high_u8(ratio_x4));

        composite16lo = vsraq_n_s16(vmovl_s8(vget_low_s8(btmColor_x4)), composite16lo, 5);
        composite16hi = vsraq_n_s16(vmovl_high_s8(btmColor_x4), composite16hi, 5);

        int8x16_t composite_x4 = vmovn_high_s16(vmovn_s16(composite16lo), composite16hi);

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, composite_x4, topColor_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&dest[i]), dstColor_x4);
    }
#endif

    for (; i < dest.size(); i++) {
        const Color888 &topColor = topColors[i];
        const Color888 &btmColor = btmColors[i];
        const uint8 &ratio = ratios[i];
        Color888 &dstColor = dest[i];
        if (mask[i]) {
            dstColor.r = btmColor.r + ((int)topColor.r - (int)btmColor.r) * ratio / 32;
            dstColor.g = btmColor.g + ((int)topColor.g - (int)btmColor.g) * ratio / 32;
            dstColor.b = btmColor.b + ((int)topColor.b - (int)btmColor.b) * ratio / 32;
        } else {
            dstColor = topColor;
        }
    }
}

FORCE_INLINE void Color888CompositeRatioMasked(const std::span<Color888> dest, const std::span<const bool> mask,
                                               const std::span<const Color888, kMaxResH> topColors,
                                               const std::span<const Color888, kMaxResH> btmColors, uint8 ratio) {
    size_t i = 0;

#if defined(_M_X64) || defined(__x86_64__)
    #if defined(__AVX2__)
    // Eight pixels at a time
    const __m256i ratio_x8 = _mm256_set1_epi32(0x01'01'01'01 * ratio);
    // Expand to 16-bit values
    const __m256i ratio16lo_x8 = _mm256_unpacklo_epi8(ratio_x8, _mm256_setzero_si256());
    const __m256i ratio16hi_x8 = _mm256_unpackhi_epi8(ratio_x8, _mm256_setzero_si256());
    for (; (i + 8) < dest.size(); i += 8) {
        // Load eight mask values and expand each byte into 32-bit 000... or 111...
        __m256i mask_x8 = _mm256_cvtepu8_epi32(_mm_loadu_si64(mask.data() + i));
        mask_x8 = _mm256_sub_epi32(_mm256_setzero_si256(), mask_x8);

        const __m256i topColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&topColors[i]));
        const __m256i btmColor_x8 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(&btmColors[i]));

        const __m256i topColor16lo = _mm256_unpacklo_epi8(topColor_x8, _mm256_setzero_si256());
        const __m256i btmColor16lo = _mm256_unpacklo_epi8(btmColor_x8, _mm256_setzero_si256());

        const __m256i topColor16hi = _mm256_unpackhi_epi8(topColor_x8, _mm256_setzero_si256());
        const __m256i btmColor16hi = _mm256_unpackhi_epi8(btmColor_x8, _mm256_setzero_si256());

        // Lerp
        const __m256i dstColor16lo = _mm256_add_epi16(
            btmColor16lo,
            _mm256_srli_epi16(_mm256_mullo_epi16(_mm256_sub_epi16(topColor16lo, btmColor16lo), ratio16lo_x8), 5));
        const __m256i dstColor16hi = _mm256_add_epi16(
            btmColor16hi,
            _mm256_srli_epi16(_mm256_mullo_epi16(_mm256_sub_epi16(topColor16hi, btmColor16hi), ratio16hi_x8), 5));

        // Pack back into 8-bit values, be sure to truncate to avoid saturation
        __m256i dstColor_x8 = _mm256_packus_epi16(_mm256_and_si256(dstColor16lo, _mm256_set1_epi16(0xFF)),
                                                  _mm256_and_si256(dstColor16hi, _mm256_set1_epi16(0xFF)));

        // Blend with mask
        dstColor_x8 = _mm256_blendv_epi8(topColor_x8, dstColor_x8, mask_x8);

        // Write
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&dest[i]), dstColor_x8);
    }
    #endif

    #if defined(__SSE2__)
    // Four pixels at a time
    const __m128i ratio_x4 = _mm_set1_epi32(0x01'01'01'01 * ratio);
    // Expand to 16-bit values
    const __m128i ratio16lo_x4 = _mm_unpacklo_epi8(ratio_x4, _mm_setzero_si128());
    const __m128i ratio16hi_x4 = _mm_unpackhi_epi8(ratio_x4, _mm_setzero_si128());
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        __m128i mask_x4 = _mm_loadu_si32(mask.data() + i);
        mask_x4 = _mm_unpacklo_epi8(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_unpacklo_epi16(mask_x4, _mm_setzero_si128());
        mask_x4 = _mm_sub_epi32(_mm_setzero_si128(), mask_x4);

        const __m128i topColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&topColors[i]));
        const __m128i btmColor_x4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&btmColors[i]));

        const __m128i topColor16lo = _mm_unpacklo_epi8(topColor_x4, _mm_setzero_si128());
        const __m128i btmColor16lo = _mm_unpacklo_epi8(btmColor_x4, _mm_setzero_si128());

        const __m128i topColor16hi = _mm_unpackhi_epi8(topColor_x4, _mm_setzero_si128());
        const __m128i btmColor16hi = _mm_unpackhi_epi8(btmColor_x4, _mm_setzero_si128());

        // Composite
        const __m128i dstColor16lo = _mm_add_epi16(
            btmColor16lo, _mm_srli_epi16(_mm_mullo_epi16(_mm_sub_epi16(topColor16lo, btmColor16lo), ratio16lo_x4), 5));
        const __m128i dstColor16hi = _mm_add_epi16(
            btmColor16hi, _mm_srli_epi16(_mm_mullo_epi16(_mm_sub_epi16(topColor16hi, btmColor16hi), ratio16hi_x4), 5));

        // Pack back into 8-bit values, be sure to truncate to avoid saturation
        __m128i dstColor_x4 = _mm_packus_epi16(_mm_and_si128(dstColor16lo, _mm_set1_epi16(0xFF)),
                                               _mm_and_si128(dstColor16hi, _mm_set1_epi16(0xFF)));

        // Blend with mask
        dstColor_x4 = _mm_or_si128(_mm_and_si128(mask_x4, dstColor_x4), _mm_andnot_si128(mask_x4, topColor_x4));

        // Write
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&dest[i]), dstColor_x4);
    }
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    // Four pixels at a time
    const uint8x16_t ratio_x4 = vdupq_n_u8(ratio);
    for (; (i + 4) < dest.size(); i += 4) {
        // Load four mask values and expand each byte into 32-bit 000... or 111...
        uint32x4_t mask_x4 = vld1q_lane_u32(reinterpret_cast<const uint32 *>(mask.data() + i), vdupq_n_u32(0), 0);
        mask_x4 = vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(mask_x4))));
        mask_x4 = vnegq_s32(mask_x4);

        const uint32x4_t topColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&topColors[i]));
        const uint32x4_t btmColor_x4 = vld1q_u32(reinterpret_cast<const uint32 *>(&btmColors[i]));

        const uint16x8_t topColor16lo = vmovl_u8(vget_low_u8(topColor_x4));
        const uint16x8_t btmColor16lo = vmovl_u8(vget_low_u8(btmColor_x4));

        const uint16x8_t topColor16hi = vmovl_high_u8(topColor_x4);
        const uint16x8_t btmColor16hi = vmovl_high_u8(btmColor_x4);

        // Composite
        int16x8_t composite16lo = vsubq_s16(topColor16lo, btmColor16lo);
        int16x8_t composite16hi = vsubq_s16(topColor16hi, btmColor16hi);

        composite16lo = vmulq_u16(composite16lo, vmovl_u8(vget_low_s8(ratio_x4)));
        composite16hi = vmulq_u16(composite16hi, vmovl_high_u8(ratio_x4));

        composite16lo = vsraq_n_s16(vmovl_s8(vget_low_s8(btmColor_x4)), composite16lo, 5);
        composite16hi = vsraq_n_s16(vmovl_high_s8(btmColor_x4), composite16hi, 5);

        int8x16_t composite_x4 = vmovn_high_s16(vmovn_s16(composite16lo), composite16hi);

        // Blend with mask
        const uint32x4_t dstColor_x4 = vbslq_u32(mask_x4, composite_x4, topColor_x4);

        // Write
        vst1q_u32(reinterpret_cast<uint32 *>(&dest[i]), dstColor_x4);
    }
#endif

    for (; i < dest.size(); i++) {
        const Color888 &topColor = topColors[i];
        const Color888 &btmColor = btmColors[i];
        Color888 &dstColor = dest[i];
        if (mask[i]) {
            dstColor.r = btmColor.r + ((int)topColor.r - (int)btmColor.r) * ratio / 32;
            dstColor.g = btmColor.g + ((int)topColor.g - (int)btmColor.g) * ratio / 32;
            dstColor.b = btmColor.b + ((int)topColor.b - (int)btmColor.b) * ratio / 32;
        } else {
            dstColor = topColor;
        }
    }
}

template <bool deinterlace, bool transparentMeshes>
FORCE_INLINE void VDP::VDP2ComposeLine(uint32 y, bool altField) {
    const VDP2Regs &regs = VDP2GetRegs();
    const auto &colorCalcParams = regs.colorCalcParams;

    y = VDP2GetY<deinterlace>(y) ^ static_cast<uint32>(altField);

    if (!m_displayEnabled || !regs.TVMD.DISP) {
        uint32 color = 0xFF000000;
        if (m_borderColorMode) {
            color |= m_lineBackLayerState.backColor.u32;
        }
        std::fill_n(&m_framebuffer[y * m_HRes], m_HRes, color);
        return;
    }

    // NOTE: All arrays here are intentionally left uninitialized for performance.
    // Only the necessary entries are initialized and used.

    // Determine layer orders
    static constexpr std::array<LayerIndex, 3> kLayersInit{LYR_Back, LYR_Back, LYR_Back};
    alignas(16) std::array<std::array<LayerIndex, 3>, kMaxResH> scanline_layers;
    std::fill_n(scanline_layers.begin(), m_HRes, kLayersInit);

    static constexpr std::array<uint8, 3> kLayerPriosInit{0, 0, 0};
    alignas(16) std::array<std::array<uint8, 3>, kMaxResH> scanline_layerPrios;
    std::fill_n(scanline_layerPrios.begin(), m_HRes, kLayerPriosInit);

    for (int layer = 0; layer < m_layerStates[altField].size(); layer++) {
        if (!m_layerEnabled[layer]) {
            continue;
        }

        const LayerState &state = m_layerStates[altField][layer];

        if (AllBool(std::span{state.pixels.transparent}.first(m_HRes))) {
            // All pixels are transparent
            continue;
        }

        if (AllZeroU8(std::span{state.pixels.priority}.first(m_HRes))) {
            // All priorities are zero
            continue;
        }

        for (uint32 x = 0; x < m_HRes; x++) {
            if (state.pixels.transparent[x]) {
                continue;
            }
            const uint8 priority = state.pixels.priority[x];
            if (priority == 0) {
                continue;
            }
            if (layer == LYR_Sprite) {
                if (m_spriteLayerAttrs[altField].normalShadow[x]) {
                    continue;
                }
            }

            // Insert the layer into the appropriate position in the stack
            // - Higher priority beats lower priority
            // - If same priority, lower Layer index beats higher Layer index
            // - layers[0] is topmost (first) layer
            std::array<LayerIndex, 3> &layers = scanline_layers[x];
            std::array<uint8, 3> &layerPrios = scanline_layerPrios[x];
            for (int i = 0; i < 3; i++) {
                if (priority > layerPrios[i] || (priority == layerPrios[i] && layer < layers[i])) {
                    // Push layers back
                    for (int j = 2; j > i; j--) {
                        layers[j] = layers[j - 1];
                        layerPrios[j] = layerPrios[j - 1];
                    }
                    layers[i] = static_cast<LayerIndex>(layer);
                    layerPrios[i] = priority;
                    break;
                }
            }
        }
    }

    // Find the sprite mesh layers
    alignas(16) std::array<uint8, kMaxResH> scanline_meshLayers;
    if constexpr (transparentMeshes) {
        std::fill_n(scanline_meshLayers.begin(), m_HRes, 0xFF);

        if (m_layerEnabled[0] && !AllBool(std::span{m_meshLayerState[altField].pixels.transparent}.first(m_HRes)) &&
            !AllZeroU8(std::span{m_meshLayerState[altField].pixels.priority}.first(m_HRes))) {

            for (uint32 x = 0; x < m_HRes; x++) {
                if (m_meshLayerState[altField].pixels.transparent[x]) {
                    continue;
                }
                const uint8 priority = m_meshLayerState[altField].pixels.priority[x];
                if (priority == 0) {
                    continue;
                }
                if (m_meshLayerAttrs[altField].normalShadow[x]) {
                    continue;
                }

                std::array<uint8, 3> &layerPrios = scanline_layerPrios[x];
                for (int i = 0; i < 3; i++) {
                    // The sprite layer has the highest priority on ties, so the priority check can be simplified.
                    // Sprite pixels drawn of top of mesh pixels erase the corresponding pixels from the mesh layer,
                    // therefore the mesh layer can be considered always on top of the sprite layer.
                    if (priority >= layerPrios[i]) {
                        scanline_meshLayers[x] = i;
                        break;
                    }
                }
            }
        }
    }

    // Retrieves the color of the given layer
    auto getLayerColor = [&](LayerIndex layer, uint32 x) -> Color888 {
        if (layer == LYR_Back) {
            return m_lineBackLayerState.backColor;
        } else {
            return m_layerStates[altField][layer].pixels.color[x];
        }
    };

    // Gather pixels for layer 0
    alignas(16) std::array<Color888, kMaxResH> layer0Pixels;
    for (uint32 x = 0; x < m_HRes; x++) {
        layer0Pixels[x] = getLayerColor(scanline_layers[x][0], x);
    }

    const auto isColorCalcEnabled = [&](LayerIndex layer, uint32 x) {
        if (layer == LYR_Sprite) {
            const SpriteParams &spriteParams = regs.spriteParams;
            if (!spriteParams.colorCalcEnable) {
                return false;
            }

            const uint8 pixelPriority = m_layerStates[altField][LYR_Sprite].pixels.priority[x];

            using enum SpriteColorCalculationCondition;
            switch (spriteParams.colorCalcCond) {
            case PriorityLessThanOrEqual: return pixelPriority <= spriteParams.colorCalcValue;
            case PriorityEqual: return pixelPriority == spriteParams.colorCalcValue;
            case PriorityGreaterThanOrEqual: return pixelPriority >= spriteParams.colorCalcValue;
            case MsbEqualsOne: return m_layerStates[altField][LYR_Sprite].pixels.color[x].msb == 1;
            default: util::unreachable();
            }
        } else if (layer == LYR_Back) {
            return regs.backScreenParams.colorCalcEnable;
        } else {
            return regs.bgParams[layer - LYR_RBG0].colorCalcEnable;
        }
    };

    // Gather layer color calculation data
    alignas(16) std::array<bool, kMaxResH> layer0ColorCalcEnabled;
    alignas(16) std::array<bool, kMaxResH> layer0BlendMeshLayer;

    for (uint32 x = 0; x < m_HRes; x++) {
        const LayerIndex layer = scanline_layers[x][0];
        if constexpr (transparentMeshes) {
            layer0BlendMeshLayer[x] = scanline_meshLayers[x] == 0;
        }
        if (m_colorCalcWindow[altField][x]) {
            layer0ColorCalcEnabled[x] = false;
            continue;
        }
        if (!isColorCalcEnabled(layer, x)) {
            layer0ColorCalcEnabled[x] = false;
            continue;
        }

        switch (layer) {
        case LYR_Back: [[fallthrough]];
        case LYR_Sprite: layer0ColorCalcEnabled[x] = true; break;
        default: layer0ColorCalcEnabled[x] = m_layerStates[altField][layer].pixels.specialColorCalc[x]; break;
        }
    }

    const std::span<Color888> framebufferOutput(reinterpret_cast<Color888 *>(&m_framebuffer[y * m_HRes]), m_HRes);

    if (AnyBool(std::span{layer0ColorCalcEnabled}.first(m_HRes))) {
        // Gather pixels for layer 1
        alignas(16) std::array<Color888, kMaxResH> layer1Pixels;
        alignas(16) std::array<bool, kMaxResH> layer1BlendMeshLayer;
        for (uint32 x = 0; x < m_HRes; x++) {
            layer1Pixels[x] = getLayerColor(scanline_layers[x][1], x);
            if constexpr (transparentMeshes) {
                layer1BlendMeshLayer[x] = scanline_meshLayers[x] == 1;
            }
        }

        // Extended color calculations (only in normal TV modes)
        const bool useExtendedColorCalc = colorCalcParams.extendedColorCalcEnable && regs.TVMD.HRESOn < 2;

        const bool doubleResH = regs.TVMD.HRESOn & 0b010;
        const uint32 xShift = doubleResH ? 1 : 0;

        // Gather line-color data
        alignas(16) std::array<bool, kMaxResH> layer0LineColorEnabled;
        alignas(16) std::array<Color888, kMaxResH> layer0LineColors;
        for (uint32 x = 0; x < m_HRes; x++) {
            const LayerIndex layer = scanline_layers[x][0];

            switch (layer) {
            case LYR_Sprite:
                layer0LineColorEnabled[x] = regs.spriteParams.lineColorScreenEnable;
                if (layer0LineColorEnabled[x]) {
                    layer0LineColors[x] = m_lineBackLayerState.lineColor;
                }
                break;
            case LYR_Back: layer0LineColorEnabled[x] = false; break;
            default:
                layer0LineColorEnabled[x] = regs.bgParams[layer - LYR_RBG0].lineColorScreenEnable;
                if (layer0LineColorEnabled[x]) {
                    if (layer == LYR_RBG0 || (layer == LYR_NBG0_RBG1 && regs.bgEnabled[5])) {
                        layer0LineColors[x] = m_rbgLineColors[layer - LYR_RBG0][x >> xShift];
                    } else {
                        layer0LineColors[x] = m_lineBackLayerState.lineColor;
                    }
                }
                break;
            }
        }

        // Apply extended color calculations to layer 1
        if (useExtendedColorCalc) {
            alignas(16) std::array<bool, kMaxResH> layer1ColorCalcEnabled;
            alignas(16) std::array<Color888, kMaxResH> layer2Pixels;
            alignas(16) std::array<bool, kMaxResH> layer2BlendMeshLayer;

            // Gather pixels for layer 2
            for (uint32 x = 0; x < m_HRes; x++) {
                layer1ColorCalcEnabled[x] = isColorCalcEnabled(scanline_layers[x][1], x);
                if (layer1ColorCalcEnabled[x]) {
                    layer2Pixels[x] = getLayerColor(scanline_layers[x][2], x);
                }
                if constexpr (transparentMeshes) {
                    layer2BlendMeshLayer[x] = scanline_meshLayers[x] == 2;
                }
            }

            // Blend layer 2 with sprite mesh layer colors
            // TODO: apply color calculation effects
            if constexpr (transparentMeshes) {
                Color888AverageMasked(std::span{layer2Pixels}.first(m_HRes), layer2BlendMeshLayer, layer2Pixels,
                                      m_meshLayerState[altField].pixels.color);
            }

            // TODO: honor color RAM mode + palette/RGB format restrictions
            // - modes 1 and 2 don't blend layers if the bottom layer uses palette color
            // HACK: assuming color RAM mode 0 for now (aka no restrictions)
            Color888AverageMasked(std::span{layer1Pixels}.first(m_HRes), layer1ColorCalcEnabled, layer1Pixels,
                                  layer2Pixels);

            if (regs.lineScreenParams.colorCalcEnable) {
                // Blend line color if top layer uses it
                Color888AverageMasked(std::span{layer1Pixels}.first(m_HRes), layer0LineColorEnabled, layer1Pixels,
                                      layer0LineColors);
            } else {
                // Replace with line color if top layer uses it
                Color888SelectMasked(std::span{layer1Pixels}.first(m_HRes), layer0LineColorEnabled, layer1Pixels,
                                     layer0LineColors);
            }
        } else {
            // Replace layer 1 pixels with line color screen where applicable
            for (uint32 x = 0; x < m_HRes; ++x) {
                if (layer0LineColorEnabled[x]) {
                    layer1Pixels[x] = layer0LineColors[x];
                }
            }
        }

        // Blend layer 1 with sprite mesh layer colors
        // TODO: apply color calculation effects
        if constexpr (transparentMeshes) {
            Color888AverageMasked(std::span{layer1Pixels}.first(m_HRes), layer1BlendMeshLayer, layer1Pixels,
                                  m_meshLayerState[altField].pixels.color);
        }

        // Blend layer 0 and layer 1
        if (colorCalcParams.useAdditiveBlend) {
            // Saturated add
            Color888SatAddMasked(framebufferOutput, layer0ColorCalcEnabled, layer0Pixels, layer1Pixels);
        } else {
            // Gather color ratio info
            alignas(16) std::array<uint8, kMaxResH> scanline_ratio;
            for (uint32 x = 0; x < m_HRes; x++) {
                if (!layer0ColorCalcEnabled[x]) {
                    scanline_ratio[x] = 0;
                    continue;
                }

                const LayerIndex layer = scanline_layers[x][colorCalcParams.useSecondScreenRatio];
                switch (layer) {
                case LYR_Sprite: scanline_ratio[x] = m_spriteLayerAttrs[altField].colorCalcRatio[x]; break;
                case LYR_Back:
                    scanline_ratio[x] = layer0LineColorEnabled[x] ? regs.lineScreenParams.colorCalcRatio
                                                                  : regs.backScreenParams.colorCalcRatio;
                    break;
                default: scanline_ratio[x] = regs.bgParams[layer - LYR_RBG0].colorCalcRatio; break;
                }
            }

            // Alpha composite
            Color888CompositeRatioPerPixelMasked(framebufferOutput, layer0ColorCalcEnabled, layer0Pixels, layer1Pixels,
                                                 scanline_ratio);
        }
    } else {
        std::copy_n(layer0Pixels.cbegin(), framebufferOutput.size(), framebufferOutput.begin());
    }

    // Blend layer 0 with sprite mesh layer colors
    // TODO: apply color calculation effects
    if constexpr (transparentMeshes) {
        Color888AverageMasked(framebufferOutput, layer0BlendMeshLayer, framebufferOutput,
                              m_meshLayerState[altField].pixels.color);
    }

    // Gather shadow data
    alignas(16) std::array<bool, kMaxResH> layer0ShadowEnabled;
    for (uint32 x = 0; x < m_HRes; x++) {
        // Sprite layer is beneath top layer
        if (m_layerStates[altField][LYR_Sprite].pixels.priority[x] < scanline_layerPrios[x][0]) {
            layer0ShadowEnabled[x] = false;
            continue;
        }

        // Sprite layer doesn't have shadow
        const bool isNormalShadow = m_spriteLayerAttrs[altField].normalShadow[x];
        const bool isMSBShadow = !regs.spriteParams.useSpriteWindow && m_spriteLayerAttrs[altField].shadowOrWindow[x];
        if (!isNormalShadow && !isMSBShadow) {
            layer0ShadowEnabled[x] = false;
            continue;
        }

        const LayerIndex layer = scanline_layers[x][0];
        switch (layer) {
        case LYR_Sprite: layer0ShadowEnabled[x] = m_spriteLayerAttrs[altField].shadowOrWindow[x]; break;
        case LYR_Back: layer0ShadowEnabled[x] = regs.backScreenParams.shadowEnable; break;
        default: layer0ShadowEnabled[x] = regs.bgParams[layer - LYR_RBG0].shadowEnable; break;
        }
    }

    // Apply sprite shadow
    // TODO: apply shadow from mesh layer
    if (AnyBool(std::span{layer0ShadowEnabled}.first(m_HRes))) {
        Color888ShadowMasked(framebufferOutput, layer0ShadowEnabled);
    }

    // Gather color offset info
    alignas(16) std::array<bool, kMaxResH> layer0ColorOffsetEnabled;
    for (uint32 x = 0; x < m_HRes; x++) {
        layer0ColorOffsetEnabled[x] = regs.colorOffsetEnable[scanline_layers[x][0]];
    }

    // Apply color offset if enabled
    if (AnyBool(std::span{layer0ColorOffsetEnabled}.first(m_HRes))) {
        for (uint32 x = 0; Color888 &outputColor : framebufferOutput) {
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

    if (vdp2DebugRenderOptions.enable) {
        auto &overlay = vdp2DebugRenderOptions.overlay;
        using OverlayType = VDP2DebugRenderOptions::Overlay::Type;

        if (overlay.type != OverlayType::None) {
            if (overlay.type == OverlayType::Windows && overlay.windowLayerIndex > 5) {
                const auto &windowSet = overlay.customWindowSet;
                auto &windowState = overlay.customWindowState[altField];
                auto windowParams = regs.windowParams;
                for (uint32 i = 0; i < 2; ++i) {
                    windowParams[i].lineWindowTableEnable = overlay.customLineWindowTableEnable[i];
                    windowParams[i].lineWindowTableAddress = overlay.customLineWindowTableAddress[i] & 0x7FFFF;
                }
                if (altField) {
                    VDP2CalcWindow<true>(y, windowSet, windowParams, windowState);
                } else {
                    VDP2CalcWindow<false>(y, windowSet, windowParams, windowState);
                }
            }

            for (uint32 x = 0; x < m_HRes; ++x) {
                Color888 overlayColor{};

                switch (overlay.type) {
                case OverlayType::None: break;
                case OverlayType::SingleLayer: //
                {
                    const uint8 layerLevel = std::min<uint8>(overlay.singleLayerIndex, 8);
                    switch (layerLevel) {
                    case LYR_Back: overlayColor = m_lineBackLayerState.backColor; break;
                    case LYR_LineColor: overlayColor = m_lineBackLayerState.lineColor; break;
                    case 8 /*transparent meshes*/: overlayColor = m_meshLayerState[altField].pixels.color[x]; break;
                    default: overlayColor = m_layerStates[altField][layerLevel].pixels.color[x];
                    }
                    break;
                }
                case OverlayType::LayerStack: //
                {
                    const uint8 layerLevel = overlay.layerStackIndex < 3 ? overlay.layerStackIndex : 0;
                    const uint32 layerNum = static_cast<uint32>(scanline_layers[x][layerLevel]);
                    overlayColor = overlay.layerColors[layerNum];
                    break;
                }
                case OverlayType::Windows: //
                {
                    const uint8 layerIndex = overlay.windowLayerIndex;
                    switch (layerIndex) {
                    case 0: // Sprite
                        overlayColor = m_spriteLayerAttrs[altField].window[x] ? overlay.windowInsideColor
                                                                              : overlay.windowOutsideColor;
                        break;
                    case 1: [[fallthrough]]; // RBG0
                    case 2: [[fallthrough]]; // NBG0/RBG1
                    case 3: [[fallthrough]]; // NBG1/EXBG
                    case 4: [[fallthrough]]; // NBG2
                    case 5:                  // NBG3
                        overlayColor = m_bgWindows[altField][layerIndex - 1][x] ? overlay.windowInsideColor
                                                                                : overlay.windowOutsideColor;
                        break;
                    case 6: // Rotation parameters
                        overlayColor =
                            m_rotParamsWindow[altField][x] ? overlay.windowInsideColor : overlay.windowOutsideColor;
                        break;
                    case 7: // Color calculations
                        overlayColor =
                            m_colorCalcWindow[altField][x] ? overlay.windowInsideColor : overlay.windowOutsideColor;
                        break;
                    default: // Custom window
                        overlayColor = overlay.customWindowState[altField][x] ? overlay.windowInsideColor
                                                                              : overlay.windowOutsideColor;
                        break;
                    }

                    break;
                }
                case OverlayType::RotParams: //
                    overlayColor = VDP2SelectRotationParameter(x, y, altField) == RotParamA ? overlay.rotParamAColor
                                                                                            : overlay.rotParamBColor;
                    break;
                case OverlayType::ColorCalc: //
                {
                    const uint8 stackIndex = overlay.colorCalcStackIndex <= 1 ? overlay.colorCalcStackIndex : 0;
                    overlayColor = isColorCalcEnabled(scanline_layers[x][stackIndex], x)
                                       ? overlay.colorCalcEnableColor
                                       : overlay.colorCalcDisableColor;
                    break;
                }
                case OverlayType::Shadow: //
                {
                    overlayColor = layer0ShadowEnabled[x] ? overlay.shadowEnableColor : overlay.shadowDisableColor;
                    break;
                }
                }

                const uint8 alpha = overlay.alpha;
                Color888 &out = framebufferOutput[x];
                out.r = out.r + ((int)overlayColor.r - out.r) * alpha / 255;
                out.g = out.g + ((int)overlayColor.g - out.g) * alpha / 255;
                out.b = out.b + ((int)overlayColor.b - out.b) * alpha / 255;
            }
        }
    }

    // Opaque alpha
    for (Color888 &outputColor : framebufferOutput) {
        outputColor.u32 |= 0xFF000000;
    }
}

template <VDP::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode,
          bool useVCellScroll, bool deinterlace>
NO_INLINE void VDP::VDP2DrawNormalScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                           const NormBGLayerState &bgState, VRAMFetcher &vramFetcher,
                                           std::span<const bool> windowState, bool altField) {
    const VDP2Regs &regs = VDP2GetRegs();

    const bool altLine = deinterlace && altField && regs.TVMD.LSMDn == InterlaceMode::DoubleDensity;
    uint32 fracScrollX = bgState.fracScrollX + bgParams.scrollAmountH;
    const uint32 fracScrollY = bgState.fracScrollY + bgState.scrollAmountV + (altLine ? bgParams.scrollIncV : 0);

    uint32 cellScrollTableAddress = regs.verticalCellScrollTableAddress + bgState.vertCellScrollOffset;
    const bool verticalCellScrollEnable = useVCellScroll && bgParams.verticalCellScrollEnable;

    auto readCellScrollY = [&](bool checkRepeat = false) {
        if (checkRepeat && bgState.vertCellScrollRepeat && bgState.vertCellScrollDelay) {
            return vramFetcher.lastVCellScroll;
        }
        const uint32 value = VDP2ReadRendererVRAM<uint32>(cellScrollTableAddress);
        if (!checkRepeat || !bgState.vertCellScrollRepeat) {
            cellScrollTableAddress += m_vertCellScrollInc;
        }
        const uint32 prevValue = vramFetcher.lastVCellScroll;
        vramFetcher.lastVCellScroll = bit::extract<8, 26>(value);
        return bgState.vertCellScrollDelay ? prevValue : vramFetcher.lastVCellScroll;
    };

    uint32 mosaicCounterX = 0;
    uint32 cellScrollY = 0;
    uint32 vCellScrollX = fracScrollX >> (8u + 3u);

    if (verticalCellScrollEnable) {
        cellScrollY = readCellScrollY(true);
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
                layerState.pixels.CopyPixel(x - 1, x);

                // Increment horizontal coordinate
                fracScrollX += bgState.scrollIncH;
                continue;
            }
        } else if (verticalCellScrollEnable) {
            // Update vertical cell scroll amount
            if ((fracScrollX >> (8u + 3u)) != vCellScrollX) {
                vCellScrollX = fracScrollX >> (8u + 3u);
                cellScrollY = readCellScrollY();
            }
        }

        if (windowState[x]) {
            // Make pixel transparent if inside active window area
            layerState.pixels.transparent[x] = true;
        } else {
            // Compute integer scroll screen coordinates
            const uint32 scrollX = fracScrollX >> 8u;
            const uint32 scrollY = ((fracScrollY + cellScrollY) >> 8u) - bgState.mosaicCounterY;
            const CoordU32 scrollCoord{scrollX, scrollY};

            // Plot pixel
            const Pixel pixel = VDP2FetchScrollBGPixel<false, charMode, fourCellChar, colorFormat, colorMode>(
                bgParams, bgParams.pageBaseAddresses, bgParams.pageShiftH, bgParams.pageShiftV, scrollCoord,
                vramFetcher);
            layerState.pixels.SetPixel(x, pixel);
        }

        // Increment horizontal coordinate
        fracScrollX += bgState.scrollIncH;
    }

    // Fetch one extra tile past the end of the display area
    {
        // Apply horizontal mosaic or vertical cell-scrolling
        // Mosaic takes priority
        if (!bgParams.mosaicEnable && verticalCellScrollEnable) {
            // Update vertical cell scroll amount
            if ((fracScrollX >> (8u + 3u)) != vCellScrollX) {
                vCellScrollX = fracScrollX >> (8u + 3u);
                cellScrollY = readCellScrollY();
            }
        }

        // Compute integer scroll screen coordinates
        const uint32 scrollX = fracScrollX >> 8u;
        const uint32 scrollY = ((fracScrollY + cellScrollY) >> 8u) - bgState.mosaicCounterY;
        const CoordU32 scrollCoord{scrollX, scrollY};

        // Fetch pixel
        VDP2FetchScrollBGPixel<false, charMode, fourCellChar, colorFormat, colorMode>(
            bgParams, bgParams.pageBaseAddresses, bgParams.pageShiftH, bgParams.pageShiftV, scrollCoord, vramFetcher);

        // Increment horizontal coordinate
        fracScrollX += bgState.scrollIncH * 8;
    }
}

template <ColorFormat colorFormat, uint32 colorMode, bool useVCellScroll, bool deinterlace>
NO_INLINE void VDP::VDP2DrawNormalBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                           const NormBGLayerState &bgState, VRAMFetcher &vramFetcher,
                                           std::span<const bool> windowState, bool altField) {
    const VDP2Regs &regs = VDP2GetRegs();

    const bool doubleDensity = regs.TVMD.LSMDn == InterlaceMode::DoubleDensity;
    const bool altLine = deinterlace && altField && doubleDensity && !bgParams.lineScrollYEnable;
    uint32 fracScrollX = bgState.fracScrollX + bgParams.scrollAmountH;
    const uint32 fracScrollY = bgState.fracScrollY + bgState.scrollAmountV + (altLine ? bgParams.scrollIncV : 0);

    uint32 cellScrollTableAddress = regs.verticalCellScrollTableAddress + bgState.vertCellScrollOffset;
    const bool verticalCellScrollEnable = useVCellScroll && bgParams.verticalCellScrollEnable;

    auto readCellScrollY = [&](bool checkRepeat = false) {
        if (checkRepeat && bgState.vertCellScrollRepeat && bgState.vertCellScrollDelay) {
            return vramFetcher.lastVCellScroll;
        }
        const uint32 value = VDP2ReadRendererVRAM<uint32>(cellScrollTableAddress);
        if (!checkRepeat || !bgState.vertCellScrollRepeat) {
            cellScrollTableAddress += m_vertCellScrollInc;
        }
        const uint32 prevValue = vramFetcher.lastVCellScroll;
        vramFetcher.lastVCellScroll = bit::extract<8, 26>(value);
        return bgState.vertCellScrollDelay ? prevValue : vramFetcher.lastVCellScroll;
    };

    uint32 mosaicCounterX = 0;
    uint32 cellScrollY = 0;
    uint32 vCellScrollX = fracScrollX >> (8u + 3u);

    if (verticalCellScrollEnable) {
        cellScrollY = readCellScrollY(true);
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
                layerState.pixels.CopyPixel(x - 1, x);

                // Increment horizontal coordinate
                fracScrollX += bgState.scrollIncH;
                continue;
            }
        } else if (verticalCellScrollEnable) {
            // Update vertical cell scroll amount
            if ((fracScrollX >> (8u + 3u)) != vCellScrollX) {
                vCellScrollX = fracScrollX >> (8u + 3u);
                cellScrollY = readCellScrollY();
            }
        }

        if (windowState[x]) {
            // Make pixel transparent if inside active window area
            layerState.pixels.transparent[x] = true;
        } else {
            // Compute integer scroll screen coordinates
            const uint32 scrollX = fracScrollX >> 8u;
            const uint32 scrollY = ((fracScrollY + cellScrollY) >> 8u) - bgState.mosaicCounterY;
            const CoordU32 scrollCoord{scrollX, scrollY};

            // Plot pixel
            const Pixel pixel = VDP2FetchBitmapPixel<colorFormat, colorMode>(bgParams, bgParams.bitmapBaseAddress,
                                                                             scrollCoord, vramFetcher);
            layerState.pixels.SetPixel(x, pixel);
        }

        // Increment horizontal coordinate
        fracScrollX += bgState.scrollIncH;
    }
}

template <uint32 bgIndex, VDP::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode>
NO_INLINE void VDP::VDP2DrawRotationScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                             VRAMFetcher &vramFetcher, std::span<const bool> windowState,
                                             bool altField) {
    static constexpr bool selRotParam = bgIndex == 0;

    const VDP2Regs &regs = VDP2GetRegs();

    const bool doubleResH = regs.TVMD.HRESOn & 0b010;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 maxX = m_HRes >> xShift;

    uint32 mosaicCounterX = 0;

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;

        // Apply horizontal mosaic if enabled
        if (bgParams.mosaicEnable) {
            const uint8 currMosaicCounterX = mosaicCounterX;
            mosaicCounterX++;
            if (mosaicCounterX >= regs.mosaicH) {
                mosaicCounterX = 0;
            }
            if (currMosaicCounterX > 0) {
                // Simply copy over the data from the previous pixel
                layerState.pixels.CopyPixel(xx - 1, xx);
                if (doubleResH) {
                    layerState.pixels.CopyPixel(xx, xx + 1);
                }
                continue;
            }
        }

        const RotParamSelector rotParamSelector = selRotParam ? VDP2SelectRotationParameter(x, y, altField) : RotParamB;

        const RotationParams &rotParams = regs.rotParams[rotParamSelector];
        const RotationParamState &rotParamState = m_rotParamStates[rotParamSelector];

        // Handle transparent pixels in coefficient table
        if (rotParams.coeffTableEnable && rotParamState.transparent[x]) {
            layerState.pixels.transparent[xx] = true;
            if (doubleResH) {
                layerState.pixels.transparent[xx + 1] = true;
            }
            continue;
        }

        // Get scroll screen coordinates
        const uint32 scrollX = rotParamState.screenCoords[x].x();
        const uint32 scrollY = rotParamState.screenCoords[x].y();
        const CoordU32 scrollCoord{scrollX, scrollY};

        // Determine maximum coordinates and screen over process
        const bool usingFixed512 = rotParams.screenOverProcess == ScreenOverProcess::Fixed512;
        const bool usingRepeat = rotParams.screenOverProcess == ScreenOverProcess::Repeat;
        const uint32 maxScrollX = usingFixed512 ? 512 : ((512 * 4) << rotParams.pageShiftH);
        const uint32 maxScrollY = usingFixed512 ? 512 : ((512 * 4) << rotParams.pageShiftV);

        // TODO: optimize doubleResH vs. window handling

        if (windowState[xx] && (!doubleResH || windowState[xx + 1])) {
            // Make pixel transparent if inside a window
            layerState.pixels.transparent[xx] = true;
            if (doubleResH) {
                layerState.pixels.transparent[xx + 1] = true;
            }
        } else if ((scrollX < maxScrollX && scrollY < maxScrollY) || usingRepeat) {
            // Plot pixel
            const Pixel pixel = VDP2FetchScrollBGPixel<true, charMode, fourCellChar, colorFormat, colorMode>(
                bgParams, rotParamState.pageBaseAddresses[bgIndex], rotParams.pageShiftH, rotParams.pageShiftV,
                scrollCoord, m_vramFetchers[altField][rotParamSelector + 4]);
            if (!doubleResH || !windowState[xx]) {
                layerState.pixels.SetPixel(xx, pixel);
            }
            if (doubleResH && !windowState[xx + 1]) {
                layerState.pixels.SetPixel(xx + 1, pixel);
            }

            VDP2StoreRotationLineColorData<bgIndex>(x, bgParams, rotParamSelector);
        } else if (rotParams.screenOverProcess == ScreenOverProcess::RepeatChar) {
            // Out of bounds - repeat character
            static constexpr bool largePalette = colorFormat != ColorFormat::Palette16;
            static constexpr bool extChar = charMode == CharacterMode::OneWordExtended;

            const uint16 charData = rotParams.screenOverPatternName;
            const Character ch = VDP2ExtractOneWordCharacter<fourCellChar, largePalette, extChar>(bgParams, charData);

            const uint32 dotX = bit::extract<0, 2>(scrollX);
            const uint32 dotY = bit::extract<0, 2>(scrollY);
            const CoordU32 dotCoord{dotX, dotY};

            const Pixel pixel = VDP2FetchCharacterPixel<colorFormat, colorMode>(bgParams, ch, dotCoord, 0);
            if (!doubleResH || !windowState[xx]) {
                layerState.pixels.SetPixel(xx, pixel);
            }
            if (doubleResH && !windowState[xx + 1]) {
                layerState.pixels.SetPixel(xx + 1, pixel);
            }

            VDP2StoreRotationLineColorData<bgIndex>(x, bgParams, rotParamSelector);
        } else {
            // Out of bounds - transparent
            layerState.pixels.transparent[xx] = true;
            if (doubleResH) {
                layerState.pixels.transparent[xx + 1] = true;
            }
        }
    }
}

template <uint32 bgIndex, ColorFormat colorFormat, uint32 colorMode>
NO_INLINE void VDP::VDP2DrawRotationBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                             std::span<const bool> windowState, bool altField) {
    static constexpr bool selRotParam = bgIndex == 0;

    const VDP2Regs &regs = VDP2GetRegs();

    const bool doubleResH = regs.TVMD.HRESOn & 0b010;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 maxX = m_HRes >> xShift;

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;

        const RotParamSelector rotParamSelector = selRotParam ? VDP2SelectRotationParameter(x, y, altField) : RotParamA;

        const RotationParams &rotParams = regs.rotParams[rotParamSelector];
        const RotationParamState &rotParamState = m_rotParamStates[rotParamSelector];

        // Handle transparent pixels in coefficient table
        if (rotParams.coeffTableEnable && rotParamState.transparent[x]) {
            layerState.pixels.transparent[xx] = true;
            if (doubleResH) {
                layerState.pixels.transparent[xx + 1] = true;
            }
            continue;
        }

        // Get scroll screen coordinates
        const uint32 scrollX = rotParamState.screenCoords[x].x();
        const uint32 scrollY = rotParamState.screenCoords[x].y();
        const CoordU32 scrollCoord{scrollX, scrollY};

        const bool usingFixed512 = rotParams.screenOverProcess == ScreenOverProcess::Fixed512;
        const bool usingRepeat = rotParams.screenOverProcess == ScreenOverProcess::Repeat;
        const uint32 maxScrollX = usingFixed512 ? 512 : bgParams.bitmapSizeH;
        const uint32 maxScrollY = usingFixed512 ? 512 : bgParams.bitmapSizeV;

        // TODO: optimize doubleResH vs. window handling

        if (windowState[xx] && (!doubleResH || windowState[xx + 1])) {
            // Make pixel transparent if inside a window
            layerState.pixels.transparent[xx] = true;
            if (doubleResH) {
                layerState.pixels.transparent[xx + 1] = true;
            }
        } else if ((scrollX < maxScrollX && scrollY < maxScrollY) || usingRepeat) {
            // Plot pixel
            const Pixel pixel = VDP2FetchBitmapPixel<colorFormat, colorMode>(
                bgParams, rotParams.bitmapBaseAddress, scrollCoord, m_vramFetchers[altField][rotParamSelector + 4]);
            if (!doubleResH || !windowState[xx]) {
                layerState.pixels.SetPixel(xx, pixel);
            }
            if (doubleResH && !windowState[xx + 1]) {
                layerState.pixels.SetPixel(xx + 1, pixel);
            }

            VDP2StoreRotationLineColorData<bgIndex>(x, bgParams, rotParamSelector);
        } else {
            // Out of bounds and no repeat
            layerState.pixels.transparent[xx] = true;
            if (doubleResH) {
                layerState.pixels.transparent[xx + 1] = true;
            }
        }
    }
}

template <uint32 bgIndex>
FORCE_INLINE void VDP::VDP2StoreRotationLineColorData(uint32 x, const BGParams &bgParams,
                                                      RotParamSelector rotParamSelector) {
    const VDP2Regs &regs = VDP2GetRegs();
    const CommonRotationParams &commonRotParams = regs.commonRotParams;

    if (bgParams.lineColorScreenEnable) {
        // Line color for rotation parameters can be either the raw LNCL value or combined with coefficient table data.
        // When combined, CRAM address bits 10-7 come from LNCL and bits 6-0 come from the coefficient table.
        // This is handled in VDP2CalcRotationParameterTables.
        //
        // Whether to combine line color data depends on the rotation parameter mode:
        //   0: data from coeff A is added to rotparam A
        //   1: data from coeff B is added to rotparam B
        //   2: data from coeff A is added to both rotparams
        //   3: data from each coeff is added to each rotparam
        // If RBG1 is enabled, coeff data A is used for both RBG0 and RBG1

        const bool hasRBG1 = regs.bgEnabled[5];

        bool useCoeffLineColor = false;
        RotParamSelector coeffSel;

        using enum RotationParamMode;
        switch (commonRotParams.rotParamMode) {
        case RotationParamA:
            useCoeffLineColor = rotParamSelector == RotParamA;
            coeffSel = RotParamA;
            break;
        case RotationParamB:
            useCoeffLineColor = rotParamSelector == RotParamB;
            coeffSel = hasRBG1 ? RotParamA : RotParamB;
            break;
        case Coefficient:
            useCoeffLineColor = true;
            coeffSel = RotParamA;
            break;
        case Window:
            useCoeffLineColor = true;
            coeffSel = hasRBG1 ? RotParamA : rotParamSelector;
            break;
        }

        m_rbgLineColors[bgIndex][x] = m_lineBackLayerState.lineColor;

        if (useCoeffLineColor) {
            const RotationParams &rotParams = regs.rotParams[coeffSel];
            const RotationParamState &rotParamState = m_rotParamStates[coeffSel];
            if (rotParams.coeffTableEnable && rotParams.coeffUseLineColorData) {
                m_rbgLineColors[bgIndex][x] = rotParamState.lineColor[x];
            }
        }
    }
}

FORCE_INLINE VDP::RotParamSelector VDP::VDP2SelectRotationParameter(uint32 x, uint32 y, bool altField) {
    const VDP2Regs &regs = VDP2GetRegs();

    const CommonRotationParams &commonRotParams = regs.commonRotParams;

    using enum RotationParamMode;
    switch (commonRotParams.rotParamMode) {
    case RotationParamA: return RotParamA;
    case RotationParamB: return RotParamB;
    case Coefficient:
        return regs.rotParams[0].coeffTableEnable && m_rotParamStates[0].transparent[x] ? RotParamB : RotParamA;
    case Window: return m_rotParamsWindow[altField][x] ? RotParamB : RotParamA;
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
        if (regs.vramControl.rotDataBankSelA0 != RotDataBankSel::Coefficients) {
            return false;
        }
        break;
    case 1: // VRAM-A1
        if (regs.vramControl.rotDataBankSelA1 != RotDataBankSel::Coefficients) {
            return false;
        }
        break;
    case 2: // VRAM-B0 or VRAM-B
        if (regs.vramControl.rotDataBankSelB0 != RotDataBankSel::Coefficients) {
            return false;
        }
        break;
    case 3: // VRAM-B1
        if (regs.vramControl.rotDataBankSelB1 != RotDataBankSel::Coefficients) {
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

    const uint32 offset = coeffAddress >> 10u;

    if (params.coeffDataSize == 1) {
        // One-word coefficient data
        const uint32 address = offset * sizeof(uint16);
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
        const uint32 address = offset * sizeof(uint32);
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
FORCE_INLINE_EX VDP::Pixel
VDP::VDP2FetchScrollBGPixel(const BGParams &bgParams, std::span<const uint32> pageBaseAddresses, uint32 pageShiftH,
                            uint32 pageShiftV, CoordU32 scrollCoord, VRAMFetcher &vramFetcher) {
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

    // HACK: apply data access shift here too
    // Not entirely correct, but fixes problems with World Heroes Perfect's demo screen
    const uint32 bank = (pageBaseAddress >> 17u) & 3u;
    scrollX += bgParams.vramDataOffset[bank];

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

    // Fetch character if needed
    if (vramFetcher.lastCharIndex != charIndex) {
        vramFetcher.lastCharIndex = charIndex;
        const uint32 pageAddress = pageBaseAddress + pageOffset;
        static constexpr bool largePalette = colorFormat != ColorFormat::Palette16;
        const Character ch =
            twoWordChar
                ? VDP2FetchTwoWordCharacter(bgParams, pageAddress, charIndex)
                : VDP2FetchOneWordCharacter<fourCellChar, largePalette, extChar>(bgParams, pageAddress, charIndex);

        // Send character to pipeline
        vramFetcher.currChar = bgParams.charPatDelay ? vramFetcher.nextChar : ch;
        vramFetcher.nextChar = ch;
    } else if constexpr (fourCellChar) {
        // Each cell of a 2x2 character is fetched individually.
        // With the delay, the fetch is done between the first and the second half of the character.
        if (bgParams.charPatDelay && vramFetcher.lastCellX != cellX) {
            vramFetcher.lastCellX = cellX;
            if (cellX == 1) {
                vramFetcher.currChar = vramFetcher.nextChar;
            }
        }
    }

    // Fetch pixel using character data
    return VDP2FetchCharacterPixel<colorFormat, colorMode>(bgParams, vramFetcher.currChar, dotCoord, cellIndex);
}

FORCE_INLINE VDP::Character VDP::VDP2FetchTwoWordCharacter(const BGParams &bgParams, uint32 pageBaseAddress,
                                                           uint32 charIndex) {
    const uint32 charAddress = pageBaseAddress + charIndex * sizeof(uint32);
    const uint32 charBank = (charAddress >> 17u) & 3u;

    if (!bgParams.patNameAccess[charBank]) {
        return {};
    }

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

void VDP::ExternalLatch(uint16 x, uint16 y) {
    if (m_state.regs2.EXTEN.EXLTEN) {
        // TODO: why do we need to tweak the coords here? And why shift by 2 instead of 1?
        m_state.regs2.WriteHCNT((x + 64u) << 2u);
        m_state.regs2.VCNTLatch = y + 16u;
        m_state.regs2.TVSTAT.EXLTFG = x < m_HRes && y < m_VRes;
    }
}

template <bool fourCellChar, bool largePalette, bool extChar>
FORCE_INLINE VDP::Character VDP::VDP2FetchOneWordCharacter(const BGParams &bgParams, uint32 pageBaseAddress,
                                                           uint32 charIndex) {
    // Contents of 1 word character patterns vary based on Character Size, Character Color Count and Auxiliary Mode:
    //     Character Size        = CHCTLA/CHCTLB.xxCHSZ  = !fourCellChar = !FCC
    //     Character Color Count = CHCTLA/CHCTLB.xxCHCNn = largePalette  = LP
    //     Auxiliary Mode        = PNCN0/PNCR.xxCNSM     = extChar       = EC
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
    const uint32 charBank = (charAddress >> 17u) & 3u;

    if (!bgParams.patNameAccess[charBank]) {
        return {};
    }

    const uint16 charData = VDP2ReadRendererVRAM<uint16>(charAddress);
    return VDP2ExtractOneWordCharacter<fourCellChar, largePalette, extChar>(bgParams, charData);
}

template <bool fourCellChar, bool largePalette, bool extChar>
FORCE_INLINE VDP::Character VDP::VDP2ExtractOneWordCharacter(const BGParams &bgParams, uint16 charData) {
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

    Character ch;
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
    if constexpr (colorFormat == ColorFormat::RGB888) {
        cellIndex <<= 3;
    } else if constexpr (colorFormat == ColorFormat::RGB555) {
        cellIndex <<= 2;
    } else if constexpr (colorFormat != ColorFormat::Palette16) {
        cellIndex <<= 1;
    }

    // Cell addressing uses a fixed offset of 32 bytes
    const uint32 cellAddress = (ch.charNum + cellIndex) * 0x20;
    const uint32 dotOffset = dotX + dotY * 8;

    // Determine special color calculation flag
    const auto &specFuncCode = regs.specialFunctionCodes[bgParams.specialFunctionSelect];
    auto getSpecialColorCalcFlag = [&](uint8 specColorCode, bool colorMSB) {
        using enum SpecialColorCalcMode;
        switch (bgParams.specialColorCalcMode) {
        case PerScreen: return bgParams.colorCalcEnable;
        case PerCharacter: return bgParams.colorCalcEnable && ch.specColorCalc;
        case PerDot: return bgParams.colorCalcEnable && ch.specColorCalc && specFuncCode.colorMatches[specColorCode];
        case ColorDataMSB: return bgParams.colorCalcEnable && colorMSB;
        }
        util::unreachable();
    };

    // Fetch color and determine transparency.
    // Also determine special color calculation flag if using per-dot or color data MSB.
    uint8 colorData;
    if constexpr (colorFormat == ColorFormat::Palette16) {
        const uint32 dotAddress = cellAddress + (dotOffset >> 1u);
        const uint32 dotBank = (dotAddress >> 17u) & 3u;
        const uint8 dotData = bgParams.charPatAccess[dotBank]
                                  ? ((VDP2ReadRendererVRAM<uint8>(dotAddress) >> ((~dotX & 1) * 4)) & 0xF)
                                  : 0x0;
        const uint32 colorIndex = (ch.palNum << 4u) | dotData;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData, pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::Palette256) {
        const uint32 dotAddress = cellAddress + dotOffset;
        const uint32 dotBank = (dotAddress >> 17u) & 3u;
        const uint8 dotData = bgParams.charPatAccess[dotBank] ? VDP2ReadRendererVRAM<uint8>(dotAddress) : 0x00;
        const uint32 colorIndex = ((ch.palNum & 0x70) << 4u) | dotData;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData, pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::Palette2048) {
        const uint32 dotAddress = cellAddress + dotOffset * sizeof(uint16);
        const uint32 dotBank = (dotAddress >> 17u) & 3u;
        const uint16 dotData = bgParams.charPatAccess[dotBank] ? VDP2ReadRendererVRAM<uint16>(dotAddress) : 0x0000;
        const uint32 colorIndex = dotData & 0x7FF;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && (dotData & 0x7FF) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData, pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::RGB555) {
        const uint32 dotAddress = cellAddress + dotOffset * sizeof(uint16);
        const uint32 dotBank = (dotAddress >> 17u) & 3u;
        const uint16 dotData = bgParams.charPatAccess[dotBank] ? VDP2ReadRendererVRAM<uint16>(dotAddress) : 0x0000;
        pixel.color = ConvertRGB555to888(Color555{.u16 = dotData});
        pixel.transparent = bgParams.enableTransparency && bit::extract<15>(dotData) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(0b111, true);

    } else if constexpr (colorFormat == ColorFormat::RGB888) {
        const uint32 dotAddress = cellAddress + dotOffset * sizeof(uint32);
        const uint32 dotBank = (dotAddress >> 17u) & 3u;
        const uint32 dotData = bgParams.charPatAccess[dotBank] ? VDP2ReadRendererVRAM<uint32>(dotAddress) : 0x00000000;
        pixel.color.u32 = dotData;
        pixel.transparent = bgParams.enableTransparency && bit::extract<31>(dotData) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(0b111, true);
    }

    // Compute priority
    pixel.priority = bgParams.priorityNumber;
    if (bgParams.priorityMode == PriorityMode::PerCharacter) {
        pixel.priority &= ~1;
        pixel.priority |= (uint8)ch.specPriority;
    } else if (bgParams.priorityMode == PriorityMode::PerDot && ch.specPriority) {
        if constexpr (IsPaletteColorFormat(colorFormat)) {
            pixel.priority &= ~1;
            pixel.priority |= static_cast<uint8>(specFuncCode.colorMatches[colorData]);
        }
    }

    return pixel;
}

template <ColorFormat colorFormat, uint32 colorMode>
FORCE_INLINE VDP::Pixel VDP::VDP2FetchBitmapPixel(const BGParams &bgParams, uint32 bitmapBaseAddress, CoordU32 dotCoord,
                                                  VRAMFetcher &vramFetcher) {
    static_assert(static_cast<uint32>(colorFormat) <= 4, "Invalid xxCHCN value");

    const VDP2Regs &regs = VDP2GetRegs();

    Pixel pixel{};

    auto [dotX, dotY] = dotCoord;

    // Bitmap data wraps around infinitely
    dotX &= bgParams.bitmapSizeH - 1;
    dotY &= bgParams.bitmapSizeV - 1;

    // Bitmap addressing uses a fixed offset of 0x20000 bytes which is precalculated when MPOFN/MPOFR is written to
    const uint32 dotOffset = dotX + dotY * bgParams.bitmapSizeH;
    const uint32 palNum = bgParams.supplBitmapPalNum;

    // Determine special color calculation flag
    const auto &specFuncCode = regs.specialFunctionCodes[bgParams.specialFunctionSelect];
    auto getSpecialColorCalcFlag = [&](uint8 specColorCode, bool colorDataMSB) {
        using enum SpecialColorCalcMode;
        switch (bgParams.specialColorCalcMode) {
        case PerScreen: return bgParams.colorCalcEnable;
        case PerCharacter: return bgParams.colorCalcEnable && bgParams.supplBitmapSpecialColorCalc;
        case PerDot:
            return bgParams.colorCalcEnable && bgParams.supplBitmapSpecialColorCalc &&
                   specFuncCode.colorMatches[specColorCode];
        case ColorDataMSB: return bgParams.colorCalcEnable && colorDataMSB;
        }
        util::unreachable();
    };

    auto fetchBitmapData = [&](uint32 address) {
        const uint32 bank = (address >> 17u) & 3u;
        if (!bgParams.charPatAccess[bank]) {
            vramFetcher.bitmapData.fill(0);
            return;
        }

        const uint32 offset = bgParams.vramDataOffset[bank];

        if (vramFetcher.UpdateBitmapDataAddress(address)) {
            address += offset;

            // TODO: handle VRSIZE.VRAMSZ
            auto &vram = VDP2GetRendererVRAM();
            std::copy_n(&vram[address & 0x7FFF8], 8, vramFetcher.bitmapData.begin());
        }
    };

    uint8 colorData;
    if constexpr (colorFormat == ColorFormat::Palette16) {
        const uint32 dotAddress = bitmapBaseAddress + (dotOffset >> 1u);
        fetchBitmapData(dotAddress);
        const uint8 dotData = (vramFetcher.bitmapData[dotAddress & 7] >> ((~dotX & 1) * 4)) & 0xF;
        const uint32 colorIndex = palNum | dotData;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(bit::extract<1, 3>(dotData), pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::Palette256) {
        const uint32 dotAddress = bitmapBaseAddress + dotOffset;
        fetchBitmapData(dotAddress);
        const uint8 dotData = vramFetcher.bitmapData[dotAddress & 7];
        const uint32 colorIndex = palNum | dotData;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(bit::extract<1, 3>(dotData), pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::Palette2048) {
        const uint32 dotAddress = bitmapBaseAddress + dotOffset * sizeof(uint16);
        fetchBitmapData(dotAddress);
        const uint16 dotData = util::ReadBE<uint16>(&vramFetcher.bitmapData[dotAddress & 6]);
        const uint32 colorIndex = dotData & 0x7FF;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && (dotData & 0x7FF) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(bit::extract<1, 3>(dotData), pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::RGB555) {
        const uint32 dotAddress = bitmapBaseAddress + dotOffset * sizeof(uint16);
        fetchBitmapData(dotAddress);
        const uint16 dotData = util::ReadBE<uint16>(&vramFetcher.bitmapData[dotAddress & 6]);
        pixel.color = ConvertRGB555to888(Color555{.u16 = dotData});
        pixel.transparent = bgParams.enableTransparency && bit::extract<15>(dotData) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(0b111, true);

    } else if constexpr (colorFormat == ColorFormat::RGB888) {
        const uint32 dotAddress = bitmapBaseAddress + dotOffset * sizeof(uint32);
        fetchBitmapData(dotAddress);
        const uint32 dotData = util::ReadBE<uint32>(&vramFetcher.bitmapData[dotAddress & 4]);
        pixel.color = Color888{.u32 = dotData};
        pixel.transparent = bgParams.enableTransparency && bit::extract<31>(dotData) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(0b111, true);
    }

    // Compute priority
    pixel.priority = bgParams.priorityNumber;
    if (bgParams.priorityMode == PriorityMode::PerCharacter) {
        pixel.priority &= ~1;
        pixel.priority |= (uint8)bgParams.supplBitmapSpecialPriority;
    } else if (bgParams.priorityMode == PriorityMode::PerDot && bgParams.supplBitmapSpecialPriority) {
        if constexpr (IsPaletteColorFormat(colorFormat)) {
            pixel.priority &= ~1;
            pixel.priority |= static_cast<uint8>(specFuncCode.colorMatches[colorData]);
        }
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

// Determines the type of sprite data (if any) based on color data.
//
// colorData is the raw color data.
//
// colorDataBits specifies the bit width of the color data.
template <uint32 colorDataBits>
FORCE_INLINE static SpriteData::Special GetSpecialPattern(uint16 rawData) {
    // Normal shadow pattern (LSB = 0, rest of the color data bits = 1)
    static constexpr uint16 kNormalShadowValue = (1u << (colorDataBits + 1u)) - 2u;

    if ((rawData & 0x7FFF) == 0) {
        return SpriteData::Special::Transparent;
    } else if (bit::extract<0, colorDataBits>(rawData) == kNormalShadowValue) {
        return SpriteData::Special::Shadow;
    } else {
        return SpriteData::Special::Normal;
    }
}

template <bool applyMesh>
FLATTEN FORCE_INLINE SpriteData VDP::VDP2FetchSpriteData(const SpriteFB &fb, uint32 fbOffset) {
    const VDP1Regs &regs1 = VDP1GetRegs();
    const VDP2Regs &regs2 = VDP2GetRegs();

    // Adjust offset based on VDP1 data size.
    // The majority of games actually set the sprite readout size to match the VDP1 sprite data size, but there's
    // *always* an exception...
    // 8-bit VDP1 data vs. 16-bit readout: NBA Live 98
    // 16-bit VDP1 data vs. 8-bit readout: I Love Donald Duck
    const uint8 type = regs2.spriteParams.type;
    uint16 rawData;
    if (regs1.pixel8Bits) {
        rawData = fb[fbOffset & 0x3FFFF];
        if (type < 8 && (!applyMesh || rawData != 0)) {
            rawData |= 0xFF00;
        }
    } else {
        fbOffset *= sizeof(uint16);
        if (type >= 8) {
            ++fbOffset;
        }
        rawData = util::ReadBE<uint16>(&fb[fbOffset & 0x3FFFE]);
    }

    // Sprite types 0-7 are 16-bit, 8-15 are 8-bit

    SpriteData data{};
    switch (regs2.spriteParams.type) {
    case 0x0:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 13>(rawData);
        data.priority = bit::extract<14, 15>(rawData);
        data.special = GetSpecialPattern<10>(rawData);
        break;

    case 0x1:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 12>(rawData);
        data.priority = bit::extract<13, 15>(rawData);
        data.special = GetSpecialPattern<10>(rawData);
        break;

    case 0x2:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 13>(rawData);
        data.priority = bit::extract<14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<10>(rawData);
        break;

    case 0x3:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11, 12>(rawData);
        data.priority = bit::extract<13, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<10>(rawData);
        break;

    case 0x4:
        data.colorData = bit::extract<0, 9>(rawData);
        data.colorCalcRatio = bit::extract<10, 12>(rawData);
        data.priority = bit::extract<13, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<9>(rawData);
        break;

    case 0x5:
        data.colorData = bit::extract<0, 10>(rawData);
        data.colorCalcRatio = bit::extract<11>(rawData);
        data.priority = bit::extract<12, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<10>(rawData);
        break;

    case 0x6:
        data.colorData = bit::extract<0, 9>(rawData);
        data.colorCalcRatio = bit::extract<10, 11>(rawData);
        data.priority = bit::extract<12, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<9>(rawData);
        break;

    case 0x7:
        data.colorData = bit::extract<0, 8>(rawData);
        data.colorCalcRatio = bit::extract<9, 11>(rawData);
        data.priority = bit::extract<12, 14>(rawData);
        data.shadowOrWindow = bit::test<15>(rawData);
        data.special = GetSpecialPattern<8>(rawData);
        break;

    case 0x8:
        data.colorData = bit::extract<0, 6>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.special = GetSpecialPattern<6>(rawData);
        break;

    case 0x9:
        data.colorData = bit::extract<0, 5>(rawData);
        data.colorCalcRatio = bit::extract<6>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.special = GetSpecialPattern<5>(rawData);
        break;

    case 0xA:
        data.colorData = bit::extract<0, 5>(rawData);
        data.priority = bit::extract<6, 7>(rawData);
        data.special = GetSpecialPattern<5>(rawData);
        break;

    case 0xB:
        data.colorData = bit::extract<0, 5>(rawData);
        data.colorCalcRatio = bit::extract<6, 7>(rawData);
        data.special = GetSpecialPattern<5>(rawData);
        break;

    case 0xC:
        data.colorData = bit::extract<0, 7>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.special = GetSpecialPattern<7>(rawData);
        break;

    case 0xD:
        data.colorData = bit::extract<0, 7>(rawData);
        data.colorCalcRatio = bit::extract<6>(rawData);
        data.priority = bit::extract<7>(rawData);
        data.special = GetSpecialPattern<7>(rawData);
        break;

    case 0xE:
        data.colorData = bit::extract<0, 7>(rawData);
        data.priority = bit::extract<6, 7>(rawData);
        data.special = GetSpecialPattern<7>(rawData);
        break;

    case 0xF:
        data.colorData = bit::extract<0, 7>(rawData);
        data.colorCalcRatio = bit::extract<6, 7>(rawData);
        data.special = GetSpecialPattern<7>(rawData);
        break;
    }
    return data;
}

template <bool deinterlace>
FORCE_INLINE uint32 VDP::VDP2GetY(uint32 y) const {
    const VDP2Regs &regs = VDP2GetRegs();

    if (regs.TVMD.IsInterlaced() && !m_exclusiveMonitor) {
        return (y << 1) | (regs.TVSTAT.ODD & !deinterlace);
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
    return m_vdp.m_state.regs2.TVMD.LSMDn;
}

const VDP1Regs &VDP::Probe::GetVDP1Regs() const {
    return m_vdp.m_state.regs1;
}

const VDP2Regs &VDP::Probe::GetVDP2Regs() const {
    return m_vdp.m_state.regs2;
}

const std::array<VDP::NormBGLayerState, 4> &VDP::Probe::GetNBGLayerStates() const {
    return m_vdp.m_normBGLayerStates;
}

uint16 VDP::Probe::GetLatchedEraseWriteValue() const {
    return m_vdp.m_VDP1RenderState.eraseWriteValue;
}

uint16 VDP::Probe::GetLatchedEraseX1() const {
    return m_vdp.m_VDP1RenderState.eraseX1;
}

uint16 VDP::Probe::GetLatchedEraseY1() const {
    return m_vdp.m_VDP1RenderState.eraseY1;
}

uint16 VDP::Probe::GetLatchedEraseX3() const {
    return m_vdp.m_VDP1RenderState.eraseX3;
}

uint16 VDP::Probe::GetLatchedEraseY3() const {
    return m_vdp.m_VDP1RenderState.eraseY3;
}

template <mem_primitive T>
void VDP::Probe::VDP1WriteVRAM(uint32 address, T value) {
    m_vdp.VDP1WriteVRAM<T, true>(address, value);
}

template void VDP::Probe::VDP1WriteVRAM<uint8>(uint32, uint8);
template void VDP::Probe::VDP1WriteVRAM<uint16>(uint32, uint16);

void VDP::Probe::VDP1WriteReg(uint32 address, uint16 value) {
    m_vdp.VDP1WriteReg<true>(address, value);
}

Color555 VDP::Probe::VDP2GetCRAMColor555(uint32 index) const {
    return ConvertRGB888to555(m_vdp.VDP2FetchCRAMColor<1>(0, index));
}

Color888 VDP::Probe::VDP2GetCRAMColor888(uint32 index) const {
    return m_vdp.VDP2FetchCRAMColor<2>(0, index);
}

void VDP::Probe::VDP2SetCRAMColor555(uint32 index, Color555 color) {
    m_vdp.VDP2WriteCRAM<uint16, true>(index * sizeof(uint16), color.u16);
}

void VDP::Probe::VDP2SetCRAMColor888(uint32 index, Color888 color) {
    m_vdp.VDP2WriteCRAM<uint32, true>(index * sizeof(uint32), color.u32);
}

uint8 VDP::Probe::VDP2GetCRAMMode() const {
    return m_vdp.m_state.regs2.vramControl.colorRAMMode;
}

} // namespace ymir::vdp
