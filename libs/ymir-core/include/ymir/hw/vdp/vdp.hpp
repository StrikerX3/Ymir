#pragma once

/**
@file
@brief VDP1 and VDP2 implementation.
*/

#include "vdp_state.hpp"

#include "vdp_callbacks.hpp"
#include "vdp_internal_callbacks.hpp"

#include "slope.hpp"

#include <ymir/core/configuration.hpp>
#include <ymir/core/scheduler.hpp>
#include <ymir/sys/bus.hpp>
#include <ymir/sys/system.hpp>

#include <ymir/state/state_vdp.hpp>

#include <ymir/hw/hw_defs.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/event.hpp>
#include <ymir/util/inline.hpp>
#include <ymir/util/unreachable.hpp>

#include <blockingconcurrentqueue.h>

#include <array>
#include <iosfwd>
#include <span>
#include <thread>

namespace ymir::vdp {

// Contains both VDP1 and VDP2
class VDP {
public:
    VDP(core::Scheduler &scheduler, core::Configuration &config);
    ~VDP();

    void Reset(bool hard);

    void MapCallbacks(CBHBlankStateChange cbHBlankStateChange, CBVBlankStateChange cbVBlankStateChange,
                      CBTriggerEvent cbSpriteDrawEnd, CBTriggerEvent cbOptimizedINTBACKRead,
                      CBTriggerEvent cbSMPCVBlankIN) {
        m_cbHBlankStateChange = cbHBlankStateChange;
        m_cbVBlankStateChange = cbVBlankStateChange;
        m_cbTriggerSpriteDrawEnd = cbSpriteDrawEnd;
        m_cbTriggerOptimizedINTBACKRead = cbOptimizedINTBACKRead;
        m_cbTriggerSMPCVBlankIN = cbSMPCVBlankIN;
    }

    void MapMemory(sys::Bus &bus);

    // TODO: replace with scheduler events
    template <bool debug>
    void Advance(uint64 cycles);

    // -------------------------------------------------------------------------
    // Frontend callbacks

    void SetRenderCallback(CBFrameComplete callback) {
        m_cbFrameComplete = callback;
    }

    void SetVDP1DrawCallback(CBVDP1DrawFinished callback) {
        m_cbVDP1DrawFinished = callback;
    }

    void SetVDP1FramebufferSwapCallback(CBVDP1FramebufferSwap callback) {
        m_cbVDP1FramebufferSwap = callback;
    }

    // -------------------------------------------------------------------------
    // Configuration

    // Enable or disable deinterlacing of double-density interlaced frames.
    void SetDeinterlaceRender(bool enable) {
        m_deinterlaceRender = enable;
        UpdateFunctionPointers();
    }

    bool IsDeinterlaceRender() const {
        return m_deinterlaceRender;
    }

    // Enable or disable transparent mesh rendering enhancement.
    void SetTransparentMeshes(bool enable) {
        m_transparentMeshes = enable;
        UpdateFunctionPointers();
    }

    bool IsTransparentMeshes() const {
        return m_transparentMeshes;
    }

    void DumpVDP1VRAM(std::ostream &out) const;
    void DumpVDP2VRAM(std::ostream &out) const;
    void DumpVDP2CRAM(std::ostream &out) const;

    // Dumps draw framebuffer followed by display framebuffer
    void DumpVDP1Framebuffers(std::ostream &out) const;

    bool InLastLinePhase() const {
        return m_state.VPhase == VerticalPhase::LastLine;
    }

    // -------------------------------------------------------------------------
    // VDP1 framebuffer access

    std::span<const uint8> VDP1GetDisplayFramebuffer() const {
        return m_state.spriteFB[m_state.displayFB];
    }

    std::span<const uint8> VDP1GetDrawFramebuffer() const {
        return m_state.spriteFB[m_state.displayFB ^ 1];
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::VDPState &state) const;
    [[nodiscard]] bool ValidateState(const state::VDPState &state) const;
    void LoadState(const state::VDPState &state);

    // -------------------------------------------------------------------------
    // Rendering control

    // Enables or disables a layer.
    // Useful for debugging and troubleshooting.
    void SetLayerEnabled(Layer layer, bool enabled);

    // Detemrines if a layer is forcibly disabled.
    bool IsLayerEnabled(Layer layer) const;

private:
    VDPState m_state;

    // Cached CRAM colors converted from RGB555 to RGB888.
    // Only valid when color RAM mode is one of the RGB555 modes.
    alignas(16) std::array<Color888, kVDP2CRAMSize / sizeof(uint16)> m_CRAMCache;

    CBHBlankStateChange m_cbHBlankStateChange;
    CBVBlankStateChange m_cbVBlankStateChange;
    CBTriggerEvent m_cbTriggerSpriteDrawEnd;
    CBTriggerEvent m_cbTriggerOptimizedINTBACKRead;
    CBTriggerEvent m_cbTriggerSMPCVBlankIN;

    core::Scheduler &m_scheduler;
    core::EventID m_phaseUpdateEvent;

    static void OnPhaseUpdateEvent(core::EventContext &eventContext, void *userContext);

    using VideoStandard = core::config::sys::VideoStandard;
    void SetVideoStandard(VideoStandard videoStandard);

    // -------------------------------------------------------------------------
    // Configuration

    void EnableThreadedVDP(bool enable);
    void IncludeVDP1RenderInVDPThread(bool enable);

    // Hacky VDP1 command execution timing penalty accrued from external writes to VRAM
    // TODO: count pulled out of thin air
    static constexpr uint64 kVDP1TimingPenaltyPerWrite = 22;
    uint64 m_VDP1TimingPenaltyCycles;   // accumulated cycle penalty
    uint64 m_VDP1TimingPenaltyPerWrite; // penalty per write

    // -------------------------------------------------------------------------
    // Frontend callbacks

    // Invoked when the VDP1 finishes drawing a frame.
    CBVDP1DrawFinished m_cbVDP1DrawFinished;

    // Invoked when the VDP1 swaps framebuffers.
    CBVDP1FramebufferSwap m_cbVDP1FramebufferSwap;

    // Invoked when the renderer finishes drawing a frame.
    CBFrameComplete m_cbFrameComplete;

    // -------------------------------------------------------------------------
    // VDP1 memory/register access

    template <mem_primitive T>
    T VDP1ReadVRAM(uint32 address) const;

    template <mem_primitive T, bool poke>
    void VDP1WriteVRAM(uint32 address, T value);

    template <mem_primitive T>
    T VDP1ReadFB(uint32 address) const;

    template <mem_primitive T>
    void VDP1WriteFB(uint32 address, T value);

    template <bool peek>
    uint16 VDP1ReadReg(uint32 address) const;

    template <bool poke>
    void VDP1WriteReg(uint32 address, uint16 value);

    // -------------------------------------------------------------------------
    // VDP2 memory/register access

    template <mem_primitive T>
    T VDP2ReadVRAM(uint32 address) const;

    template <mem_primitive T>
    void VDP2WriteVRAM(uint32 address, T value);

    template <mem_primitive T, bool peek>
    T VDP2ReadCRAM(uint32 address) const;

    template <mem_primitive T, bool poke>
    void VDP2WriteCRAM(uint32 address, T value);

    uint16 VDP2ReadReg(uint32 address) const;
    void VDP2WriteReg(uint32 address, uint16 value);

