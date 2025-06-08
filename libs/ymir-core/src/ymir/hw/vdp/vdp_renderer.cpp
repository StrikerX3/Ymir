#include <ymir/hw/vdp/vdp_renderer.hpp>

#include "slope.hpp"

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/constexpr_for.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/scope_guard.hpp>
#include <ymir/util/thread_name.hpp>
#include <ymir/util/unreachable.hpp>

#include <cassert>

#if defined(_M_X64) || defined(__x86_64__)
    #include <immintrin.h>
#elif defined(_M_ARM64) || defined(__aarch64__)
    #include <arm_neon.h>
#endif

#include <algorithm>
#include <string_view>

namespace ymir::vdp {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   vdp1
    //   vdp2

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "VDP-Render";
    };

    struct vdp1 : public base {
        static constexpr std::string_view name = "VDP1-Render";
    };

    struct vdp2 : public base {
        static constexpr std::string_view name = "VDP2-Render";
    };

} // namespace grp

VDPRenderer::VDPRenderer(VDPState &mainState)
    : m_mainState(mainState) {
    Reset(true);
}

VDPRenderer::~VDPRenderer() {
    if (m_threadedRendering) {
        EnqueueEvent(RenderEvent::Shutdown());
        if (m_renderThread.joinable()) {
            m_renderThread.join();
        }
    }
}

void VDPRenderer::Reset(bool hard) {
    if (m_threadedRendering) {
        m_vdp1Done = false;
        EnqueueEvent(RenderEvent::Reset(hard));
    }
}

template <bool debug>
void VDPRenderer::Advance(uint64 cycles) {
    if (!m_effectiveRenderVDP1InVDP2Thread) {
        // HACK: slow down VDP1 commands to avoid FMV freezes on Virtua Racing
        // TODO: use this counter in the threaded renderer
        // TODO: proper cycle counting
        static constexpr uint64 kCyclesPerCommand = 12;

        m_VDP1RenderContext.cycleCount += cycles;
        const uint64 steps = m_VDP1RenderContext.cycleCount / kCyclesPerCommand;
        m_VDP1RenderContext.cycleCount %= kCyclesPerCommand;

        if (m_deinterlaceRender) {
            for (uint64 i = 0; i < steps; i++) {
                VDP1ProcessCommand<true>();
            }
        } else {
            for (uint64 i = 0; i < steps; i++) {
                VDP1ProcessCommand<false>();
            }
        }
    }
}

template void VDPRenderer::Advance<false>(uint64);
template void VDPRenderer::Advance<true>(uint64);

void VDPRenderer::EnqueueEvent(RenderEvent &&event) {
    switch (event.type) {
    case RenderEvent::Type::VDP1VRAMWriteByte:
    case RenderEvent::Type::VDP1VRAMWriteWord:
    case RenderEvent::Type::VDP1RegWrite:
    case RenderEvent::Type::VDP2VRAMWriteByte:
    case RenderEvent::Type::VDP2VRAMWriteWord:
    case RenderEvent::Type::VDP2CRAMWriteByte:
    case RenderEvent::Type::VDP2CRAMWriteWord:
    case RenderEvent::Type::VDP2RegWrite:
        // Batch VRAM, CRAM and register writes to send in bulk
        m_pendingEvents[m_pendingEventsCount++] = event;
        if (m_pendingEventsCount == m_pendingEvents.size()) {
            m_eventQueue.enqueue_bulk(m_pTok, m_pendingEvents.begin(), m_pendingEventsCount);
            m_pendingEventsCount = 0;
        }
        break;
    default:
        // Send any pending writes before rendering
        if (m_pendingEventsCount > 0) {
            m_eventQueue.enqueue_bulk(m_pTok, m_pendingEvents.begin(), m_pendingEventsCount);
            m_pendingEventsCount = 0;
        }
        m_eventQueue.enqueue(m_pTok, std::move(event));
        break;
    }
}

void VDPRenderer::SetLayerEnabled(Layer layer, bool enabled) {
    m_layerStates[static_cast<size_t>(layer)].rendered = enabled;
    VDP2UpdateEnabledBGs();
}

bool VDPRenderer::IsLayerEnabled(Layer layer) const {
    return m_layerStates[static_cast<size_t>(layer)].rendered;
}

FORCE_INLINE void VDPRenderer::SaveStatePrologue() {
    EnqueueEvent(RenderEvent::PreSaveStateSync());
    m_preSaveSyncSignal.Wait(true);
}

void VDPRenderer::SaveState(state::VDPState &state) const {
    if (m_threadedRendering) {
        // Requires mutation to the event queue
        const_cast<VDPRenderer *>(this)->SaveStatePrologue();
    }

    state.renderer.vdp1State.sysClipH = m_VDP1RenderContext.sysClipH;
    state.renderer.vdp1State.sysClipV = m_VDP1RenderContext.sysClipV;
    state.renderer.vdp1State.userClipX0 = m_VDP1RenderContext.userClipX0;
    state.renderer.vdp1State.userClipY0 = m_VDP1RenderContext.userClipY0;
    state.renderer.vdp1State.userClipX1 = m_VDP1RenderContext.userClipX1;
    state.renderer.vdp1State.userClipY1 = m_VDP1RenderContext.userClipY1;
    state.renderer.vdp1State.localCoordX = m_VDP1RenderContext.localCoordX;
    state.renderer.vdp1State.localCoordY = m_VDP1RenderContext.localCoordY;
    state.renderer.vdp1State.rendering = m_VDP1RenderContext.rendering;
    state.renderer.vdp1State.erase = m_VDP1RenderContext.erase;
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

    state.renderer.displayFB = m_localState.displayFB; // TODO: might not be necessary
    state.renderer.vdp1Done = m_vdp1Done;
}

bool VDPRenderer::ValidateState(const state::VDPState &state) const {
    return true;
}