    // -------------------------------------------------------------------------

    // RAMCTL.CRMD modes 2 and 3 shuffle address bits as follows:
    //   11 10 09 08 07 06 05 04 03 02 01 00 -- input
    //   01 11 10 09 08 07 06 05 04 03 02 00 -- output
    // In short, bits 11-02 are shifted right and bit 01 is shifted to the top.
    // This results in the lower 2 bytes of every longword to be stored at 000..3FF and the upper 2 bytes at 400..7FF.
    static constexpr auto kCRAMAddressMapping = [] {
        std::array<std::array<uint32, 4096>, 2> addrs{};
        for (uint32 addr = 0; addr < 4096; addr++) {
            addrs[0][addr] = addr;
            addrs[1][addr] = (bit::extract<1>(addr) << 11u) | (bit::extract<2, 11>(addr) << 1u) | bit::extract<0>(addr);
        }
        return addrs;
    }();

    FORCE_INLINE uint32 MapCRAMAddress(uint32 address) const {
        return kCRAMAddressMapping[m_state.regs2.vramControl.colorRAMMode >> 1][address & 0xFFF];
    }

    FORCE_INLINE uint32 MapRendererCRAMAddress(uint32 address) const {
        return kCRAMAddressMapping[m_VDPRenderContext.vdp2.regs.vramControl.colorRAMMode >> 1][address & 0xFFF];
    }

    template <mem_primitive T>
    void VDP2UpdateCRAMCache(uint32 address);

    // -------------------------------------------------------------------------
    // Timings and signals

    // Display resolution (derived from TVMODE)
    uint32 m_HRes; // Horizontal display resolution
    uint32 m_VRes; // Vertical display resolution
    bool m_exclusiveMonitor;

    // Display timings
    std::array<uint32, 4> m_HTimings;                // [phase]
    std::array<std::array<uint32, 6>, 2> m_VTimings; // [even/odd][phase]
    uint32 m_VTimingField;
    uint16 m_VCounterSkip;

    // Moves to the next phase.
    void UpdatePhase();

    // Returns the number of cycles between the current and the next phase.
    uint64 GetPhaseCycles() const;

    // Updates the display resolution and timings based on TVMODE if it is dirty
    //
    // `verbose` enables dev logging
    template <bool verbose>
    void UpdateResolution();

    void IncrementVCounter();

    // Phase handlers
    void BeginHPhaseActiveDisplay();
    void BeginHPhaseRightBorder();
    void BeginHPhaseSync();
    void BeginHPhaseLeftBorder();

    void BeginVPhaseActiveDisplay();
    void BeginVPhaseBottomBorder();
    void BeginVPhaseBlankingAndSync();
    void BeginVPhaseVCounterSkip();
    void BeginVPhaseTopBorder();
    void BeginVPhaseLastLine();

    // -------------------------------------------------------------------------
    // VDP rendering

    // TODO: split out rendering code

    struct VDPRenderEvent {
        enum class Type {
            Reset,
            OddField,
            VDP1EraseFramebuffer,
            VDP1SwapFramebuffer,
            VDP1BeginFrame,
            // VDP1ProcessCommands,

            VDP2BeginFrame,
            VDP2UpdateEnabledBGs,
            VDP2DrawLine,
            VDP2EndFrame,

            VDP1VRAMWriteByte,
            VDP1VRAMWriteWord,
            /*VDP1FBWriteByte,
            VDP1FBWriteWord,*/
            VDP1RegWrite,

            VDP2VRAMWriteByte,
            VDP2VRAMWriteWord,
            VDP2CRAMWriteByte,
            VDP2CRAMWriteWord,
            VDP2RegWrite,

            PreSaveStateSync,
            PostLoadStateSync,
            VDP1StateSync,

            UpdateEffectiveRenderingFlags,

            Shutdown,
        };

        Type type;
        union {
            struct {
                uint32 vcnt;
            } drawLine;

            struct {
                bool odd;
            } oddField;

            /*struct {
                uint64 steps;
            } vdp1ProcessCommands;*/

            struct {
                uint32 address;
                uint32 value;
            } write;
        };

        static VDPRenderEvent Reset() {
            return {Type::Reset};
        }

        static VDPRenderEvent OddField(bool odd) {
            return {Type::OddField, {.oddField = {.odd = odd}}};
        }

        static VDPRenderEvent VDP1EraseFramebuffer() {
            return {Type::VDP1EraseFramebuffer};
        }

        static VDPRenderEvent VDP1SwapFramebuffer() {
            return {Type::VDP1SwapFramebuffer};
        }

        static VDPRenderEvent VDP1BeginFrame() {
            return {Type::VDP1BeginFrame};
        }

        /*static VDP1RenderEvent VDP1ProcessCommands(uint64 steps) {
            return {Type::VDP1ProcessCommands, {.processCommands = {.steps = steps}}};
        }*/

        static VDPRenderEvent VDP2BeginFrame() {
            return {Type::VDP2BeginFrame};
        }

        static VDPRenderEvent VDP2UpdateEnabledBGs() {
            return {Type::VDP2UpdateEnabledBGs};
        }

        static VDPRenderEvent VDP2DrawLine(uint32 vcnt) {
            return {Type::VDP2DrawLine, {.drawLine = {.vcnt = vcnt}}};
        }

        static VDPRenderEvent VDP2EndFrame() {
            return {Type::VDP2EndFrame};
        }

        template <mem_primitive T>
        static VDPRenderEvent VDP1VRAMWrite(uint32 address, T value) {
            static_assert(!std::is_same_v<T, uint32>, "unsupported write size");

            if constexpr (std::is_same_v<T, uint8>) {
                return VDP1VRAMWriteByte(address, value);
            } else if constexpr (std::is_same_v<T, uint16>) {
                return VDP1VRAMWriteWord(address, value);
            }
            util::unreachable();
        }

        static VDPRenderEvent VDP1VRAMWriteByte(uint32 address, uint8 value) {
            return {Type::VDP1VRAMWriteByte, {.write = {.address = address, .value = value}}};
        }

        static VDPRenderEvent VDP1VRAMWriteWord(uint32 address, uint16 value) {
            return {Type::VDP1VRAMWriteWord, {.write = {.address = address, .value = value}}};
        }

        /*template <mem_primitive T>
        static VDPRenderEvent VDP1FBWrite(uint32 address, T value) {
            static_assert(!std::is_same_v<T, uint32>, "unsupported write size");

            if constexpr (std::is_same_v<T, uint8>) {
                return VDP1FBWriteByte(address, value);
            } else if constexpr (std::is_same_v<T, uint16>) {
                return VDP1FBWriteWord(address, value);
            }
            util::unreachable();
        }

        static VDPRenderEvent VDP1FBWriteByte(uint32 address, uint8 value) {
            return {Type::VDP1FBWriteByte, {.write = {.address = address, .value = value}}};
        }

        static VDPRenderEvent VDP1FBWriteWord(uint32 address, uint16 value) {
            return {Type::VDP1FBWriteWord, {.write = {.address = address, .value = value}}};
        }*/

        static VDPRenderEvent VDP1RegWrite(uint32 address, uint16 value) {
            return {Type::VDP1RegWrite, {.write = {.address = address, .value = value}}};
        }

        template <mem_primitive T>
        static VDPRenderEvent VDP2VRAMWrite(uint32 address, T value) {
            static_assert(!std::is_same_v<T, uint32>, "unsupported write size");

            if constexpr (std::is_same_v<T, uint8>) {
                return VDP2VRAMWriteByte(address, value);
            } else if constexpr (std::is_same_v<T, uint16>) {
                return VDP2VRAMWriteWord(address, value);
            }
            util::unreachable();
        }

        static VDPRenderEvent VDP2VRAMWriteByte(uint32 address, uint8 value) {
            return {Type::VDP2VRAMWriteByte, {.write = {.address = address, .value = value}}};
        }

        static VDPRenderEvent VDP2VRAMWriteWord(uint32 address, uint16 value) {
            return {Type::VDP2VRAMWriteWord, {.write = {.address = address, .value = value}}};
        }

        template <mem_primitive T>
        static VDPRenderEvent VDP2CRAMWrite(uint32 address, T value) {
            static_assert(!std::is_same_v<T, uint32>, "unsupported write size");

            if constexpr (std::is_same_v<T, uint8>) {
                return VDP2CRAMWriteByte(address, value);
            } else if constexpr (std::is_same_v<T, uint16>) {
                return VDP2CRAMWriteWord(address, value);
            }
            util::unreachable();
        }

        static VDPRenderEvent VDP2CRAMWriteByte(uint32 address, uint8 value) {
            return {Type::VDP2CRAMWriteByte, {.write = {.address = address, .value = value}}};
        }

        static VDPRenderEvent VDP2CRAMWriteWord(uint32 address, uint16 value) {
            return {Type::VDP2CRAMWriteWord, {.write = {.address = address, .value = value}}};
        }

        static VDPRenderEvent VDP2RegWrite(uint32 address, uint16 value) {
            return {Type::VDP2RegWrite, {.write = {.address = address, .value = value}}};
        }

        static VDPRenderEvent PreSaveStateSync() {
            return {Type::PreSaveStateSync};
        }

        static VDPRenderEvent PostLoadStateSync() {
            return {Type::PostLoadStateSync};
        }

        static VDPRenderEvent VDP1StateSync() {
            return {Type::VDP1StateSync};
        }

        static VDPRenderEvent UpdateEffectiveRenderingFlags() {
            return {Type::UpdateEffectiveRenderingFlags};
        }

        static VDPRenderEvent Shutdown() {
            return {Type::Shutdown};
        }
    };

    mutable struct VDPRenderContext {
        struct QueueTraits : moodycamel::ConcurrentQueueDefaultTraits {
            static constexpr size_t BLOCK_SIZE = 64;
            static constexpr size_t EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD = 64;
            static constexpr std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = 512;
            static constexpr int MAX_SEMA_SPINS = 20000;
        };

        moodycamel::BlockingConcurrentQueue<VDPRenderEvent, QueueTraits> eventQueue;
        moodycamel::ProducerToken pTok{eventQueue};
        moodycamel::ConsumerToken cTok{eventQueue};
        util::Event renderFinishedSignal{false};
        util::Event framebufferSwapSignal{false};
        util::Event eraseFramebufferReadySignal{false};
        util::Event preSaveSyncSignal{false};
        util::Event postLoadSyncSignal{false};

        util::Event deinterlaceRenderBeginSignal{false};
        util::Event deinterlaceRenderEndSignal{false};
        uint32 deinterlaceY;
        std::atomic_bool deinterlaceShutdown;

        std::array<VDPRenderEvent, 64> pendingEvents;
        size_t pendingEventsCount = 0;

        bool vdp1Done;

        struct VDP1 {
            VDP1Regs regs;
            alignas(16) std::array<uint8, kVDP1VRAMSize> VRAM;
            // alignas(16) std::array<SpriteFB, 2> spriteFB;
        } vdp1;

        struct VDP2 {
            VDP2Regs regs;
            alignas(16) std::array<uint8, kVDP2VRAMSize> VRAM;
            alignas(16) std::array<uint8, kVDP2CRAMSize> CRAM;

            // Cached CRAM colors converted from RGB555 to RGB888.
            // Only valid when color RAM mode is one of the RGB555 modes.
            alignas(16) std::array<Color888, kVDP2CRAMSize / sizeof(uint16)> CRAMCache;
        } vdp2;

        uint8 displayFB;

        void Reset() {
            vdp1.regs.Reset();
            for (uint32 addr = 0; addr < vdp1.VRAM.size(); addr++) {
                if ((addr & 0x1F) == 0) {
                    vdp1.VRAM[addr] = 0x80;
                } else if ((addr & 0x1F) == 1) {
                    vdp1.VRAM[addr] = 0x00;
                } else if ((addr & 2) == 2) {
                    vdp1.VRAM[addr] = 0x55;
                } else {
                    vdp1.VRAM[addr] = 0xAA;
                }
            }
            // vdp1.spriteFB[0].fill(0);
            // vdp1.spriteFB[1].fill(0);
            vdp2.regs.Reset();
            vdp2.VRAM.fill(0);
            vdp2.CRAM.fill(0);
            vdp2.CRAMCache.fill({.u32 = 0});
            displayFB = 0;

            vdp1Done = false;
        }

        void EnqueueEvent(VDPRenderEvent &&event) {
            switch (event.type) {
            case VDPRenderEvent::Type::VDP1VRAMWriteByte:
            case VDPRenderEvent::Type::VDP1VRAMWriteWord:
            case VDPRenderEvent::Type::VDP1RegWrite:
            case VDPRenderEvent::Type::VDP2VRAMWriteByte:
            case VDPRenderEvent::Type::VDP2VRAMWriteWord:
            case VDPRenderEvent::Type::VDP2CRAMWriteByte:
            case VDPRenderEvent::Type::VDP2CRAMWriteWord:
            case VDPRenderEvent::Type::VDP2RegWrite:
                // Batch VRAM, CRAM and register writes to send in bulk
                pendingEvents[pendingEventsCount++] = event;
                if (pendingEventsCount == pendingEvents.size()) {
                    eventQueue.enqueue_bulk(pTok, pendingEvents.begin(), pendingEventsCount);
                    pendingEventsCount = 0;
                }
                break;
            default:
                // Send any pending writes before rendering
                if (pendingEventsCount > 0) {
                    eventQueue.enqueue_bulk(pTok, pendingEvents.begin(), pendingEventsCount);
                    pendingEventsCount = 0;
                }
                eventQueue.enqueue(pTok, event);
                break;
            }
        }

        template <typename It>
        size_t DequeueEvents(It first, size_t count) {
            return eventQueue.wait_dequeue_bulk(cTok, first, count);
        }
    } m_VDPRenderContext;

    std::thread m_VDPRenderThread;
    std::thread m_VDPDeinterlaceRenderThread;
    bool m_threadedVDPRendering = false;
    bool m_renderVDP1OnVDP2Thread = false;

    // Managed by the render thread.
    // == m_threadedVDPRendering && m_renderVDP1OnVDP2Thread
    bool m_effectiveRenderVDP1InVDP2Thread = false;

    void UpdateEffectiveRenderingFlags();

    void VDPRenderThread();
    void VDPDeinterlaceRenderThread();

    template <mem_primitive T>
    T VDP1ReadRendererVRAM(uint32 address);

    template <mem_primitive T>
    T VDP2ReadRendererVRAM(uint32 address);