void VDPRenderer::LoadState(const state::VDPState &state) {
    for (uint32 address = 0; address < kVDP2CRAMSize; address += 2) {
        VDP2UpdateCRAMCache<uint16>(address);
    }
    VDP2UpdateEnabledBGs();

    if (m_threadedRendering) {
        EnqueueEvent(RenderEvent::PostLoadStateSync());
        m_postLoadSyncSignal.Wait(true);
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
    m_VDP1RenderContext.erase = state.renderer.vdp1State.erase;
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

    m_localState.displayFB = state.renderer.displayFB;
    m_vdp1Done = state.renderer.vdp1Done;

    m_localState.UpdateResolution<true>();
}

// -----------------------------------------------------------------------------
// Rendering control

void VDPRenderer::BeginFrame() {
    if (m_mainState.regs2.bgEnabled[5]) {
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

void VDPRenderer::EndFrame() {
    if (m_threadedRendering) {
        EnqueueEvent(RenderEvent::VDP2EndFrame());
        m_renderFinishedSignal.Wait(true);
    }
    m_cbFrameComplete(m_framebuffer.data(), m_mainState.HRes, m_mainState.VRes);
}

void VDPRenderer::BeginVDP1() {
    const uint32 drawFB = m_mainState.displayFB ^ 1;

    devlog::trace<grp::vdp1>("Begin VDP1 frame on framebuffer {}", drawFB);

    // TODO: setup rendering
    // TODO: figure out VDP1 timings

    m_mainState.regs1.prevCommandAddress = m_mainState.regs1.currCommandAddress;
    m_mainState.regs1.currCommandAddress = 0;
    m_mainState.regs1.returnAddress = ~0;
    m_mainState.regs1.prevFrameEnded = m_mainState.regs1.currFrameEnded;
    m_mainState.regs1.currFrameEnded = false;

    m_VDP1RenderContext.rendering = true;
    if (m_threadedRendering) {
        EnqueueEvent(RenderEvent::VDP1BeginFrame());
    }
}

void VDPRenderer::ProcessLine(uint32 y) {
    if (m_threadedRendering) {
        // HACK: trigger VDP1 done events
        if (m_effectiveRenderVDP1InVDP2Thread && m_vdp1Done) {
            m_mainState.regs1.currFrameEnded = true;
            m_cbVDP1FrameComplete();
            m_vdp1Done = false;
        }

        EnqueueEvent(RenderEvent::VDP2DrawLine(y));
    } else {
        m_deinterlaceRender ? VDP2DrawLine<true>(y) : VDP2DrawLine<false>(y);
    }
}

void VDPRenderer::ProcessVBlankHBlank() {
    if (m_mainState.regs1.vblankErase || !m_mainState.regs1.fbSwapMode) {
        // TODO: cycle-count the erase process, starting here
        if (m_threadedRendering) {
            EnqueueEvent(RenderEvent::VDP1EraseFramebuffer());
            if (!m_effectiveRenderVDP1InVDP2Thread) {
                m_eraseFramebufferReadySignal.Wait(true);
                VDP1EraseFramebuffer();
            }
        } else {
            VDP1EraseFramebuffer();
        }
    }
}

void VDPRenderer::ProcessVBlankOUT() {
    // FIXME: this breaks several games:
    // - After Burner II and OutRun: erases data used by VDP2 graphics tiles
    // - Powerslave/Exhumed: intro video flashes light blue every other frame
    //
    // Without this, Mickey Mouse/Donald Duck don't clear sprites on some screens (e.g. Donald Duck's items menu)

    /*
    // Erase frame if manually requested in previous frame
    if (m_VDP1RenderContext.erase) {
        m_VDP1RenderContext.erase = false;
        if (m_effectiveRenderVDP1InVDP2Thread) {
            EnqueueEvent(RenderEvent::VDP1EraseFramebuffer());
        } else {
            VDP1EraseFramebuffer();
        }
    }

    // If manual erase is requested, schedule it for the next frame
    if (m_mainState.regs1.fbManualErase) {
        m_mainState.regs1.fbManualErase = false;
        m_VDP1RenderContext.erase = true;
    }
    */

    // Swap framebuffer in manual swap requested or in 1-cycle mode
    if (!m_mainState.regs1.fbSwapMode || m_mainState.regs1.fbManualSwap) {
        m_mainState.regs1.fbManualSwap = false;
        VDP1SwapFramebuffer();
    }
}

void VDPRenderer::ProcessEvenOddFieldSwitch() {
    if (m_threadedRendering) {
        EnqueueEvent(RenderEvent::OddField(m_mainState.regs2.TVSTAT.ODD));
    }
}

// -----------------------------------------------------------------------------
// VDP1

FORCE_INLINE void VDPRenderer::VDP1EraseFramebuffer() {
    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;

    const uint8 fbIndex = vdpState.displayFB;
    const uint32 VRes = m_mainState.VRes;

    devlog::trace<grp::vdp1>("Erasing framebuffer {} - {}x{} to {}x{} -> {:04X}  {}x{}  {}-bit", fbIndex, regs1.eraseX1,
                             regs1.eraseY1, regs1.eraseX3, regs1.eraseY3, regs1.eraseWriteValue, regs1.fbSizeH,
                             regs1.fbSizeV, (regs1.pixel8Bits ? 8 : 16));

    auto &fb = m_mainState.spriteFB[fbIndex];
    auto &altFB = m_altSpriteFB[fbIndex];

    // Horizontal scale is doubled in hi-res modes or when targeting rotation background
    const uint32 scaleH = (regs2.TVMD.HRESOn & 0b010) || regs1.fbRotEnable ? 1 : 0;
    // Vertical scale is doubled in double-interlace mode
    const uint32 scaleV = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity ? 1 : 0;

    // Constrain erase area to certain limits based on current resolution
    const uint32 maxH = (regs2.TVMD.HRESOn & 1) ? 428 : 400;
    const uint32 maxV = VRes >> scaleV;

    const uint32 offsetShift = regs1.pixel8Bits ? 0 : 1;

    const uint32 x1 = std::min<uint32>(regs1.eraseX1, maxH) << scaleH;
    const uint32 x3 = std::min<uint32>(regs1.eraseX3, maxH) << scaleH;
    const uint32 y1 = std::min<uint32>(regs1.eraseY1, maxV) << scaleV;
    const uint32 y3 = std::min<uint32>(regs1.eraseY3, maxV) << scaleV;

    const bool mirror = m_deinterlaceRender && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    for (uint32 y = y1; y <= y3; y++) {
        const uint32 fbOffset = y * regs1.fbSizeH;
        for (uint32 x = x1; x <= x3; x++) {
            const uint32 address = (fbOffset + x) << offsetShift;
            util::WriteBE<uint16>(&fb[address & 0x3FFFE], regs1.eraseWriteValue);
            if (mirror) {
                util::WriteBE<uint16>(&altFB[address & 0x3FFFE], regs1.eraseWriteValue);
            }
        }
    }
}

FORCE_INLINE void VDPRenderer::VDP1SwapFramebuffer() {
    VDP1Regs &regs1 = m_mainState.regs1;
    uint8 &displayFB = m_mainState.displayFB;

    devlog::trace<grp::vdp1>("Swapping framebuffers - draw {}, display {}", displayFB, displayFB ^ 1);

    // FIXME: FCM=1 FCT=0 should erase regardless of framebuffer swap, otherwise I Love Mickey Mouse/Donald Duck leaves
    // behind sprites in some screens
    if (regs1.fbManualErase) {
        regs1.fbManualErase = false;
        if (m_threadedRendering) {
            EnqueueEvent(RenderEvent::VDP1EraseFramebuffer());
        } else {
            VDP1EraseFramebuffer();
        }
    }

    if (m_threadedRendering) {
        EnqueueEvent(RenderEvent::VDP1SwapFramebuffer());
        m_framebufferSwapSignal.Wait(true);
    }

    displayFB ^= 1;

    if (bit::test<1>(regs1.plotTrigger)) {
        BeginVDP1();
    }
}

void VDPRenderer::VDP1EndFrame() {
    const uint8 drawFB = m_mainState.displayFB ^ 1;

    devlog::trace<grp::vdp1>("End VDP1 frame on framebuffer {}", drawFB);

    m_VDP1RenderContext.rendering = false;

    if (m_threadedRendering) {
        m_vdp1Done = true;
    } else {
        m_mainState.regs1.currFrameEnded = true;
        m_cbVDP1FrameComplete();
    }
}

template <bool deinterlace>
void VDPRenderer::VDP1ProcessCommand() {
    static constexpr uint32 kNoReturn = ~0;

    if (!m_VDP1RenderContext.rendering) {
        return;
    }

    auto &cmdAddress = m_mainState.regs1.currCommandAddress;

    const VDP1Command::Control control{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress)};
    devlog::trace<grp::vdp1>("Processing command {:04X} @ {:05X}", control.u16, cmdAddress);
    if (control.end) [[unlikely]] {
        devlog::trace<grp::vdp1>("End of command list");
        VDP1EndFrame();
    } else if (!control.skip) {
        // Process command
        using enum VDP1Command::CommandType;

        switch (control.command) {
        case DrawNormalSprite: VDP1Cmd_DrawNormalSprite<deinterlace>(cmdAddress, control); break;
        case DrawScaledSprite: VDP1Cmd_DrawScaledSprite<deinterlace>(cmdAddress, control); break;
        case DrawDistortedSprite: [[fallthrough]];
        case DrawDistortedSpriteAlt: VDP1Cmd_DrawDistortedSprite<deinterlace>(cmdAddress, control); break;

        case DrawPolygon: VDP1Cmd_DrawPolygon<deinterlace>(cmdAddress); break;
        case DrawPolylines: [[fallthrough]];
        case DrawPolylinesAlt: VDP1Cmd_DrawPolylines<deinterlace>(cmdAddress); break;
        case DrawLine: VDP1Cmd_DrawLine<deinterlace>(cmdAddress); break;

        case UserClipping: [[fallthrough]];
        case UserClippingAlt: VDP1Cmd_SetUserClipping(cmdAddress); break;
        case SystemClipping: VDP1Cmd_SetSystemClipping(cmdAddress); break;
        case SetLocalCoordinates: VDP1Cmd_SetLocalCoordinates(cmdAddress); break;

        default:
            devlog::debug<grp::vdp1>("Unexpected command type {:X}; aborting", static_cast<uint16>(control.command));
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
            devlog::trace<grp::vdp1>("Jump to {:05X}", cmdAddress);

            // HACK: Sonic R attempts to jump back to 0 in some cases
            if (cmdAddress == 0) {
                devlog::warn<grp::vdp1>("Possible infinite loop detected; aborting");
                VDP1EndFrame();
                return;
            }
            break;
        }
        case Call: {
            // Nested calls seem to not update the return address
            if (m_mainState.regs1.returnAddress == kNoReturn) {
                m_mainState.regs1.returnAddress = cmdAddress + 0x20;
            }
            cmdAddress = (VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x02) << 3u) & ~0x1F;
            devlog::trace<grp::vdp1>("Call {:05X}", cmdAddress);
            break;
        }
        case Return: {
            // Return seems to only return if there was a previous Call
            if (m_mainState.regs1.returnAddress != kNoReturn) {
                cmdAddress = m_mainState.regs1.returnAddress;
                m_mainState.regs1.returnAddress = kNoReturn;
            } else {
                cmdAddress += 0x20;
            }
            devlog::trace<grp::vdp1>("Return to {:05X}", cmdAddress);
            break;
        }
        }
        cmdAddress &= 0x7FFFF;
    }
}