    std::array<uint8, kVDP2VRAMSize> &VDP2GetRendererVRAM();

    template <mem_primitive T>
    T VDP2ReadRendererCRAM(uint32 address);

    Color888 VDP2ReadRendererColor5to8(uint32 address) const;

    // Enables deinterlacing of double-density interlace frames in the renderer.
    // When false, double-density interlace mode is rendered normally - only even or odd lines are updated every frame.
    // When true, double-density interlace modes is rendered in full resolution image every frame.
    // - the standard even/odd frame is rendered into m_spriteFB
    // - the complementary field is rendered into m_altSpriteFB
    // - VDP2 renders two lines per line into the output framebuffer instead of just the even or odd lines
    // - data written by the CPU on the VDP1 framebuffer is mirrored to the same position in m_altSpriteFB
    bool m_deinterlaceRender = false;

    // Runs the deinterlacer in a dedicated thread.
    bool m_threadedDeinterlacer = false;

    // Enables rendering of meshes as transparent polygons.
    // When false, mesh polygons are rendered as checkerboard patterns, exactly like a real Saturn.
    // When true, mesh polygons are rendered as 50% transparent. This does not interfere with color calculations or
    // other graphics effects.
    bool m_transparentMeshes = false;

    // Complementary (alternate) VDP1 framebuffers, for deinterlaced rendering.
    // When deinterlace mode is enabled, if the system is using double-density interlace, this buffer will contain the
    // field lines complementary to the standard VDP1 framebuffer memory (e.g. while displaying odd lines, this buffer
    // contains even lines).
    // VDP2 rendering will combine both buffers to draw a full-resolution progressive image in one go.
    alignas(16) std::array<SpriteFB, 2> m_altSpriteFB;

    using FnVDP1ProcessCommand = void (VDP::*)();
    using FnVDP2DrawLine = void (VDP::*)(uint32 y, bool altField);

    FnVDP1ProcessCommand m_fnVDP1ProcessCommand;
    FnVDP2DrawLine m_fnVDP2DrawLine;

    // Updates the function pointers based on the rendering settings.
    void UpdateFunctionPointers();

    // -------------------------------------------------------------------------
    // VDP1

    // VDP1 renderer parameters and state
    struct VDP1RenderContext {
        VDP1RenderContext() {
            Reset();
        }

        void Reset() {
            sysClipH = 512;
            sysClipV = 256;

            userClipX0 = 0;
            userClipY0 = 0;

            userClipX1 = 512;
            userClipY1 = 256;

            localCoordX = 0;
            localCoordY = 0;

            rendering = false;

            erase = false;

            cycleCount = 0;
        }

        // System clipping dimensions
        uint16 sysClipH;
        uint16 sysClipV;

        // User clipping area
        // Top-left
        uint16 userClipX0;
        uint16 userClipY0;
        // Bottom-right
        uint16 userClipX1;
        uint16 userClipY1;

        // Local coordinates offset
        sint32 localCoordX;
        sint32 localCoordY;

        // Is the VDP1 currently processing commands?
        bool rendering;

        // Is manual framebuffer erase scheduled for the next frame?
        bool erase;

        // Command processing cycle counter
        uint64 cycleCount;

        // Transparent mesh sprite framebuffer.
        // Used when transparent meshes are enabled.
        // Indexing: [altFB][drawFB]
        std::array<std::array<SpriteFB, 2>, 2> meshFB;
    } m_VDP1RenderContext;

    struct VDP1PixelParams {
        VDP1Command::DrawMode mode;
        uint16 color;
        GouraudStepper gouraud;
    };

    struct VDP1LineParams {
        VDP1Command::DrawMode mode;
        uint16 color;
        Color555 gouraudLeft;
        Color555 gouraudRight;
    };

    struct VDP1TexturedLineParams {
        VDP1Command::Control control;
        VDP1Command::DrawMode mode;
        uint32 colorBank;
        uint32 charAddr;
        uint32 charSizeH;
        uint32 charSizeV;
        TextureStepper texVStepper;
        const GouraudStepper *gouraudLeft;
        const GouraudStepper *gouraudRight;
    };

    // Character modes, a combination of Character Size from the Character Control Register (CHCTLA-B) and Character
    // Number Supplement from the Pattern Name Control Register (PNCN0-3/PNCR)
    enum class CharacterMode {
        TwoWord,         // 2 word characters
        OneWordStandard, // 1 word characters with standard character data, H/V flip available
        OneWordExtended, // 1 word characters with extended character data; H/V flip unavailable
    };

    // Pattern Name Data, contains parameters for a character
    struct Character {
        uint16 charNum;     // Character number, 15 bits
        uint8 palNum;       // Palette number, 7 bits
        bool specColorCalc; // Special color calculation
        bool specPriority;  // Special priority
        bool flipH;         // Horizontal flip
        bool flipV;         // Vertical flip
    };

    // Common pixel data: color, transparency, priority and special color calculation flag.
    struct Pixel {
        Color888 color;
        uint8 priority;
        bool transparent;
        bool specialColorCalc;
    };

    struct Pixels {
        alignas(16) std::array<Color888, kMaxResH> color;
        alignas(16) std::array<uint8, kMaxResH> priority;
        alignas(16) std::array<bool, kMaxResH> transparent;
        alignas(16) std::array<bool, kMaxResH> specialColorCalc;

        FORCE_INLINE Pixel GetPixel(size_t index) const {
            return Pixel{
                .color = color[index],
                .priority = priority[index],
                .transparent = transparent[index],
                .specialColorCalc = specialColorCalc[index],
            };
        }
        FORCE_INLINE void SetPixel(size_t index, Pixel pixel) {
            color[index] = pixel.color;
            priority[index] = pixel.priority;
            transparent[index] = pixel.transparent;
            specialColorCalc[index] = pixel.specialColorCalc;
        }
        FORCE_INLINE void CopyPixel(size_t src, size_t dst) {
            color[dst] = color[src];
            priority[dst] = priority[src];
            transparent[dst] = transparent[src];
            specialColorCalc[dst] = specialColorCalc[src];
        }
    };

    // Layer state, containing the pixel output for the current scanline.
    struct alignas(4096) LayerState {
        LayerState() {
            Reset();
        }

        void Reset() {
            pixels.color.fill({});
            pixels.priority.fill({});
            pixels.transparent.fill(false);
            pixels.specialColorCalc.fill(false);
        }

        alignas(16) Pixels pixels;
    };

    // Attributes specific to the sprite layer for the current scanline.
    struct SpriteLayerAttributes {
        SpriteLayerAttributes() {
            Reset();
        }

        void Reset() {
            colorCalcRatio.fill(0);
            shadowOrWindow.fill(false);
            normalShadow.fill(false);
            window.fill(false);
        }

        void CopyAttrs(size_t src, size_t dst) {
            colorCalcRatio[dst] = colorCalcRatio[src];
            shadowOrWindow[dst] = shadowOrWindow[src];
            normalShadow[dst] = normalShadow[src];
            // window is computed separately
        }

        alignas(16) std::array<uint8, kMaxResH> colorCalcRatio;
        alignas(16) std::array<bool, kMaxResH> shadowOrWindow;
        alignas(16) std::array<bool, kMaxResH> normalShadow;

        alignas(16) std::array<bool, kMaxResH> window;
    };