template <bool deinterlace>
FORCE_INLINE bool VDPRenderer::VDP1IsPixelUserClipped(CoordS32 coord) const {
    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;
    const bool doubleV = deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
    auto [x, y] = coord;
    const auto &ctx = m_VDP1RenderContext;
    if (x < ctx.userClipX0 || x > ctx.userClipX1) {
        return true;
    }
    if (y < (ctx.userClipY0 << doubleV) || y > (ctx.userClipY1 << doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace>
FORCE_INLINE bool VDPRenderer::VDP1IsPixelSystemClipped(CoordS32 coord) const {
    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;
    const bool doubleV = deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
    auto [x, y] = coord;
    const auto &ctx = m_VDP1RenderContext;
    if (x < 0 || x > ctx.sysClipH) {
        return true;
    }
    if (y < 0 || y > (ctx.sysClipV << doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace>
FORCE_INLINE bool VDPRenderer::VDP1IsLineSystemClipped(CoordS32 coord1, CoordS32 coord2) const {
    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;
    const bool doubleV = deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
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
    if (y1 > (ctx.sysClipV << doubleV) && y2 > (ctx.sysClipV << doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace>
bool VDPRenderer::VDP1IsQuadSystemClipped(CoordS32 coord1, CoordS32 coord2, CoordS32 coord3, CoordS32 coord4) const {
    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;
    const bool doubleV = deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
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
    if (y1 > (ctx.sysClipV << doubleV) && y2 > (ctx.sysClipV << doubleV) && y3 > (ctx.sysClipV << doubleV) &&
        y4 > (ctx.sysClipV << doubleV)) {
        return true;
    }
    return false;
}

template <bool deinterlace>
FORCE_INLINE void VDPRenderer::VDP1PlotPixel(CoordS32 coord, const VDP1PixelParams &pixelParams,
                                             const VDP1GouraudParams &gouraudParams) {
    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;

    auto [x, y] = coord;

    if (pixelParams.mode.meshEnable && ((x ^ y) & 1)) {
        return;
    }

    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;
    const bool altFB = deinterlace && doubleDensity && (y & 1);
    if (doubleDensity) {
        if (!deinterlace && regs1.dblInterlaceEnable && (y & 1) == regs1.dblInterlaceDrawLine) {
            return;
        }
        if (deinterlace || regs1.dblInterlaceEnable) {
            y >>= 1;
        }
    }

    // Reject pixels outside of clipping area
    if (VDP1IsPixelSystemClipped<deinterlace>(coord)) {
        return;
    }
    if (pixelParams.mode.userClippingEnable) {
        // clippingMode = false -> draw inside, reject outside
        // clippingMode = true -> draw outside, reject inside
        // The function returns true if the pixel is clipped, therefore we want to reject pixels that return the
        // opposite of clippingMode on that function.
        if (VDP1IsPixelUserClipped<deinterlace>(coord) != pixelParams.mode.clippingMode) {
            return;
        }
    }

    // TODO: pixelParams.mode.preClippingDisable

    const uint32 fbOffset = y * regs1.fbSizeH + x;
    const uint8 fbIndex = vdpState.displayFB ^ 1;
    auto &drawFB = (altFB ? m_altSpriteFB : m_mainState.spriteFB)[fbIndex];
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

template <bool deinterlace>
FORCE_INLINE void VDPRenderer::VDP1PlotLine(CoordS32 coord1, CoordS32 coord2, const VDP1PixelParams &pixelParams,
                                            VDP1GouraudParams &gouraudParams) {
    for (LineStepper line{coord1, coord2}; line.CanStep(); line.Step()) {
        gouraudParams.U = line.FracPos();
        VDP1PlotPixel<deinterlace>(line.Coord(), pixelParams, gouraudParams);
        if (line.NeedsAntiAliasing()) {
            VDP1PlotPixel<deinterlace>(line.AACoord(), pixelParams, gouraudParams);
        }
    }
}

template <bool deinterlace>
void VDPRenderer::VDP1PlotTexturedLine(CoordS32 coord1, CoordS32 coord2, const VDP1TexturedLineParams &lineParams,
                                       VDP1GouraudParams &gouraudParams) {
    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;

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
            const uint32 adjustedU = useHighSpeedShrink ? ((u & ~1) | (uint32)regs1.evenOddCoordSelect) : u;

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

        VDP1PlotPixel<deinterlace>(line.Coord(), pixelParams, gouraudParams);
        if (line.NeedsAntiAliasing()) {
            VDP1PlotPixel<deinterlace>(line.AACoord(), pixelParams, gouraudParams);
        }
    }
}

template <bool deinterlace>
void VDPRenderer::VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control) {
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

    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;
    const bool doubleV = deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
    const CoordS32 coordA{lx, ty << doubleV};
    const CoordS32 coordB{rx, ty << doubleV};
    const CoordS32 coordC{rx, by << doubleV};
    const CoordS32 coordD{lx, by << doubleV};

    devlog::trace<grp::vdp1>("Draw normal sprite: {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} color={:04X} "
                             "gouraud={:04X} mode={:04X} size={:2d}x{:<2d} char={:X}",
                             lx, ty, rx, ty, rx, by, lx, by, color, gouraudTable, mode.u16, charSizeH, charSizeV,
                             charAddr);

    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
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
        VDP1PlotTexturedLine<deinterlace>(coordL, coordR, lineParams, gouraudParams);
    }
}

template <bool deinterlace>
void VDPRenderer::VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control) {
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

    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;
    const bool doubleV = deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
    const CoordS32 coordA{qxa, qya << doubleV};
    const CoordS32 coordB{qxb, qyb << doubleV};
    const CoordS32 coordC{qxc, qyc << doubleV};
    const CoordS32 coordD{qxd, qyd << doubleV};

    devlog::trace<grp::vdp1>("Draw scaled sprite: {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} color={:04X} "
                             "gouraud={:04X} mode={:04X} size={:2d}x{:<2d} char={:X}",
                             qxa, qya, qxb, qyb, qxc, qyc, qxd, qyd, color, gouraudTable, mode.u16, charSizeH,
                             charSizeV, charAddr);

    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
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
        VDP1PlotTexturedLine<deinterlace>(coordL, coordR, lineParams, gouraudParams);
    }
}

template <bool deinterlace>
void VDPRenderer::VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control) {
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

    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;
    const bool doubleV = deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
    const CoordS32 coordA{xa, ya << doubleV};
    const CoordS32 coordB{xb, yb << doubleV};
    const CoordS32 coordC{xc, yc << doubleV};
    const CoordS32 coordD{xd, yd << doubleV};

    devlog::trace<grp::vdp1>("Draw distorted sprite: {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} {:3d}x{:<3d} color={:04X} "
                             "gouraud={:04X} mode={:04X} size={:2d}x{:<2d} char={:X}",
                             xa, ya, xb, yb, xc, yc, xd, yd, color, gouraudTable, mode.u16, charSizeH, charSizeV,
                             charAddr);

    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
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
        VDP1PlotTexturedLine<deinterlace>(coordL, coordR, lineParams, gouraudParams);
    }
}

template <bool deinterlace>
void VDPRenderer::VDP1Cmd_DrawPolygon(uint32 cmdAddress) {
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

    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;
    const bool doubleV = deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
    const CoordS32 coordA{xa, ya << doubleV};
    const CoordS32 coordB{xb, yb << doubleV};
    const CoordS32 coordC{xc, yc << doubleV};
    const CoordS32 coordD{xd, yd << doubleV};

    devlog::trace<grp::vdp1>(
        "Draw polygon: {}x{} - {}x{} - {}x{} - {}x{}, color {:04X}, gouraud table {}, CMDPMOD = {:04X}", xa, ya, xb, yb,
        xc, yc, xd, yd, color, gouraudTable, mode.u16);

    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
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
        VDP1PlotLine<deinterlace>(coordL, coordR, pixelParams, gouraudParams);
    }
}

template <bool deinterlace>
void VDPRenderer::VDP1Cmd_DrawPolylines(uint32 cmdAddress) {
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

    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;
    const bool doubleV = deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
    const CoordS32 coordA{xa, ya << doubleV};
    const CoordS32 coordB{xb, yb << doubleV};
    const CoordS32 coordC{xc, yc << doubleV};
    const CoordS32 coordD{xd, yd << doubleV};

    devlog::trace<grp::vdp1>(
        "Draw polylines: {}x{} - {}x{} - {}x{} - {}x{}, color {:04X}, gouraud table {}, CMDPMOD = {:04X}", xa, ya, xb,
        yb, xc, yc, xd, yd, color, gouraudTable >> 3u, mode.u16);

    if (VDP1IsQuadSystemClipped<deinterlace>(coordA, coordB, coordC, coordD)) {
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

    VDP1PlotLine<deinterlace>(coordA, coordB, pixelParams, gouraudParamsAB);
    VDP1PlotLine<deinterlace>(coordB, coordC, pixelParams, gouraudParamsBC);
    VDP1PlotLine<deinterlace>(coordC, coordD, pixelParams, gouraudParamsCD);
    VDP1PlotLine<deinterlace>(coordD, coordA, pixelParams, gouraudParamsDA);
}

template <bool deinterlace>
void VDPRenderer::VDP1Cmd_DrawLine(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderContext;
    const VDP1Command::DrawMode mode{.u16 = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x04)};

    const uint16 color = VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x06);
    const sint32 xa = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    const sint32 ya = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    const sint32 xb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    const sint32 yb = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    const uint32 gouraudTable = static_cast<uint32>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x1C)) << 3u;

    const VDPState &vdpState = GetRendererVDP1State();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;
    const bool doubleV = deinterlace && regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity && !regs1.dblInterlaceEnable;
    const CoordS32 coordA{xa, ya << doubleV};
    const CoordS32 coordB{xb, yb << doubleV};

    devlog::trace<grp::vdp1>("Draw line: {}x{} - {}x{}, color {:04X}, gouraud table {}, CMDPMOD = {:04X}", xa, ya, xb,
                             yb, color, gouraudTable, mode.u16);

    if (VDP1IsLineSystemClipped<deinterlace>(coordA, coordB)) {
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

    VDP1PlotLine<deinterlace>(coordA, coordB, pixelParams, gouraudParams);
}

void VDPRenderer::VDP1Cmd_SetSystemClipping(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderContext;
    ctx.sysClipH = bit::extract<0, 9>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));
    ctx.sysClipV = bit::extract<0, 8>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));
    devlog::trace<grp::vdp1>("Set system clipping: {}x{}", ctx.sysClipH, ctx.sysClipV);
}

void VDPRenderer::VDP1Cmd_SetUserClipping(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderContext;
    ctx.userClipX0 = bit::extract<0, 9>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
    ctx.userClipY0 = bit::extract<0, 8>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
    ctx.userClipX1 = bit::extract<0, 9>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x14));
    ctx.userClipY1 = bit::extract<0, 8>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x16));
    devlog::trace<grp::vdp1>("Set user clipping: {}x{} - {}x{}", ctx.userClipX0, ctx.userClipY0, ctx.userClipX1,
                             ctx.userClipY1);
}

void VDPRenderer::VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress) {
    auto &ctx = m_VDP1RenderContext;
    ctx.localCoordX = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0C));
    ctx.localCoordY = bit::sign_extend<13>(VDP1ReadRendererVRAM<uint16>(cmdAddress + 0x0E));
    devlog::trace<grp::vdp1>("Set local coordinates: {}x{}", ctx.localCoordX, ctx.localCoordY);
}

// -----------------------------------------------------------------------------
// VDP2

template <uint32 index>
FORCE_INLINE void VDPRenderer::VDP2InitNormalBG() {
    static_assert(index < 4, "Invalid NBG index");

    if (!m_mainState.regs2.bgEnabled[index]) {
        return;
    }

    const BGParams &bgParams = m_mainState.regs2.bgParams[index + 1];
    NormBGLayerState &bgState = m_normBGLayerStates[index];
    bgState.fracScrollX = 0;
    bgState.fracScrollY = 0;
    if (!m_deinterlaceRender && m_mainState.regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity &&
        m_mainState.regs2.TVSTAT.ODD) {
        bgState.fracScrollY += bgParams.scrollIncV;
    }

    bgState.scrollIncH = bgParams.scrollIncH;
    bgState.mosaicCounterY = 0;
    if constexpr (index < 2) {
        bgState.lineScrollTableAddress = bgParams.lineScrollTableAddress;
    }
}

template <uint32 index>
FORCE_INLINE void VDPRenderer::VDP2InitRotationBG() {
    static_assert(index < 2, "Invalid RBG index");

    if (!m_mainState.regs2.bgEnabled[index + 4]) {
        return;
    }

    const BGParams &bgParams = m_mainState.regs2.bgParams[index];
    const bool cellSizeShift = bgParams.cellSizeShift;
    const bool twoWordChar = bgParams.twoWordChar;

    for (int param = 0; param < 2; param++) {
        const RotationParams &rotParam = m_mainState.regs2.rotParams[param];
        auto &pageBaseAddresses = m_rotParamStates[param].pageBaseAddresses;
        const uint16 plsz = rotParam.plsz;
        for (int plane = 0; plane < 16; plane++) {
            const uint32 mapIndex = rotParam.mapIndices[plane];
            pageBaseAddresses[plane] = CalcPageBaseAddress(cellSizeShift, twoWordChar, plsz, mapIndex);
        }
    }
}

void VDPRenderer::VDP2UpdateEnabledBGs() {
    // Sprite layer is always enabled, unless forcibly disabled
    m_layerStates[0].enabled = m_layerStates[0].rendered;

    if (m_mainState.regs2.bgEnabled[5]) {
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
        const ColorFormat colorFormatNBG0 = m_mainState.regs2.bgParams[1].colorFormat;
        const ColorFormat colorFormatNBG1 = m_mainState.regs2.bgParams[2].colorFormat;
        const bool disableNBG1 = colorFormatNBG0 == ColorFormat::RGB888;
        const bool disableNBG2 = colorFormatNBG0 == ColorFormat::Palette2048 ||
                                 colorFormatNBG0 == ColorFormat::RGB555 || colorFormatNBG0 == ColorFormat::RGB888;
        const bool disableNBG3 = colorFormatNBG0 == ColorFormat::RGB888 ||
                                 colorFormatNBG1 == ColorFormat::Palette2048 || colorFormatNBG1 == ColorFormat::RGB555;

        m_layerStates[1].enabled = m_layerStates[1].rendered && m_mainState.regs2.bgEnabled[4]; // RBG0
        m_layerStates[2].enabled = m_layerStates[2].rendered && m_mainState.regs2.bgEnabled[0]; // NBG0
        m_layerStates[3].enabled =
            m_layerStates[3].rendered && m_mainState.regs2.bgEnabled[1] && !disableNBG1; // NBG1/EXBG
        m_layerStates[4].enabled = m_layerStates[4].rendered && m_mainState.regs2.bgEnabled[2] && !disableNBG2; // NBG2
        m_layerStates[5].enabled = m_layerStates[5].rendered && m_mainState.regs2.bgEnabled[3] && !disableNBG3; // NBG3
    }
}

template <bool update>
FORCE_INLINE void VDPRenderer::VDP2UpdateLineScreenScroll(uint32 y, const BGParams &bgParams,
                                                          NormBGLayerState &bgState) {
    uint32 address = bgState.lineScrollTableAddress;
    auto read = [&] {
        const uint32 value = VDP2ReadRendererVRAM<uint32>(address);
        address += sizeof(uint32);
        return value;
    };

    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;
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
    if constexpr (update) {
        if (y > 0 && (y & ((1u << bgParams.lineScrollInterval) - 1)) == 0) {
            bgState.lineScrollTableAddress = address;
        }
    }
}

FORCE_INLINE void VDPRenderer::VDP2CalcRotationParameterTables(uint32 y) {
    VDPState &vdpState = GetRendererVDPState();
    VDP2Regs &regs = vdpState.regs2;

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
        t.ReadFrom(&vdpState.VRAM2[address & 0x7FFFF]);

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
        const uint32 maxX = m_mainState.HRes >> xShift;

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

template <bool deinterlace, bool altField>
FORCE_INLINE void VDPRenderer::VDP2CalcWindows(uint32 y) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

    y = VDP2GetY<deinterlace>(y) ^ altField;

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
FORCE_INLINE void VDPRenderer::VDP2CalcWindow(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
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
FORCE_INLINE void VDPRenderer::VDP2CalcWindowAnd(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                                                 const std::array<WindowParams, 2> &windowParams,
                                                 std::array<bool, kMaxResH> &windowState) {

    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

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
        if (regs.TVMD.HRESOn < 2) {
            startX >>= 1;
            endX >>= 1;
        }

        // Fill in horizontal coordinate
        if (inverted) {
            if (startX < windowState.size()) {
                endX = std::min<sint16>(endX, windowState.size() - 1);
                if (endX >= startX) {
                    std::fill(windowState.begin() + startX, windowState.begin() + endX + 1, false);
                }
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
            for (uint32 x = 0; x < m_mainState.HRes; x++) {
                windowState[x] &= m_spriteLayerState.attrs[x].shadowOrWindow != inverted;
            }
        }
    }
}

template <bool hasSpriteWindow>
FORCE_INLINE void VDPRenderer::VDP2CalcWindowOr(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                                                const std::array<WindowParams, 2> &windowParams,
                                                std::array<bool, kMaxResH> &windowState) {

    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

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
        if (regs.TVMD.HRESOn < 2) {
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
            for (uint32 x = 0; x < m_mainState.HRes; x++) {
                windowState[x] |= m_spriteLayerState.attrs[x].shadowOrWindow != inverted;
            }
        }
    }
}

FORCE_INLINE void VDPRenderer::VDP2CalcAccessCycles() {
    VDPState &vdpState = GetRendererVDPState();
    VDP2Regs &regs = vdpState.regs2;

    if (!regs.bgEnabled[5]) {
        // Translate VRAM access cycles for vertical cell scroll data into increment and offset for NBG0 and NBG1.
        //
        // Some games set up "illegal" access patterns which we have to honor. This is an approximation of the real
        // thing, since this VDP emulator does not actually perform the accesses described by the CYCxn registers.

        if (regs.cyclePatterns.dirty) [[unlikely]] {
            regs.cyclePatterns.dirty = false;

            m_vertCellScrollInc = 0;
            uint32 vcellAccessOffset = 0;

            // Update cycle accesses
            for (uint32 bank = 0; bank < 4; ++bank) {
                for (auto access : regs.cyclePatterns.timings[bank]) {
                    switch (access) {
                    case CyclePatterns::VCellScrollNBG0:
                        m_vertCellScrollInc += sizeof(uint32);
                        m_normBGLayerStates[0].vertCellScrollOffset = vcellAccessOffset;
                        vcellAccessOffset += sizeof(uint32);
                        break;
                    case CyclePatterns::VCellScrollNBG1:
                        m_vertCellScrollInc += sizeof(uint32);
                        m_normBGLayerStates[1].vertCellScrollOffset = vcellAccessOffset;
                        vcellAccessOffset += sizeof(uint32);
                        break;
                    default: break;
                    }
                }
            }
        }
    }
}

template <bool deinterlace>
void VDPRenderer::VDP2DrawLine(uint32 y) {
    devlog::trace<grp::vdp2>("Drawing line {}", y);

    const VDPState &vdpState = GetRendererVDPState();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;

    // If starting a new frame, compute access cycles
    if (y == 0) {
        VDP2CalcAccessCycles();
    }

    using FnDrawLayer = void (VDPRenderer::*)(uint32 y);

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
            arr[cmIndex][rotate][altFieldIndex] = &VDPRenderer::VDP2DrawSpriteLayer<colorMode, rotate, altField>;
        });

        return arr;
    }();

    const uint32 colorMode = regs2.vramControl.colorRAMMode;
    const bool rotate = regs1.fbRotEnable;
    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;

    // Precalculate window state
    VDP2CalcWindows<deinterlace, false>(y);

    // Load rotation parameters if any of the RBG layers is enabled
    if (regs2.bgEnabled[4] || regs2.bgEnabled[5]) {
        VDP2CalcRotationParameterTables(y);
    }

    // Draw line color and back screen layers
    VDP2DrawLineColorAndBackScreens(y);

    // Draw sprite layer
    (this->*fnDrawSprite[colorMode][rotate][false])(y);

    // Draw background layers
    if (regs2.bgEnabled[5]) {
        VDP2DrawRotationBG<0>(y, colorMode); // RBG0
        VDP2DrawRotationBG<1>(y, colorMode); // RBG1
    } else {
        VDP2DrawRotationBG<0>(y, colorMode); // RBG0
        if (doubleDensity) {
            VDP2DrawNormalBG<0, deinterlace, false>(y, colorMode); // NBG0
            VDP2DrawNormalBG<1, deinterlace, false>(y, colorMode); // NBG1
            VDP2DrawNormalBG<2, deinterlace, false>(y, colorMode); // NBG2
            VDP2DrawNormalBG<3, deinterlace, false>(y, colorMode); // NBG3
        } else {
            VDP2DrawNormalBG<0, false, false>(y, colorMode); // NBG0
            VDP2DrawNormalBG<1, false, false>(y, colorMode); // NBG1
            VDP2DrawNormalBG<2, false, false>(y, colorMode); // NBG2
            VDP2DrawNormalBG<3, false, false>(y, colorMode); // NBG3
        }
    }

    // Compose image
    VDP2ComposeLine<deinterlace, false>(y);

    // Draw complementary field if deinterlace is enabled while in double-density interlace mode
    if constexpr (deinterlace) {
        if (doubleDensity) {
            // Precalculate window state
            VDP2CalcWindows<true, true>(y);

            // Draw sprite layer
            (this->*fnDrawSprite[colorMode][rotate][true])(y);

            // Draw background layers
            if (regs2.bgEnabled[5]) {
                VDP2DrawRotationBG<0>(y, colorMode); // RBG0
                VDP2DrawRotationBG<1>(y, colorMode); // RBG1
            } else {
                VDP2DrawRotationBG<0>(y, colorMode);           // RBG0
                VDP2DrawNormalBG<0, true, true>(y, colorMode); // NBG0
                VDP2DrawNormalBG<1, true, true>(y, colorMode); // NBG1
                VDP2DrawNormalBG<2, true, true>(y, colorMode); // NBG2
                VDP2DrawNormalBG<3, true, true>(y, colorMode); // NBG3
            }

            // Compose image
            VDP2ComposeLine<true, true>(y);
        }
    }
}