    // Pipelined VRAM fetcher
    struct VRAMFetcher {
        VRAMFetcher() {
            Reset();
        }

        void Reset() {
            currChar = {};
            nextChar = {};
            lastCharIndex = 0xFFFFFFFF;

            bitmapData.fill(0);

            lastVCellScroll = 0xFFFFFFFF;
        }

        bool UpdateBitmapDataAddress(uint32 address) {
            address &= ~7;
            if (address != bitmapDataAddress) {
                bitmapDataAddress = address;
                return true;
            }
            return false;
        }

        // Character patterns (for scroll BGs)
        Character currChar;
        Character nextChar;
        uint32 lastCharIndex;

        // Bitmap data (for bitmap BGs)
        alignas(uint64) std::array<uint8, 8> bitmapData;
        uint32 bitmapDataAddress;

        // Vertical cell scroll data
        uint32 lastVCellScroll;
    };

    // NBG layer state, including coordinate counters, increments and addresses.
    struct NormBGLayerState {
        NormBGLayerState() {
            Reset();
        }

        void Reset() {
            fracScrollX = 0;
            fracScrollY = 0;
            scrollIncH = 0x100;
            lineScrollTableAddress = 0;
            vertCellScrollOffset = 0;
            vertCellScrollDelay = 0;
            vertCellScrollRepeat = 0;
            mosaicCounterY = 0;
        }

        // Initial fractional X scroll coordinate.
        uint32 fracScrollX;

        // Fractional Y scroll coordinate.
        // Reset at the start of every frame and updated every scanline.
        uint32 fracScrollY;

        // Vertical scroll amount with 8 fractional bits.
        // Initialized at VBlank OUT.
        // Derived from SCYINn and SCYDNn
        uint32 scrollAmountV;

        // Fractional X scroll coordinate increment.
        // Applied every pixel and updated every scanline.
        uint32 scrollIncH;

        // Current line scroll table address.
        // Reset at the start of every frame and incremented every 1/2/4/8/16 lines.
        uint32 lineScrollTableAddress;

        // Vertical cell scroll offset.
        // Only valid for NBG0 and NBG1.
        // Based on CYCA0/A1/B0/B1 parameters.
        uint32 vertCellScrollOffset;

        // Is the vertical cell scroll read delayed by one cycle?
        // Only valid for NBG0 and NBG1.
        // Based on CYCA0/A1/B0/B1 parameters.
        bool vertCellScrollDelay;

        // Is the first vertical cell scroll entry repeated?
        // Only valid for NBG0.
        // Based on CYCA0/A1/B0/B1 parameters.
        bool vertCellScrollRepeat;

        // Vertical mosaic counter.
        // Reset at the start of every frame and incremented every line.
        // The value is mod mosaicV.
        uint8 mosaicCounterY;
    };

    // State for Rotation Parameters A and B.
    struct RotationParamState {
        RotationParamState() {
            Reset();
        }

        void Reset() {
            for (auto &addrs : pageBaseAddresses) {
                addrs.fill(0);
            }
            screenCoords.fill({});
            lineColor.fill({.u32 = 0});
            transparent.fill(false);
            Xst = Yst = 0;
            KA = 0;
        }

        // Page base addresses for RBG planes A-P using Rotation Parameters A and B.
        // Indexing: [RBG0-1][Plane A-P]
        // Derived from mapIndices, CHCTLA/CHCTLB.xxCHSZ, PNCR.xxPNB and PLSZ.xxPLSZn
        std::array<std::array<uint32, 16>, 2> pageBaseAddresses;

        // Precomputed screen coordinates (with 16 fractional bits).
        alignas(16) std::array<CoordS32, kMaxResH> screenCoords;

        // Precomputed sprite coordinates (without fractional bits).
        alignas(16) std::array<CoordS32, kMaxResH> spriteCoords;

        // Precomputed coefficient table line color.
        // Filled in only if the coefficient table is enabled and using line color data.
        alignas(16) std::array<Color888, kMaxResH> lineColor;

        // Prefetched coefficient table transparency bits.
        // Filled in only if the coefficient table is enabled.
        alignas(16) std::array<bool, kMaxResH> transparent;

        // Current base screen coordinates, updated every scanline.
        sint32 Xst, Yst;

        // Current base coefficient address, updated every scanline.
        uint32 KA;
    };

    enum RotParamSelector { RotParamA, RotParamB };

    // State of the LNCL and BACK screens, including the current color and address.
    struct LineBackLayerState {
        LineBackLayerState() {
            Reset();
        }

        void Reset() {
            lineColor.u32 = 0;
            backColor.u32 = 0;
        }

        Color888 lineColor;
        Color888 backColor;
    };

    // Layer state indices
    enum LayerIndex : uint8 {
        LYR_Sprite,
        LYR_RBG0,
        LYR_NBG0_RBG1,
        LYR_NBG1_EXBG,
        LYR_NBG2,
        LYR_NBG3,
        LYR_Back,
        LYR_LineColor,
    };

    // Layer enabled by BGON and other factors.
    //     RBG0+RBG1   RBG0        no RBGs
    // [0] Sprite      Sprite      Sprite
    // [1] RBG0        RBG0        -
    // [2] RBG1        NBG0        NBG0
    // [3] EXBG        NBG1/EXBG   NBG1/EXBG
    // [4] -           NBG2        NBG2
    // [5] -           NBG3        NBG3
    std::array<bool, 6> m_layerEnabled;

    // Layer enabled for rendering.
    // Externally configured - do not include in save state!
    //     RBG0+RBG1   RBG0        no RBGs
    // [0] Sprite      Sprite      Sprite
    // [1] RBG0        RBG0        -
    // [2] RBG1        NBG0        NBG0
    // [3] EXBG        NBG1/EXBG   NBG1/EXBG
    // [4] -           NBG2        NBG2
    // [5] -           NBG3        NBG3
    std::array<bool, 6> m_layerRendered;

    // Common layer states.
    // Entry [0] is primary and [1] is alternate field for deinterlacing.
    //     RBG0+RBG1   RBG0        no RBGs
    // [0] Sprite      Sprite      Sprite
    // [1] RBG0        RBG0        -
    // [2] RBG1        NBG0        NBG0
    // [3] EXBG        NBG1/EXBG   NBG1/EXBG
    // [4] -           NBG2        NBG2
    // [5] -           NBG3        NBG3
    std::array<std::array<LayerState, 6>, 2> m_layerStates;

    // Sprite layer attributes.
    // Entry [0] is primary and [1] is alternate field for deinterlacing.
    std::array<SpriteLayerAttributes, 2> m_spriteLayerAttrs;

    // Transparent mesh layer states.
    // Entry [0] is primary and [1] is alternate field for deinterlacing.
    std::array<LayerState, 2> m_meshLayerState;

    // Transparent mesh sprite layer attributes.
    // Entry [0] is primary and [1] is alternate field for deinterlacing.
    std::array<SpriteLayerAttributes, 2> m_meshLayerAttrs;

    // Layer state for NBGs 0-3.
    std::array<NormBGLayerState, 4> m_normBGLayerStates;

    // States for Rotation Parameters A and B.
    std::array<RotationParamState, 2> m_rotParamStates;

    // State for the line color and back screens.
    LineBackLayerState m_lineBackLayerState;