FORCE_INLINE void VDPRenderer::VDP2DrawLineColorAndBackScreens(uint32 y) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

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

template <uint32 colorMode, bool rotate, bool altField>
NO_INLINE void VDPRenderer::VDP2DrawSpriteLayer(uint32 y) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;

    // VDP1 scaling:
    // 2x horz: VDP1 TVM=000 and VDP2 HRESO=01x
    const bool doubleResH =
        !regs1.hdtvEnable && !regs1.fbRotEnable && !regs1.pixel8Bits && (regs2.TVMD.HRESOn & 0b110) == 0b010;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 maxX = m_mainState.HRes >> xShift;

    auto &layerState = m_layerStates[0];
    auto &spriteLayerState = m_spriteLayerState;

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;

        const uint8 fbIndex = vdpState.displayFB;
        const auto &spriteFB = altField ? m_altSpriteFB[fbIndex] : m_mainState.spriteFB[fbIndex];
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
        auto &attr = spriteLayerState.attrs[xx];

        util::ScopeGuard sgDoublePixel{[&] {
            if (doubleResH) {
                auto pixel = layerState.pixels.GetPixel(xx);
                layerState.pixels.SetPixel(xx + 1, pixel);
                spriteLayerState.attrs[xx + 1] = attr;
            }
        }};

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
                        layerState.pixels.transparent[xx] = true;
                        continue;
                    }
                } else if (params.type >= 2) {
                    if (params.spriteWindowEnable && bit::extract<0, 14>(spriteDataValue) == 0) {
                        layerState.pixels.transparent[xx] = true;
                        continue;
                    }
                }

                layerState.pixels.color[xx] = ConvertRGB555to888(Color555{spriteDataValue});
                layerState.pixels.transparent[xx] = false;
                layerState.pixels.priority[xx] = params.priorities[0];

                attr.colorCalcRatio = params.colorCalcRatios[0];
                attr.shadowOrWindow = false;
                attr.normalShadow = false;
                continue;
            }
        }

        // Palette data
        const SpriteData spriteData = VDP2FetchSpriteData<altField>(spriteFBOffset);
        const uint32 colorIndex = params.colorDataOffset + spriteData.colorData;
        layerState.pixels.color[xx] = VDP2FetchCRAMColor<colorMode>(0, colorIndex);
        layerState.pixels.transparent[xx] = spriteData.colorData == 0;
        layerState.pixels.priority[xx] = params.priorities[spriteData.priority];

        attr.colorCalcRatio = params.colorCalcRatios[spriteData.colorCalcRatio];
        attr.shadowOrWindow = spriteData.shadowOrWindow;
        attr.normalShadow = spriteData.normalShadow;
    }
}

template <uint32 bgIndex, bool deinterlace, bool altField>
FORCE_INLINE void VDPRenderer::VDP2DrawNormalBG(uint32 y, uint32 colorMode) {
    static_assert(bgIndex < 4, "Invalid NBG index");

    using FnDraw = void (VDPRenderer::*)(uint32 y, const BGParams &, LayerState &, NormBGLayerState &,
                                         const std::array<bool, kMaxResH> &);

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
            arr[chm][fcc][cf][clm] = &VDPRenderer::VDP2DrawNormalScrollBG<chmEnum, fcc, cfEnum, colorMode, deinterlace>;
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
            arr[cf][cm] = &VDPRenderer::VDP2DrawNormalBitmapBG<cfEnum, colorMode, deinterlace>;
        });

        return arr;
    }();

    if (!m_layerStates[bgIndex + 2].enabled) {
        return;
    }

    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;
    const BGParams &bgParams = regs.bgParams[bgIndex + 1];
    LayerState &layerState = m_layerStates[bgIndex + 2];
    NormBGLayerState &bgState = m_normBGLayerStates[bgIndex];
    const auto &windowState = m_bgWindows[bgIndex + 1];

    if constexpr (bgIndex < 2) {
        VDP2UpdateLineScreenScroll<!deinterlace || altField>(y, bgParams, bgState);
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
FORCE_INLINE void VDPRenderer::VDP2DrawRotationBG(uint32 y, uint32 colorMode) {
    static_assert(bgIndex < 2, "Invalid RBG index");

    static constexpr bool selRotParam = bgIndex == 0;

    using FnDraw = void (VDPRenderer::*)(uint32 y, const BGParams &, LayerState &, const std::array<bool, kMaxResH> &);

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
            arr[chm][fcc][cf][clm] =
                &VDPRenderer::VDP2DrawRotationScrollBG<selRotParam, chmEnum, fcc, cfEnum, colorMode>;
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
            arr[cf][cm] = &VDPRenderer::VDP2DrawRotationBitmapBG<selRotParam, cfEnum, colorMode>;
        });

        return arr;
    }();

    if (!m_layerStates[bgIndex + 1].enabled) {
        return;
    }

    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;
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

FORCE_INLINE void Color888AverageMasked(const std::span<Color888> dest, const std::span<const bool, kMaxResH> mask,
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

template <bool deinterlace, bool altField>
FORCE_INLINE void VDPRenderer::VDP2ComposeLine(uint32 y) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;
    const auto &colorCalcParams = regs.colorCalcParams;

    y = VDP2GetY<deinterlace>(y) ^ altField;

    if (!regs.TVMD.DISP) {
        std::fill_n(&m_framebuffer[y * m_mainState.HRes], m_mainState.HRes, 0xFF000000);
        return;
    }

    // Determine layer orders
    static constexpr std::array<LayerIndex, 3> kLayersInit{LYR_Back, LYR_Back, LYR_Back};
    alignas(16) std::array<std::array<LayerIndex, 3>, kMaxResH> scanline_layers;
    std::fill_n(scanline_layers.begin(), m_mainState.HRes, kLayersInit);

    static constexpr std::array<uint8, 3> kLayerPriosInit{0, 0, 0};
    alignas(16) std::array<std::array<uint8, 3>, kMaxResH> scanline_layerPrios;
    std::fill_n(scanline_layerPrios.begin(), m_mainState.HRes, kLayerPriosInit);

    for (int layer = 0; layer < m_layerStates.size(); layer++) {
        const LayerState &state = m_layerStates[layer];
        if (!state.enabled) {
            continue;
        }

        if (AllBool(std::span{state.pixels.transparent}.first(m_mainState.HRes))) {
            // All pixels are transparent
            continue;
        }

        if (AllZeroU8(std::span{state.pixels.priority}.first(m_mainState.HRes))) {
            // All priorities are zero
            continue;
        }

        for (uint32 x = 0; x < m_mainState.HRes; x++) {
            if (state.pixels.transparent[x]) {
                continue;
            }
            const uint8 priority = state.pixels.priority[x];
            if (priority == 0) {
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

    // Retrieves the color of the given layer
    auto getLayerColor = [&](LayerIndex layer, uint32 x) -> Color888 {
        if (layer == LYR_Back) {
            return m_lineBackLayerState.backColor;
        } else {
            return m_layerStates[layer].pixels.color[x];
        }
    };

    // Gather pixels for layer 0
    alignas(16) std::array<Color888, kMaxResH> layer0Pixels;
    for (uint32 x = 0; x < m_mainState.HRes; x++) {
        layer0Pixels[x] = getLayerColor(scanline_layers[x][0], x);
    }

    const auto isColorCalcEnabled = [&](LayerIndex layer, uint32 x) {
        if (layer == LYR_Sprite) {
            const SpriteParams &spriteParams = regs.spriteParams;
            if (!spriteParams.colorCalcEnable) {
                return false;
            }

            const uint8 pixelPriority = m_layerStates[LYR_Sprite].pixels.priority[x];

            using enum SpriteColorCalculationCondition;
            switch (spriteParams.colorCalcCond) {
            case PriorityLessThanOrEqual: return pixelPriority <= spriteParams.colorCalcValue;
            case PriorityEqual: return pixelPriority == spriteParams.colorCalcValue;
            case PriorityGreaterThanOrEqual: return pixelPriority >= spriteParams.colorCalcValue;
            case MsbEqualsOne: return m_layerStates[LYR_Sprite].pixels.color[x].msb == 1;
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

    for (uint32 x = 0; x < m_mainState.HRes; x++) {
        const LayerIndex layer = scanline_layers[x][0];
        if (m_colorCalcWindow[x]) {
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
        default: layer0ColorCalcEnabled[x] = m_layerStates[layer].pixels.specialColorCalc[x]; break;
        }
    }

    const std::span<Color888> framebufferOutput(reinterpret_cast<Color888 *>(&m_framebuffer[y * m_mainState.HRes]),
                                                m_mainState.HRes);

    if (AnyBool(std::span{layer0ColorCalcEnabled}.first(m_mainState.HRes))) {
        // Gather pixels for layer 1
        alignas(16) std::array<Color888, kMaxResH> layer1Pixels;
        for (uint32 x = 0; x < m_mainState.HRes; x++) {
            layer1Pixels[x] = getLayerColor(scanline_layers[x][1], x);
        }

        // Extended color calculations (only in normal TV modes)
        const bool useExtendedColorCalc = colorCalcParams.extendedColorCalcEnable && regs.TVMD.HRESOn < 2;

        // Gather line-color data
        alignas(16) std::array<bool, kMaxResH> layer0LineColorEnabled;
        alignas(16) std::array<Color888, kMaxResH> layer0LineColors;
        for (uint32 x = 0; x < m_mainState.HRes; x++) {
            const LayerIndex layer = scanline_layers[x][0];

            switch (layer) {
            case LYR_Sprite: layer0LineColorEnabled[x] = regs.spriteParams.lineColorScreenEnable; break;
            case LYR_Back: layer0LineColorEnabled[x] = false; break;
            default: layer0LineColorEnabled[x] = regs.bgParams[layer - LYR_RBG0].lineColorScreenEnable; break;
            }

            if (layer0LineColorEnabled[x]) {
                if (layer == LYR_RBG0 || (layer == LYR_NBG0_RBG1 && regs.bgEnabled[5])) {
                    const auto &rotParams = regs.rotParams[layer - LYR_RBG0];
                    if (rotParams.coeffTableEnable && rotParams.coeffUseLineColorData) {
                        layer0LineColors[x] = m_rotParamStates[layer - LYR_RBG0].lineColor[x];
                    } else {
                        layer0LineColors[x] = m_lineBackLayerState.lineColor;
                    }
                } else {
                    layer0LineColors[x] = m_lineBackLayerState.lineColor;
                }
            }
        }

        // Apply extended color calculations to layer 1
        if (useExtendedColorCalc) {
            alignas(16) std::array<bool, kMaxResH> layer1ColorCalcEnabled;
            alignas(16) std::array<Color888, kMaxResH> layer2Pixels;

            // Gather pixels for layer 2
            for (uint32 x = 0; x < m_mainState.HRes; x++) {
                layer1ColorCalcEnabled[x] = isColorCalcEnabled(scanline_layers[x][1], x);
                if (layer1ColorCalcEnabled[x]) {
                    layer2Pixels[x] = getLayerColor(scanline_layers[x][2], x);
                }
            }

            // TODO: honor color RAM mode + palette/RGB format restrictions
            // - modes 1 and 2 don't blend layers if the bottom layer uses palette color
            // HACK: assuming color RAM mode 0 for now (aka no restrictions)
            Color888AverageMasked(std::span{layer1Pixels}.first(m_mainState.HRes), layer1ColorCalcEnabled, layer1Pixels,
                                  layer2Pixels);

            // Blend line color if top layer uses it
            Color888AverageMasked(std::span{layer1Pixels}.first(m_mainState.HRes), layer0LineColorEnabled, layer1Pixels,
                                  layer0LineColors);
        } else {
            // Alpha composite
            Color888CompositeRatioMasked(std::span{layer1Pixels}.first(m_mainState.HRes), layer0LineColorEnabled,
                                         layer1Pixels, layer0LineColors, regs.lineScreenParams.colorCalcRatio);
        }

        // Blend layer 0 and layer 1
        if (colorCalcParams.useAdditiveBlend) {
            // Saturated add
            Color888SatAddMasked(framebufferOutput, layer0ColorCalcEnabled, layer0Pixels, layer1Pixels);
        } else {
            // Gather extended color ratio info
            alignas(16) std::array<uint8, kMaxResH> scanline_ratio;
            for (uint32 x = 0; x < m_mainState.HRes; x++) {
                if (!layer0ColorCalcEnabled[x]) {
                    scanline_ratio[x] = 0;
                    continue;
                }

                const LayerIndex layer = scanline_layers[x][colorCalcParams.useSecondScreenRatio];
                switch (layer) {
                case LYR_Sprite: scanline_ratio[x] = m_spriteLayerState.attrs[x].colorCalcRatio; break;
                case LYR_Back: scanline_ratio[x] = regs.backScreenParams.colorCalcRatio; break;
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

    // Gather shadow data
    alignas(16) std::array<bool, kMaxResH> layer0ShadowEnabled;
    for (uint32 x = 0; x < m_mainState.HRes; x++) {
        const LayerIndex layer = scanline_layers[x][0];

        const bool isNormalShadow = m_spriteLayerState.attrs[x].normalShadow;
        const bool isMSBShadow = !regs.spriteParams.spriteWindowEnable && m_spriteLayerState.attrs[x].shadowOrWindow;
        if (!isNormalShadow && !isMSBShadow) {
            layer0ShadowEnabled[x] = false;
            continue;
        }

        switch (layer) {
        case LYR_Sprite: layer0ShadowEnabled[x] = m_spriteLayerState.attrs[x].shadowOrWindow; break;
        case LYR_Back: layer0ShadowEnabled[x] = regs.backScreenParams.shadowEnable; break;
        default: layer0ShadowEnabled[x] = regs.bgParams[layer - LYR_RBG0].shadowEnable; break;
        }
    }

    // Apply sprite shadow
    if (AnyBool(std::span{layer0ShadowEnabled}.first(m_mainState.HRes))) {
        Color888ShadowMasked(framebufferOutput, layer0ShadowEnabled);
    }

    // Gather color offset info
    alignas(16) std::array<bool, kMaxResH> layer0ColorOffsetEnabled;
    for (uint32 x = 0; x < m_mainState.HRes; x++) {
        layer0ColorOffsetEnabled[x] = regs.colorOffsetEnable[scanline_layers[x][0]];
    }

    // Apply color offset if enabled
    if (AnyBool(std::span{layer0ColorOffsetEnabled}.first(m_mainState.HRes))) {
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

template <VDPRenderer::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode,
          bool deinterlace>
NO_INLINE void VDPRenderer::VDP2DrawNormalScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                                   NormBGLayerState &bgState,
                                                   const std::array<bool, kMaxResH> &windowState) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

    uint32 fracScrollX = bgState.fracScrollX + bgParams.scrollAmountH;
    const uint32 fracScrollY = bgState.fracScrollY + bgParams.scrollAmountV;
    bgState.fracScrollY += bgParams.scrollIncV;
    if (!deinterlace && regs.TVMD.LSMDn == InterlaceMode::DoubleDensity) {
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

    for (uint32 x = 0; x < m_mainState.HRes; x++) {
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
                layerState.pixels.SetPixel(x, layerState.pixels.GetPixel(x - 1));

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
            layerState.pixels.transparent[x] = true;
        } else {
            // Compute integer scroll screen coordinates
            const uint32 scrollX = fracScrollX >> 8u;
            const uint32 scrollY = ((fracScrollY + cellScrollY) >> 8u) - bgState.mosaicCounterY;
            const CoordU32 scrollCoord{scrollX, scrollY};

            // Plot pixel
            layerState.pixels.SetPixel(
                x, VDP2FetchScrollBGPixel<false, charMode, fourCellChar, colorFormat, colorMode>(
                       bgParams, bgParams.pageBaseAddresses, bgParams.pageShiftH, bgParams.pageShiftV, scrollCoord));
        }

        // Increment horizontal coordinate
        fracScrollX += bgState.scrollIncH;
    }
}

template <ColorFormat colorFormat, uint32 colorMode, bool deinterlace>
NO_INLINE void VDPRenderer::VDP2DrawNormalBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                                   NormBGLayerState &bgState,
                                                   const std::array<bool, kMaxResH> &windowState) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

    uint32 fracScrollX = bgState.fracScrollX + bgParams.scrollAmountH;
    const uint32 fracScrollY = bgState.fracScrollY + bgParams.scrollAmountV;
    bgState.fracScrollY += bgParams.scrollIncV;
    if (!deinterlace && regs.TVMD.LSMDn == InterlaceMode::DoubleDensity) {
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

    for (uint32 x = 0; x < m_mainState.HRes; x++) {
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
                layerState.pixels.SetPixel(x, layerState.pixels.GetPixel(x - 1));

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
            layerState.pixels.transparent[x] = true;
        } else {
            // Compute integer scroll screen coordinates
            const uint32 scrollX = fracScrollX >> 8u;
            const uint32 scrollY = ((fracScrollY + cellScrollY) >> 8u) - bgState.mosaicCounterY;
            const CoordU32 scrollCoord{scrollX, scrollY};

            // Plot pixel
            layerState.pixels.SetPixel(
                x, VDP2FetchBitmapPixel<colorFormat, colorMode>(bgParams, bgParams.bitmapBaseAddress, scrollCoord));
        }

        // Increment horizontal coordinate
        fracScrollX += bgState.scrollIncH;
    }
}

template <bool selRotParam, VDPRenderer::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat,
          uint32 colorMode>
NO_INLINE void VDPRenderer::VDP2DrawRotationScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                                     const std::array<bool, kMaxResH> &windowState) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

    const bool doubleResH = regs.TVMD.HRESOn & 0b010;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 maxX = m_mainState.HRes >> xShift;

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;
        util::ScopeGuard sgDoublePixel{[&] {
            if (doubleResH) {
                const Pixel pixel = layerState.pixels.GetPixel(xx);
                layerState.pixels.SetPixel(xx + 1, pixel);
            }
        }};

        const RotParamSelector rotParamSelector = selRotParam ? VDP2SelectRotationParameter(x, y) : RotParamA;

        const RotationParams &rotParams = regs.rotParams[rotParamSelector];
        const RotationParamState &rotParamState = m_rotParamStates[rotParamSelector];

        // Handle transparent pixels in coefficient table
        if (rotParams.coeffTableEnable && rotParamState.transparent[x]) {
            layerState.pixels.transparent[xx] = true;
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
            layerState.pixels.transparent[xx] = true;
        } else if ((scrollX < maxScrollX && scrollY < maxScrollY) || usingRepeat) {
            // Plot pixel
            layerState.pixels.SetPixel(xx, VDP2FetchScrollBGPixel<true, charMode, fourCellChar, colorFormat, colorMode>(
                                               bgParams, rotParamState.pageBaseAddresses, rotParams.pageShiftH,
                                               rotParams.pageShiftV, scrollCoord));
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
            layerState.pixels.SetPixel(xx, VDP2FetchCharacterPixel<colorFormat, colorMode>(bgParams, ch, dotCoord, 0));
        } else {
            // Out of bounds - transparent
            layerState.pixels.transparent[xx] = true;
        }
    }
}

template <bool selRotParam, ColorFormat colorFormat, uint32 colorMode>
NO_INLINE void VDPRenderer::VDP2DrawRotationBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                                     const std::array<bool, kMaxResH> &windowState) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

    const bool doubleResH = regs.TVMD.HRESOn & 0b010;
    const uint32 xShift = doubleResH ? 1 : 0;
    const uint32 maxX = m_mainState.HRes >> xShift;

    for (uint32 x = 0; x < maxX; x++) {
        const uint32 xx = x << xShift;
        util::ScopeGuard sgDoublePixel{[&] {
            if (doubleResH) {
                const Pixel pixel = layerState.pixels.GetPixel(xx);
                layerState.pixels.SetPixel(xx + 1, pixel);
            }
        }};
        const RotParamSelector rotParamSelector = selRotParam ? VDP2SelectRotationParameter(x, y) : RotParamA;

        const RotationParams &rotParams = regs.rotParams[rotParamSelector];
        const RotationParamState &rotParamState = m_rotParamStates[rotParamSelector];

        // Handle transparent pixels in coefficient table
        if (rotParams.coeffTableEnable && rotParamState.transparent[x]) {
            layerState.pixels.transparent[xx] = true;
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
            layerState.pixels.transparent[xx] = true;
        } else if ((scrollX < maxScrollX && scrollY < maxScrollY) || usingRepeat) {
            // Plot pixel
            layerState.pixels.SetPixel(
                xx, VDP2FetchBitmapPixel<colorFormat, colorMode>(bgParams, rotParams.bitmapBaseAddress, scrollCoord));
        } else {
            // Out of bounds and no repeat
            layerState.pixels.transparent[xx] = true;
        }
    }
}

FORCE_INLINE VDPRenderer::RotParamSelector VDPRenderer::VDP2SelectRotationParameter(uint32 x, uint32 y) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

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

FORCE_INLINE bool VDPRenderer::VDP2CanFetchCoefficient(const RotationParams &params, uint32 coeffAddress) const {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

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

FORCE_INLINE Coefficient VDPRenderer::VDP2FetchRotationCoefficient(const RotationParams &params, uint32 coeffAddress) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

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
template <bool rot, VDPRenderer::CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode>
FORCE_INLINE VDPRenderer::Pixel
VDPRenderer::VDP2FetchScrollBGPixel(const BGParams &bgParams, std::span<const uint32> pageBaseAddresses,
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

FORCE_INLINE VDPRenderer::Character VDPRenderer::VDP2FetchTwoWordCharacter(uint32 pageBaseAddress, uint32 charIndex) {
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
FORCE_INLINE VDPRenderer::Character VDPRenderer::VDP2FetchOneWordCharacter(const BGParams &bgParams,
                                                                           uint32 pageBaseAddress, uint32 charIndex) {
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
FORCE_INLINE VDPRenderer::Pixel VDPRenderer::VDP2FetchCharacterPixel(const BGParams &bgParams, Character ch,
                                                                     CoordU32 dotCoord, uint32 cellIndex) {
    static_assert(static_cast<uint32>(colorFormat) <= 4, "Invalid xxCHCN value");

    const VDPState &state = GetRendererVDPState();
    const VDP2Regs &regs = state.regs2;

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
        const uint8 dotData = (VDP2ReadRendererVRAM<uint8>(dotAddress) >> ((~dotX & 1) * 4)) & 0xF;
        const uint32 colorIndex = (ch.palNum << 4u) | dotData;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData, pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::Palette256) {
        const uint32 dotAddress = cellAddress + dotOffset;
        const uint8 dotData = VDP2ReadRendererVRAM<uint8>(dotAddress);
        const uint32 colorIndex = ((ch.palNum & 0x70) << 4u) | dotData;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData, pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::Palette2048) {
        const uint32 dotAddress = cellAddress + dotOffset * sizeof(uint16);
        const uint16 dotData = VDP2ReadRendererVRAM<uint16>(dotAddress);
        const uint32 colorIndex = dotData & 0x7FF;
        colorData = bit::extract<1, 3>(dotData);
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && (dotData & 0x7FF) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(colorData, pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::RGB555) {
        const uint32 dotAddress = cellAddress + dotOffset * sizeof(uint16);
        const uint16 dotData = VDP2ReadRendererVRAM<uint16>(dotAddress);
        pixel.color = ConvertRGB555to888(Color555{.u16 = dotData});
        pixel.transparent = bgParams.enableTransparency && bit::extract<15>(dotData) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(0b111, true);

    } else if constexpr (colorFormat == ColorFormat::RGB888) {
        const uint32 dotAddress = cellAddress + dotOffset * sizeof(uint32);
        const uint32 dotData = VDP2ReadRendererVRAM<uint32>(dotAddress);
        pixel.color.u32 = dotData;
        pixel.transparent = bgParams.enableTransparency && bit::extract<31>(dotData) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(0b111, true);
    }

    // Compute priority
    pixel.priority = bgParams.priorityNumber;
    if (bgParams.priorityMode == PriorityMode::PerCharacter) {
        pixel.priority &= ~1;
        pixel.priority |= (uint8)ch.specPriority;
    } else if (bgParams.priorityMode == PriorityMode::PerDot) {
        if constexpr (IsPaletteColorFormat(colorFormat)) {
            pixel.priority &= ~1;
            pixel.priority |= static_cast<uint8>(specFuncCode.colorMatches[colorData]);
        }
    }

    return pixel;
}

template <ColorFormat colorFormat, uint32 colorMode>
FORCE_INLINE VDPRenderer::Pixel VDPRenderer::VDP2FetchBitmapPixel(const BGParams &bgParams, uint32 bitmapBaseAddress,
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
        pixel.specialColorCalc = getSpecialColorCalcFlag(pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::Palette256) {
        const uint32 dotAddress = bitmapBaseAddress + dotOffset;
        const uint8 dotData = VDP2ReadRendererVRAM<uint8>(dotAddress);
        const uint32 colorIndex = palNum | dotData;
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && dotData == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(pixel.color.msb);

    } else if constexpr (colorFormat == ColorFormat::Palette2048) {
        const uint32 dotAddress = bitmapBaseAddress + dotOffset * sizeof(uint16);
        const uint16 dotData = VDP2ReadRendererVRAM<uint16>(dotAddress);
        const uint32 colorIndex = dotData & 0x7FF;
        pixel.color = VDP2FetchCRAMColor<colorMode>(bgParams.cramOffset, colorIndex);
        pixel.transparent = bgParams.enableTransparency && (dotData & 0x7FF) == 0;
        pixel.specialColorCalc = getSpecialColorCalcFlag(pixel.color.msb);

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
FORCE_INLINE Color888 VDPRenderer::VDP2FetchCRAMColor(uint32 cramOffset, uint32 colorIndex) {
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

template <bool altField>
FLATTEN FORCE_INLINE SpriteData VDPRenderer::VDP2FetchSpriteData(uint32 fbOffset) {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP1Regs &regs1 = vdpState.regs1;
    const VDP2Regs &regs2 = vdpState.regs2;

    const uint8 type = regs2.spriteParams.type;
    if (type < 8) {
        return VDP2FetchWordSpriteData<altField>(fbOffset * sizeof(uint16), type);
    } else {
        // Adjust the offset if VDP1 used 16-bit data.
        // The majority of games actually set these two parameters properly, but there's *always* an exception...
        if (!regs1.pixel8Bits) {
            fbOffset = fbOffset * sizeof(uint16) + 1;
        }
        return VDP2FetchByteSpriteData<altField>(fbOffset, type);
    }
}

template <bool altField>
FORCE_INLINE SpriteData VDPRenderer::VDP2FetchWordSpriteData(uint32 fbOffset, uint8 type) {
    assert(type < 8);

    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

    const uint8 fbIndex = vdpState.displayFB;
    auto &fb = altField ? m_altSpriteFB[fbIndex] : m_mainState.spriteFB[fbIndex];
    const uint16 rawData = util::ReadBE<uint16>(&fb[fbOffset & 0x3FFFE]);

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

template <bool altField>
FORCE_INLINE SpriteData VDPRenderer::VDP2FetchByteSpriteData(uint32 fbOffset, uint8 type) {
    assert(type >= 8);

    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

    const uint8 fbIndex = vdpState.displayFB;
    auto &fb = altField ? m_altSpriteFB[fbIndex] : m_mainState.spriteFB[fbIndex];
    const uint8 rawData = fb[fbOffset & 0x3FFFF];

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
FORCE_INLINE bool VDPRenderer::VDP2IsNormalShadow(uint16 colorData) {
    // Check against normal shadow pattern (LSB = 0, rest of the bits = 1)
    static constexpr uint16 kNormalShadowValue = ~(~0 << (colorDataBits + 1)) & ~1;
    return (colorData == kNormalShadowValue);
}

template <bool deinterlace>
FORCE_INLINE uint32 VDPRenderer::VDP2GetY(uint32 y) const {
    const VDPState &vdpState = GetRendererVDPState();
    const VDP2Regs &regs = vdpState.regs2;

    if (regs.TVMD.LSMDn == InterlaceMode::DoubleDensity) {
        return (y << 1) | (regs.TVSTAT.ODD & !deinterlace);
    } else {
        return y;
    }
}

// -----------------------------------------------------------------------------
// Threading

void VDPRenderer::EnableThreadedVDP(bool enable) {
    if (m_threadedRendering == enable) {
        return;
    }

    devlog::debug<grp::vdp2>("{} threaded VDP rendering", (enable ? "Enabling" : "Disabling"));

    m_threadedRendering = enable;
    if (enable) {
        EnqueueEvent(RenderEvent::UpdateEffectiveRenderingFlags());
        EnqueueEvent(RenderEvent::PostLoadStateSync());
        m_renderThread = std::thread{[&] { RenderThread(); }};
        m_postLoadSyncSignal.Wait(true);
    } else {
        EnqueueEvent(RenderEvent::Shutdown());
        if (m_renderThread.joinable()) {
            m_renderThread.join();
        }
    }
}

void VDPRenderer::IncludeVDP1RenderInVDPThread(bool enable) {
    if (m_threadedRendering) {
        m_renderVDP1OnVDP2Thread = enable;
        EnqueueEvent(RenderEvent::UpdateEffectiveRenderingFlags());
        EnqueueEvent(RenderEvent::VDP1StateSync());
        m_postLoadSyncSignal.Wait(true);
    }
}

void VDPRenderer::UpdateEffectiveRenderingFlags() {
    m_effectiveRenderVDP1InVDP2Thread = m_threadedRendering && m_renderVDP1OnVDP2Thread;
}

void VDPRenderer::RenderThread() {
    util::SetCurrentThreadName("VDP render thread");

    std::array<RenderEvent, 64> events{};

    bool running = true;
    while (running) {
        const size_t count = DequeueEvents(events.begin(), events.size());

        for (size_t i = 0; i < count; ++i) {
            auto &event = events[i];
            using EvtType = RenderEvent::Type;
            switch (event.type) {
            case EvtType::Reset:
                if (event.reset.hard) {
                    m_CRAMCache.fill({});
                }

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

                m_localState.Reset(event.reset.hard);
                m_framebuffer.fill(0xFF000000);
                break;
            case EvtType::OddField: m_localState.regs2.TVSTAT.ODD = event.oddField.odd; break;
            case EvtType::VDP1EraseFramebuffer:
                if (m_effectiveRenderVDP1InVDP2Thread) {
                    VDP1EraseFramebuffer();
                } else {
                    m_eraseFramebufferReadySignal.Set();
                }
                break;
            case EvtType::VDP1SwapFramebuffer:
                m_localState.displayFB ^= 1;
                m_framebufferSwapSignal.Set();
                break;
            case EvtType::VDP1BeginFrame:
                m_vdp1Done = false;
                if (m_deinterlaceRender) {
                    for (int i = 0; i < 10000 && m_VDP1RenderContext.rendering; i++) {
                        VDP1ProcessCommand<true>();
                    }
                } else {
                    for (int i = 0; i < 10000 && m_VDP1RenderContext.rendering; i++) {
                        VDP1ProcessCommand<false>();
                    }
                }
                break;
            /*case EvtType::VDP1ProcessCommands:
                for (uint64 i = 0; i < event.processCommands.steps; i++) {
                    VDP1ProcessCommand();
                }
                break;*/
            case EvtType::VDP2DrawLine:
                m_deinterlaceRender ? VDP2DrawLine<true>(event.drawLine.vcnt)
                                    : VDP2DrawLine<false>(event.drawLine.vcnt);
                break;
            case EvtType::VDP2EndFrame: m_renderFinishedSignal.Set(); break;

            case EvtType::VDP1VRAMWriteByte: m_localState.VRAM1[event.write.address] = event.write.value; break;
            case EvtType::VDP1VRAMWriteWord:
                util::WriteBE<uint16>(&m_localState.VRAM1[event.write.address], event.write.value);
                break;
            /*case EvtType::VDP1FBWriteByte: m_localState.spriteFB[event.write.address] = event.write.value; break;
            case EvtType::VDP1FBWriteWord:
                util::WriteBE<uint16>(&m_localState.spriteFB[event.write.address], event.write.value);
                break;*/
            case EvtType::VDP1RegWrite: m_localState.regs1.Write<false>(event.write.address, event.write.value); break;

            case EvtType::VDP2VRAMWriteByte: m_localState.VRAM2[event.write.address] = event.write.value; break;
            case EvtType::VDP2VRAMWriteWord:
                util::WriteBE<uint16>(&m_localState.VRAM2[event.write.address], event.write.value);
                break;
            case EvtType::VDP2CRAMWriteByte:
                // Update CRAM cache if color RAM mode changed is in one of the RGB555 modes
                if (m_localState.regs2.vramControl.colorRAMMode <= 1) {
                    const uint8 oldValue = m_localState.CRAM[event.write.address];
                    m_localState.CRAM[event.write.address] = event.write.value;

                    if (oldValue != event.write.value) {
                        const uint32 cramAddress = event.write.address & ~1;
                        const uint16 colorValue = VDP2ReadRendererCRAM<uint16>(cramAddress);
                        const Color555 color5{.u16 = colorValue};
                        m_CRAMCache[cramAddress / sizeof(uint16)] = ConvertRGB555to888(color5);
                    }
                } else {
                    m_localState.CRAM[event.write.address] = event.write.value;
                }
                break;
            case EvtType::VDP2CRAMWriteWord:
                // Update CRAM cache if color RAM mode is in one of the RGB555 modes
                if (m_localState.regs2.vramControl.colorRAMMode <= 1) {
                    const uint16 oldValue = util::ReadBE<uint16>(&m_localState.CRAM[event.write.address]);
                    util::WriteBE<uint16>(&m_localState.CRAM[event.write.address], event.write.value);

                    if (oldValue != event.write.value) {
                        const uint32 cramAddress = event.write.address & ~1;
                        const Color555 color5{.u16 = (uint16)event.write.value};
                        m_CRAMCache[cramAddress / sizeof(uint16)] = ConvertRGB555to888(color5);
                    }
                } else {
                    util::WriteBE<uint16>(&m_localState.CRAM[event.write.address], event.write.value);
                }
                break;
            case EvtType::VDP2RegWrite:
                // Refill CRAM cache if color RAM mode changed to one of the RGB555 modes
                if (event.write.address == 0x00E) {
                    const uint8 oldMode = m_localState.regs2.vramControl.colorRAMMode;
                    m_localState.regs2.WriteRAMCTL(event.write.value);

                    const uint8 newMode = m_localState.regs2.vramControl.colorRAMMode;
                    if (newMode != oldMode && newMode <= 1) {
                        for (uint32 addr = 0; addr < m_localState.CRAM.size(); addr += sizeof(uint16)) {
                            const uint16 colorValue = VDP2ReadRendererCRAM<uint16>(addr);
                            const Color555 color5{.u16 = colorValue};
                            m_CRAMCache[addr / sizeof(uint16)] = ConvertRGB555to888(color5);
                        }
                    }
                } else {
                    m_localState.regs2.Write(event.write.address, event.write.value);
                }
                break;

            case EvtType::PreSaveStateSync: m_preSaveSyncSignal.Set(); break;
            case EvtType::PostLoadStateSync:
                m_localState = m_mainState;
                m_postLoadSyncSignal.Set();
                for (uint32 addr = 0; addr < m_localState.CRAM.size(); addr += sizeof(uint16)) {
                    const uint16 colorValue = VDP2ReadRendererCRAM<uint16>(addr);
                    const Color555 color5{.u16 = colorValue};
                    m_CRAMCache[addr / sizeof(uint16)] = ConvertRGB555to888(color5);
                }
                break;
            case EvtType::VDP1StateSync:
                m_localState.regs1 = m_mainState.regs1;
                m_localState.VRAM1 = m_mainState.VRAM1;
                m_postLoadSyncSignal.Set();
                break;

            case EvtType::UpdateEffectiveRenderingFlags: UpdateEffectiveRenderingFlags(); break;

            case EvtType::Shutdown: running = false; break;
            }
        }
    }

    // Drain queue
    RenderEvent dummy{};
    while (m_eventQueue.try_dequeue(dummy)) {
    }
}

template <mem_primitive T>
FORCE_INLINE T VDPRenderer::VDP1ReadRendererVRAM(uint32 address) {
    return util::ReadBE<T>(&GetRendererVDP1State().VRAM1[address & 0x7FFFF]);
}

template <mem_primitive T>
FORCE_INLINE T VDPRenderer::VDP2ReadRendererVRAM(uint32 address) {
    // TODO: handle VRSIZE.VRAMSZ
    return util::ReadBE<T>(&GetRendererVDPState().VRAM2[address & 0x7FFFF]);
}

template <mem_primitive T>
FORCE_INLINE T VDPRenderer::VDP2ReadRendererCRAM(uint32 address) {
    if constexpr (std::is_same_v<T, uint32>) {
        uint32 value = VDP2ReadRendererCRAM<uint16>(address + 0) << 16u;
        value |= VDP2ReadRendererCRAM<uint16>(address + 2) << 0u;
        return value;
    }

    VDPState &vdpState = GetRendererVDPState();
    address = MapCRAMAddress(address, vdpState.regs2.vramControl.colorRAMMode);
    return util::ReadBE<T>(&vdpState.CRAM[address]);
}

FORCE_INLINE Color888 VDPRenderer::VDP2ReadRendererColor5to8(uint32 address) {
    return m_CRAMCache[(address / sizeof(uint16)) & 0x7FF];
}

// -----------------------------------------------------------------------------
// Memory dumps

void VDPRenderer::DumpVDP1AltFramebuffers(std::ostream &out) const {
    const uint8 dispFB = m_mainState.displayFB;
    const uint8 drawFB = dispFB ^ 1;
    if (m_deinterlaceRender) {
        out.write((const char *)m_altSpriteFB[drawFB].data(), m_altSpriteFB[drawFB].size());
        out.write((const char *)m_altSpriteFB[dispFB].data(), m_altSpriteFB[dispFB].size());
    }
}

} // namespace ymir::vdp