    // VRAM fetcher states for NBGs 0-3 and rotation parameters A/B.
    // Entry [0] is primary and [1] is alternate field for deinterlacing.
    std::array<std::array<VRAMFetcher, 6>, 2> m_vramFetchers;

    // Window state for NBGs and RBGs.
    // Entry [0] is primary and [1] is alternate field for deinterlacing.
    // [0] RBG0
    // [1] NBG0/RBG1
    // [2] NBG1/EXBG
    // [3] NBG2
    // [4] NBG3
    alignas(16) std::array<std::array<std::array<bool, kMaxResH>, 5>, 2> m_bgWindows;

    // Window state for rotation parameters.
    // Entry [0] is primary and [1] is alternate field for deinterlacing.
    alignas(16) std::array<std::array<bool, kMaxResH>, 2> m_rotParamsWindow;

    // Window state for color calculation.
    // Entry [0] is primary and [1] is alternate field for deinterlacing.
    alignas(16) std::array<std::array<bool, kMaxResH>, 2> m_colorCalcWindow;

    // Vertical cell scroll increment.
    // Based on CYCA0/A1/B0/B1 parameters.
    uint32 m_vertCellScrollInc;

    // Current display framebuffer.
    std::array<uint32, kMaxResH * kMaxResV> m_framebuffer;

    // Retrieves the current set of VDP1 registers.
    VDP1Regs &VDP1GetRegs();

    // Retrieves the current set of VDP1 registers.
    const VDP1Regs &VDP1GetRegs() const;

    // Retrieves the current index of the VDP1 display framebuffer.
    uint8 VDP1GetDisplayFBIndex() const;

    // Erases the current VDP1 display framebuffer.
    void VDP1EraseFramebuffer();

    // Swaps VDP1 framebuffers.
    void VDP1SwapFramebuffer();

    // Begins the next VDP1 frame.
    void VDP1BeginFrame();

    // Ends the current VDP1 frame.
    void VDP1EndFrame();

#define TPL_TRAITS template <bool deinterlace, bool transparentMeshes>
#define TPL_LINE_TRAITS template <bool antiAlias, bool deinterlace, bool transparentMeshes>
#define TPL_DEINTERLACE template <bool deinterlace>

    // Processes a single commmand from the VDP1 command table.
    TPL_TRAITS void VDP1ProcessCommand();

    TPL_DEINTERLACE bool VDP1IsPixelUserClipped(CoordS32 coord) const;
    TPL_DEINTERLACE bool VDP1IsPixelSystemClipped(CoordS32 coord) const;
    TPL_DEINTERLACE bool VDP1IsLineSystemClipped(CoordS32 coord1, CoordS32 coord2) const;
    TPL_DEINTERLACE bool VDP1IsQuadSystemClipped(CoordS32 coord1, CoordS32 coord2, CoordS32 coord3,
                                                 CoordS32 coord4) const;

    TPL_TRAITS void VDP1PlotPixel(CoordS32 coord, const VDP1PixelParams &pixelParams);
    TPL_LINE_TRAITS void VDP1PlotLine(CoordS32 coord1, CoordS32 coord2, VDP1LineParams &lineParams);
    TPL_TRAITS void VDP1PlotTexturedLine(CoordS32 coord1, CoordS32 coord2, VDP1TexturedLineParams &lineParams);
    TPL_TRAITS void VDP1PlotTexturedQuad(uint32 cmdAddress, VDP1Command::Control control, VDP1Command::Size size,
                                         CoordS32 coordA, CoordS32 coordB, CoordS32 coordC, CoordS32 coordD);

    // Individual VDP1 command processors

    TPL_TRAITS void VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control);
    TPL_TRAITS void VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control);
    TPL_TRAITS void VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control);

    TPL_TRAITS void VDP1Cmd_DrawPolygon(uint32 cmdAddress, VDP1Command::Control control);
    TPL_TRAITS void VDP1Cmd_DrawPolylines(uint32 cmdAddress, VDP1Command::Control control);
    TPL_TRAITS void VDP1Cmd_DrawLine(uint32 cmdAddress, VDP1Command::Control control);

    void VDP1Cmd_SetSystemClipping(uint32 cmdAddress);
    void VDP1Cmd_SetUserClipping(uint32 cmdAddress);
    void VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress);

#undef TPL_TRAITS

    // -------------------------------------------------------------------------
    // VDP2

    // Retrieves the current set of VDP2 registers.
    VDP2Regs &VDP2GetRegs();

    // Retrieves the current set of VDP2 registers.
    const VDP2Regs &VDP2GetRegs() const;

    // Retrieves the current VDP2 VRAM array.
    std::array<uint8, kVDP2VRAMSize> &VDP2GetVRAM();

    // Initializes renderer state for a new frame.
    void VDP2InitFrame();

    // Initializes the specified NBG.
    template <uint32 index>
    void VDP2InitNormalBG();

    void VDP2UpdateRotationPageBaseAddresses(VDP2Regs &regs2);

    // Updates the enabled backgrounds.
    void VDP2UpdateEnabledBGs();

    // Updates the line screen scroll parameters for NBG0 and NBG1.
    //
    // y is the scanline to draw
    void VDP2UpdateLineScreenScrollParams(uint32 y);

    // Updates the line screen scroll parameters for the given background.
    // Only valid for NBG0 and NBG1.
    //
    // y is the scanline to draw
    // bgParams contains the parameters for the BG to draw.
    // bgState is a reference to the background layer state for the background.
    void VDP2UpdateLineScreenScroll(uint32 y, const BGParams &bgParams, NormBGLayerState &bgState);

    // Loads rotation parameter tables and calculates coefficients and increments.
    //
    // y is the scanline to draw
    void VDP2CalcRotationParameterTables(uint32 y);

    // Precalculates all window state for the scanline.
    //
    // y is the scanline to draw
    //
    // deinterlace determines whether to deinterlace video output
    // altField selects the complementary field when rendering deinterlaced frames
    template <bool deinterlace, bool altField>
    void VDP2CalcWindows(uint32 y);

    // Precalculates window state for a given set of parameters.
    //
    // y is the scanline to draw
    // windowSet contains the windows
    // windowParams contains additional window parameters
    // windowState is the window state output
    //
    // altField selects the complementary field when rendering deinterlaced frames
    template <bool altField, bool hasSpriteWindow>
    void VDP2CalcWindow(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                        const std::array<WindowParams, 2> &windowParams, std::span<bool> windowState);

    // Precalculates window state for a given set of parameters using AND or OR logic.
    //
    // y is the scanline to draw
    // windowSet contains the windows
    // windowParams contains additional window parameters
    // windowState is the window state output
    //
    // altField selects the complementary field when rendering deinterlaced frames
    // logicOR determines if the windows should be combined with OR logic (true) or AND logic (false)
    template <bool altField, bool logicOR, bool hasSpriteWindow>
    void VDP2CalcWindowLogic(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                             const std::array<WindowParams, 2> &windowParams, std::span<bool> windowState);

    // Computes the access patterns for NBGs and RBGs.
    //
    // regs2 is a reference to the set of VDP2 registers to use as reference
    void VDP2CalcAccessPatterns(VDP2Regs &regs2);

    // Prepares the specified VDP2 scanline for rendering.
    //
    // y is the scanline to prepare
    void VDP2PrepareLine(uint32 y);

    // Finishes rendering the specified VDP2 scanline, updating internal registers.
    //
    // y is the scanline to finish
    void VDP2FinishLine(uint32 y);

    // Draws the specified VDP2 scanline.
    //
    // y is the scanline to draw
    // altField selects the complementary field when rendering deinterlaced frames
    //
    // deinterlace determines whether to deinterlace video output
    // transparentMeshes enables transparent mesh rendering enhancement
    template <bool deinterlace, bool transparentMeshes>
    void VDP2DrawLine(uint32 y, bool altField);

    // Draws the line color and back screens.
    //
    // y is the scanline to draw
    void VDP2DrawLineColorAndBackScreens(uint32 y);

    // Draws the current VDP2 scanline of the sprite layer.
    //
    // y is the scanline to draw
    //
    // colorMode is the CRAM color mode.
    // rotate determines if Rotation Parameter A coordinates should be used to draw the sprite layer.
    // altField selects the complementary field when rendering deinterlaced frames
    // transparentMeshes enables transparent mesh rendering enhancement
    template <uint32 colorMode, bool rotate, bool altField, bool transparentMeshes>
    void VDP2DrawSpriteLayer(uint32 y);

    // Draws a pixel on the sprite layer of the current VDP2 scanline.
    //
    // x is the X coordinate of the pixel to draw.
    // params contains the sprite layer's parameters.
    // spriteFB is a reference to the sprite framebuffer to read from.
    // spriteFBOffset is the offset into the buffer of the pixel to read.
    //
    // colorMode is the CRAM color mode.
    // altField selects the complementary field when rendering deinterlaced frames
    // transparentMeshes enables transparent mesh rendering enhancement
    // applyMesh determines if the pixel to be applied is a transparent mesh pixel (true) or a regular sprite layer
    // pixel (false).
    template <uint32 colorMode, bool altField, bool transparentMeshes, bool applyMesh>
    void VDP2DrawSpritePixel(uint32 x, const SpriteParams &params, const SpriteFB &spriteFB, uint32 spriteFBOffset);

    // Draws the current VDP2 scanline of the specified normal background layer.
    //
    // y is the scanline to draw
    // colorMode is the CRAM color mode.
    //
    // bgIndex specifies the normal background index, from 0 to 3.
    // deinterlace determines whether to deinterlace video output
    // altField selects the complementary field when rendering deinterlaced frames
    template <uint32 bgIndex, bool deinterlace>
    void VDP2DrawNormalBG(uint32 y, uint32 colorMode, bool altField);

    // Draws the current VDP2 scanline of the specified rotation background layer.
    //
    // y is the scanline to draw
    // colorMode is the CRAM color mode.
    // altField selects the complementary field when rendering deinterlaced frames
    //
    // bgIndex specifies the rotation background index, from 0 to 1.
    template <uint32 bgIndex>
    void VDP2DrawRotationBG(uint32 y, uint32 colorMode, bool altField);

    // Composes the current VDP2 scanline out of the rendered lines.
    //
    // y is the scanline to draw
    // altField selects the complementary field when rendering deinterlaced frames
    //
    // deinterlace determines whether to deinterlace video output
    // transparentMeshes enables transparent mesh rendering enhancement
    template <bool deinterlace, bool transparentMeshes>
    void VDP2ComposeLine(uint32 y, bool altField);

    // Draws a normal scroll BG scanline.
    //
    // y is the scanline to draw
    // bgParams contains the parameters for the BG to draw.
    // layerState is a reference to the common layer state for the background.
    // bgState is a reference to the background layer state for the background.
    // vramFetcher is the corresponding background layer's VRAM fetcher.
    // windowState is a reference to the window state for the layer.
    // altField selects the complementary field when rendering deinterlaced frames
    //
    // charMode indicates if character patterns use two words or one word with standard or extended character data.
    // fourCellChar indicates if character patterns are 1x1 cells (false) or 2x2 cells (true).
    // colorFormat is the color format for cell data.
    // colorMode is the CRAM color mode.
    // useVCellScroll determines whether to use the vertical cell scroll effect
    // deinterlace determines whether to deinterlace video output
    template <CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode, bool useVCellScroll,
              bool deinterlace>
    void VDP2DrawNormalScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                const NormBGLayerState &bgState, VRAMFetcher &vramFetcher,
                                std::span<const bool> windowState, bool altField);

    // Draws a normal bitmap BG scanline.
    //
    // y is the scanline to draw
    // bgParams contains the parameters for the BG to draw.
    // layerState is a reference to the common layer state for the background.
    // bgState is a reference to the background layer state for the background.
    // vramFetcher is the corresponding background layer's VRAM fetcher.
    // windowState is a reference to the window state for the layer.
    // altField selects the complementary field when rendering deinterlaced frames
    //
    // colorFormat is the color format for bitmap data.
    // colorMode is the CRAM color mode.
    // useVCellScroll determines whether to use the vertical cell scroll effect
    // deinterlace determines whether to deinterlace video output
    template <ColorFormat colorFormat, uint32 colorMode, bool useVCellScroll, bool deinterlace>
    void VDP2DrawNormalBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                const NormBGLayerState &bgState, VRAMFetcher &vramFetcher,
                                std::span<const bool> windowState, bool altField);

    // Draws a rotation scroll BG scanline.
    //
    // y is the scanline to draw
    // bgParams contains the parameters for the BG to draw.
    // layerState is a reference to the common layer state for the background.
    // windowState is a reference to the window state for the layer.
    // altField selects the complementary field when rendering deinterlaced frames
    // vramFetcher is the corresponding background layer's VRAM fetcher.
    //
    // bgIndex specifies the rotation background index, from 0 to 1.
    // selRotParam enables dynamic rotation parameter selection (for RBG0).
    // charMode indicates if character patterns use two words or one word with standard or extended character data.
    // fourCellChar indicates if character patterns are 1x1 cells (false) or 2x2 cells (true).
    // colorFormat is the color format for cell data.
    // colorMode is the CRAM color mode.
    template <uint32 bgIndex, bool selRotParam, CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat,
              uint32 colorMode>
    void VDP2DrawRotationScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState, VRAMFetcher &vramFetcher,
                                  std::span<const bool> windowState, bool altField);

    // Draws a rotation bitmap BG scanline.
    //
    // y is the scanline to draw
    // bgParams contains the parameters for the BG to draw.
    // layerState is a reference to the common layer state for the background.
    // windowState is a reference to the window state for the layer.
    // altField selects the complementary field when rendering deinterlaced frames
    //
    // selRotParam enables dynamic rotation parameter selection (for RBG0).
    // colorFormat is the color format for bitmap data.
    // colorMode is the CRAM color mode.
    template <bool selRotParam, ColorFormat colorFormat, uint32 colorMode>
    void VDP2DrawRotationBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                  std::span<const bool> windowState, bool altField);

    // Selects a rotation parameter set based on the current parameter selection mode.
    //
    // x is the horizontal coordinate of the pixel
    // y is the vertical coordinate of the pixel
    // altField selects the complementary field when rendering deinterlaced frames
    RotParamSelector VDP2SelectRotationParameter(uint32 x, uint32 y, bool altField);

    // Determines if a rotation coefficient entry can be fetched from the specified address.
    // Coefficients can always be fetched from CRAM.
    // Coefficients can only be fetched from VRAM if the corresponding bank is designated for coefficient data.
    //
    // params is the rotation parameter from which to retrieve the base address and coefficient data size.
    // coeffAddress is the calculated coefficient address (KA).
    bool VDP2CanFetchCoefficient(const RotationParams &params, uint32 coeffAddress) const;

    // Fetches a rotation coefficient entry from VRAM or CRAM (depending on RAMCTL.CRKTE) using the specified rotation
    // parameters.
    //
    // params is the rotation parameter from which to retrieve the base address and coefficient data size.
    // coeffAddress is the calculated coefficient address (KA).
    Coefficient VDP2FetchRotationCoefficient(const RotationParams &params, uint32 coeffAddress);

    // Fetches a scroll background pixel at the given coordinates.
    //
    // bgParams contains the parameters for the BG to draw.
    // pageBaseAddresses is a reference to the table containing the planes' pages' base addresses.
    // pageShiftH and pageShiftV are address shifts derived from PLSZ to determine the plane and page indices.
    // scrollCoord has the coordinates of the scroll screen.
    // vramFetcher is the corresponding background layer's VRAM fetcher.
    //
    // charMode indicates if character patterns use two words or one word with standard or extended character data.
    // fourCellChar indicates if character patterns are 1x1 cells (false) or 2x2 cells (true).
    // colorFormat is the color format for cell data.
    // colorMode is the CRAM color mode.
    template <bool rot, CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode>
    Pixel VDP2FetchScrollBGPixel(const BGParams &bgParams, std::span<const uint32> pageBaseAddresses, uint32 pageShiftH,
                                 uint32 pageShiftV, CoordU32 scrollCoord, VRAMFetcher &vramFetcher);

    // Fetches a two-word character from VRAM.
    //
    // bgParams contains the parameters for the BG to draw.
    // pageBaseAddress specifies the base address of the page of character patterns.
    // charIndex is the index of the character to fetch.
    Character VDP2FetchTwoWordCharacter(const BGParams &bgParams, uint32 pageBaseAddress, uint32 charIndex);

    // Fetches a one-word character from VRAM.
    //
    // bgParams contains the parameters for the BG to draw.
    // pageBaseAddress specifies the base address of the page of character patterns.
    // charIndex is the index of the character to fetch.
    //
    // fourCellChar indicates if character patterns are 1x1 cells (false) or 2x2 cells (true).
    // largePalette indicates if the color format uses 16 colors (false) or more (true).
    // extChar indicates if the flip bits are available (false) or used to extend the character number (true).
    template <bool fourCellChar, bool largePalette, bool extChar>
    Character VDP2FetchOneWordCharacter(const BGParams &bgParams, uint32 pageBaseAddress, uint32 charIndex);

    // Extract a one-word character from the given raw character data.
    //
    // bgParams contains the parameters for the BG to draw.
    // charData is the raw character data.
    //
    // fourCellChar indicates if character patterns are 1x1 cells (false) or 2x2 cells (true).
    // largePalette indicates if the color format uses 16 colors (false) or more (true).
    // extChar indicates if the flip bits are available (false) or used to extend the character number (true).
    template <bool fourCellChar, bool largePalette, bool extChar>
    Character VDP2ExtractOneWordCharacter(const BGParams &bgParams, uint16 charData);

    // Fetches a pixel in the specified cell in a 2x2 character pattern.
    //
    // cramOffset is the base CRAM offset computed from CRAOFA/CRAOFB.xxCAOSn and vramControl.colorRAMMode.
    // ch is the character's parameters.
    // dotCoord specify the coordinates of the pixel within the cell, ranging from 0 to 7.
    // cellIndex is the index of the cell in the character pattern, ranging from 0 to 3.
    //
    // colorFormat is the value of CHCTLA/CHCTLB.xxCHCNn.
    // colorMode is the CRAM color mode.
    template <ColorFormat colorFormat, uint32 colorMode>
    Pixel VDP2FetchCharacterPixel(const BGParams &bgParams, Character ch, CoordU32 dotCoord, uint32 cellIndex);

    // Fetches a bitmap pixel at the given coordinates.
    //
    // bgParams contains the parameters for the BG to draw.
    // dotCoord specify the coordinates of the pixel within the bitmap.
    // vramFetcher is the corresponding background layer's VRAM fetcher.
    //
    // colorFormat is the color format for pixel data.
    // bitmapBaseAddress is the base address of bitmap data.
    // colorMode is the CRAM color mode.
    template <ColorFormat colorFormat, uint32 colorMode>
    Pixel VDP2FetchBitmapPixel(const BGParams &bgParams, uint32 bitmapBaseAddress, CoordU32 dotCoord,
                               VRAMFetcher &vramFetcher);

    // Fetches a color from CRAM using the current color mode specified by vramControl.colorRAMMode.
    //
    // cramOffset is the base CRAM offset computed from CRAOFA/CRAOFB.xxCAOSn and vramControl.colorRAMMode.
    // colorIndex specifies the color index.
    // colorMode is the CRAM color mode.
    template <uint32 colorMode>
    Color888 VDP2FetchCRAMColor(uint32 cramOffset, uint32 colorIndex);

    // Fetches sprite data based on the current sprite mode.
    //
    // fb is the VDP1 framebuffer to read sprite data from.
    // fbOffset is the offset into the framebuffer (in bytes) where the sprite data is located.
    SpriteData VDP2FetchSpriteData(const SpriteFB &fb, uint32 fbOffset);

    // Fetches 16-bit sprite data based on the current sprite mode.
    //
    // fb is the VDP1 framebuffer to read sprite data from.
    // fbOffset is the offset into the framebuffer (in bytes) where the sprite data is located.
    // type is the sprite type (between 0 and 7).
    SpriteData VDP2FetchWordSpriteData(const SpriteFB &fb, uint32 fbOffset, uint8 type);

    // Fetches 8-bit sprite data based on the current sprite mode.
    //
    // fb is the VDP1 framebuffer to read sprite data from.
    // fbOffset is the offset into the framebuffer (in bytes) where the sprite data is located.
    // type is the sprite type (between 8 and 15).
    SpriteData VDP2FetchByteSpriteData(const SpriteFB &fb, uint32 fbOffset, uint8 type);

    // Retrieves the Y display coordinate based on the current interlace mode.
    //
    // y is the Y coordinate to translate
    //
    // deinterlace determines whether to deinterlace video output
    template <bool deinterlace>
    uint32 VDP2GetY(uint32 y) const;

public:
    // -------------------------------------------------------------------------
    // Debugger

    class Probe {
    public:
        explicit Probe(VDP &vdp);

        [[nodiscard]] Dimensions GetResolution() const;
        [[nodiscard]] InterlaceMode GetInterlaceMode() const;

        [[nodiscard]] const VDP1Regs &GetVDP1Regs() const;
        [[nodiscard]] const VDP2Regs &GetVDP2Regs() const;

        [[nodiscard]] const std::array<NormBGLayerState, 4> &GetNBGLayerStates() const;

        template <mem_primitive T>
        void VDP1WriteVRAM(uint32 address, T value);

        void VDP1WriteReg(uint32 address, uint16 value);

    private:
        VDP &m_vdp;
    };

    Probe &GetProbe() {
        return m_probe;
    }

    const Probe &GetProbe() const {
        return m_probe;
    }

private:
    Probe m_probe{*this};
};

} // namespace ymir::vdp
