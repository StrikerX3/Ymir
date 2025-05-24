#pragma once

/**
@file
@brief VDP1 and VDP2 implementation.
*/

#include "vdp_defs.hpp"

#include "vdp1_regs.hpp"
#include "vdp2_regs.hpp"

#include "vdp_callbacks.hpp"
#include "vdp_internal_callbacks.hpp"

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

    void MapCallbacks(CBHVBlankStateChange cbHBlankStateChange, CBHVBlankStateChange cbVBlankStateChange,
                      CBTriggerEvent cbSpriteDrawEnd, CBTriggerEvent cbOptimizedINTBACKRead) {
        m_cbHBlankStateChange = cbHBlankStateChange;
        m_cbVBlankStateChange = cbVBlankStateChange;
        m_cbTriggerSpriteDrawEnd = cbSpriteDrawEnd;
        m_cbTriggerOptimizedINTBACKRead = cbOptimizedINTBACKRead;
    }

    void MapMemory(sys::Bus &bus);

    void SetRenderCallback(CBFrameComplete cbFrameComplete) {
        m_cbFrameComplete = cbFrameComplete;
    }

    void SetVDP1Callback(CBVDP1FrameComplete cbFrameComplete) {
        m_cbVDP1FrameComplete = cbFrameComplete;
    }

    // TODO: replace with scheduler events
    template <bool debug>
    void Advance(uint64 cycles);

    void DumpVDP1VRAM(std::ostream &out) const;
    void DumpVDP2VRAM(std::ostream &out) const;
    void DumpVDP2CRAM(std::ostream &out) const;

    // Dumps draw framebuffer followed by display framebuffer
    void DumpVDP1Framebuffers(std::ostream &out) const;

    bool InLastLinePhase() const {
        return m_VPhase == VerticalPhase::LastLine;
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::VDPState &state) const;
    bool ValidateState(const state::VDPState &state) const;
    void LoadState(const state::VDPState &state);

    // -------------------------------------------------------------------------
    // Rendering control

    // Layers
    enum class Layer { Sprite, RBG0, NBG0_RBG1, NBG1_EXBG, NBG2, NBG3 };

    // Enables or disables a layer.
    // Useful for debugging and troubleshooting.
    void SetLayerEnabled(Layer layer, bool enabled);

    // Detemrines if a layer is forcibly disabled.
    bool IsLayerEnabled(Layer layer) const;

private:
    alignas(16) std::array<uint8, kVDP1VRAMSize> m_VRAM1;
    alignas(16) std::array<uint8, kVDP2VRAMSize> m_VRAM2; // 4x 128 KiB banks: A0, A1, B0, B1
    alignas(16) std::array<uint8, kVDP2CRAMSize> m_CRAM;
    alignas(16) std::array<std::array<uint8, kVDP1FramebufferRAMSize>, 2> m_spriteFB;
    uint8 m_displayFB; // index of current sprite display buffer, CPU-accessible; opposite buffer is drawn into

    // Cached CRAM colors converted from RGB555 to RGB888.
    // Only valid when color RAM mode is one of the RGB555 modes.
    alignas(16) std::array<Color888, kVDP2CRAMSize / sizeof(uint16)> m_CRAMCache;

    CBHVBlankStateChange m_cbHBlankStateChange;
    CBHVBlankStateChange m_cbVBlankStateChange;
    CBTriggerEvent m_cbTriggerSpriteDrawEnd;
    CBTriggerEvent m_cbTriggerOptimizedINTBACKRead;

    core::Scheduler &m_scheduler;
    core::EventID m_phaseUpdateEvent;

    static void OnPhaseUpdateEvent(core::EventContext &eventContext, void *userContext);

    using VideoStandard = ::ymir::core::config::sys::VideoStandard;
    void SetVideoStandard(VideoStandard videoStandard);

    // -------------------------------------------------------------------------
    // Configuration

    void EnableThreadedVDP(bool enable);

    // -------------------------------------------------------------------------
    // VDP1 memory/register access

    template <mem_primitive T>
    T VDP1ReadVRAM(uint32 address);

    template <mem_primitive T>
    void VDP1WriteVRAM(uint32 address, T value);

    template <mem_primitive T>
    T VDP1ReadFB(uint32 address);

    template <mem_primitive T>
    void VDP1WriteFB(uint32 address, T value);

    template <bool peek>
    uint16 VDP1ReadReg(uint32 address);
    template <bool poke>
    void VDP1WriteReg(uint32 address, uint16 value);

    // -------------------------------------------------------------------------
    // VDP2 memory/register access

    template <mem_primitive T>
    T VDP2ReadVRAM(uint32 address);

    template <mem_primitive T>
    void VDP2WriteVRAM(uint32 address, T value);

    template <mem_primitive T, bool peek>
    T VDP2ReadCRAM(uint32 address);

    template <mem_primitive T, bool poke>
    void VDP2WriteCRAM(uint32 address, T value);

    template <mem_primitive T>
    void VDP2UpdateCRAMCache(uint32 address);

    uint16 VDP2ReadReg(uint32 address);
    void VDP2WriteReg(uint32 address, uint16 value);

    // -------------------------------------------------------------------------
    // Frontend callbacks

    // Invoked when the VDP1 finishes drawing a frame.
    CBVDP1FrameComplete m_cbVDP1FrameComplete;

    // Invoked when the renderer finishes drawing a frame.
    CBFrameComplete m_cbFrameComplete;

    // -------------------------------------------------------------------------
    // Registers

    VDP1Regs m_VDP1;
    VDP2Regs m_VDP2;

    // -------------------------------------------------------------------------

    static constexpr auto kCRAMAddressMapping = [] {
        std::array<std::array<uint32, 4096>, 2> addrs{};
        for (uint32 addr = 0; addr < 4096; addr++) {
            addrs[0][addr] = addr;
            addrs[1][addr] = bit::extract<0>(addr) | (bit::extract<11>(addr) << 1u) | (bit::extract<1, 10>(addr) << 2u);
        }
        return addrs;
    }();

    // RAMCTL.CRMD modes 2 and 3 shuffle address bits as follows:
    //   10 09 08 07 06 05 04 03 02 01 11 00
    //   in short, bits 10-01 are shifted left and bit 11 takes place of bit 01
    FORCE_INLINE uint32 MapCRAMAddress(uint32 address) const {
        return kCRAMAddressMapping[m_VDP2.vramControl.colorRAMMode >> 1][address & 0xFFF];
    }

    // RAMCTL.CRMD modes 2 and 3 shuffle address bits as follows:
    //   10 09 08 07 06 05 04 03 02 01 11 00
    //   in short, bits 10-01 are shifted left and bit 11 takes place of bit 01
    FORCE_INLINE uint32 MapRendererCRAMAddress(uint32 address) const {
        return kCRAMAddressMapping[m_VDPRenderContext.vdp2.regs.vramControl.colorRAMMode >> 1][address & 0xFFF];
    }

    // -------------------------------------------------------------------------
    // Timings and signals

    // Based on https://github.com/srg320/Saturn_hw/blob/main/VDP2/VDP2.xlsx

    // Horizontal display phases:
    // NOTE: each dot takes 4 system (SH-2) cycles on standard resolutions, 2 system cycles on hi-res modes
    // NOTE: hi-res modes doubles all HCNTs
    //
    //   320 352  dots
    // --------------------------------
    //     0   0  Active display area
    //   320 352  Right border
    //   347 375  Horizontal sync
    //   374 403  VBlank OUT
    //   400 432  Left border
    //   426 454  Last dot
    //   427 455  Total HCNT
    //
    // Vertical display phases:
    // NOTE: bottom blanking, vertical sync and top blanking are consolidated into a single phase since no important
    // events happen other than not drawing the border
    //
    //    NTSC    --  PAL  --
    //   224 240  224 240 256  lines
    // ---------------------------------------------
    //     0   0    0   0   0  Active display area
    //   224 240  224 240 256  Bottom border
    //   232 240  256 264 272  Bottom blanking | these are
    //   237 245  259 267 275  Vertical sync   | merged into
    //   240 248  262 270 278  Top blanking    | one phase
    //   255 255  281 289 297  Top border
    //   262 262  312 312 312  Last line
    //   263 263  313 313 313  Total VCNT
    //
    // Events:
    //   VBLANK signal is raised when entering bottom border V phase
    //   VBLANK signal is lowered when entering VBlank clear H phase during last line V phase
    //
    //   HBLANK signal is raised when entering right border H phase (closest match, 4 cycles early)
    //   HBLANK signal is lowered when entering left border H phase (closest match, 10 cycles early)
    //
    //   Even/odd field flag is flipped when entering last dot H phase during first line of bottom border V phase
    //
    //   VBlank IN/OUT interrupts are raised when the VBLANK signal is raised/lowered
    //   HBlank IN interrupt is raised when the HBLANK signal is raised
    //
    //   Drawing happens when in both active display area phases
    //   Border drawing happens when in any of the border phases

    enum class HorizontalPhase { Active, RightBorder, Sync, VBlankOut, LeftBorder, LastDot };
    HorizontalPhase m_HPhase; // Current horizontal display phase

    enum class VerticalPhase { Active, BottomBorder, BlankingAndSync, TopBorder, LastLine };
    VerticalPhase m_VPhase; // Current vertical display phase

    // 180008   HCNT    H Counter
    //
    //   bits   r/w  code          description
    //  15-10        -             Reserved, must be zero
    //    9-0   R    HCT9-0        H Counter Value
    //
    // Notes
    // - Counter layout depends on screen mode:
    //     Normal: bits 8-0 shifted left by 1; HCT0 is invalid
    //     Hi-Res: bits 9-0
    //     Excl. Normal: bits 8-0 (no shift); HCT9 is invalid
    //     Excl. Hi-Res: bits 9-1 shifted right by 1; HCT9 is invalid
    //
    // 18000A   VCNT    V Counter
    //
    //   bits   r/w  code          description
    //  15-10        -             Reserved, must be zero
    //    9-0   R    VCT9-0        V Counter Value
    //
    // Notes
    // - Counter layout depends on screen mode:
    //     Exclusive Monitor: bits 9-0
    //     Normal Hi-Res double-density interlace:
    //       bits 8-0 shifted left by 1
    //       bit 0 contains interlaced field (0=odd, 1=even)
    //     All other modes: bits 8-0 shifted left by 1; VCT0 is invalid

    // TODO: store latched H/V counters
    uint16 m_VCounter;

    // Display resolution (derived from TVMODE)
    uint32 m_HRes; // Horizontal display resolution
    uint32 m_VRes; // Vertical display resolution

    // Display timings
    std::array<uint32, 6> m_HTimings;
    std::array<uint32, 5> m_VTimings;

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
    void BeginHPhaseVBlankOut();
    void BeginHPhaseLeftBorder();
    void BeginHPhaseLastDot();

    void BeginVPhaseActiveDisplay();
    void BeginVPhaseBottomBorder();
    void BeginVPhaseBlankingAndSync();
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

        static VDPRenderEvent Shutdown() {
            return {Type::Shutdown};
        }
    };

    mutable struct VDPRenderContext {
        struct QueueTraits : moodycamel::ConcurrentQueueDefaultTraits {
            static const size_t BLOCK_SIZE = 64;
            static const size_t EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD = 64;
            static const std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = 512;
            static const int MAX_SEMA_SPINS = 20000;
        };

        moodycamel::BlockingConcurrentQueue<VDPRenderEvent, QueueTraits> eventQueue;
        moodycamel::ProducerToken pTok{eventQueue};
        moodycamel::ConsumerToken cTok{eventQueue};
        util::Event renderFinishedSignal{false};
        util::Event framebufferSwapSignal{false};
        util::Event preSaveSyncSignal{false};
        util::Event postLoadSyncSignal{false};

        std::array<VDPRenderEvent, 64> pendingEvents;
        size_t pendingEventsCount = 0;

        bool vdp1Done;

        struct VDP1 {
            VDP1Regs regs;
            alignas(16) std::array<uint8, kVDP1VRAMSize> VRAM;
            // alignas(16) std::array<std::array<uint8, kVDP1FramebufferRAMSize>, 2> spriteFB;
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
                eventQueue.enqueue(pTok, std::move(event));
                break;
            }
        }

        template <typename It>
        size_t DequeueEvents(It first, size_t count) {
            return eventQueue.wait_dequeue_bulk(cTok, first, count);
        }
    } m_VDPRenderContext;

    std::thread m_VDPRenderThread;
    bool m_threadedVDPRendering = false;

    void VDPRenderThread();

    template <mem_primitive T>
    T VDP1ReadRendererVRAM(uint32 address);

    template <mem_primitive T>
    T VDP2ReadRendererVRAM(uint32 address);

    template <mem_primitive T>
    T VDP2ReadRendererCRAM(uint32 address);

    Color888 VDP2ReadRendererColor5to8(uint32 address);

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

        bool rendering;

        bool erase;

        uint64 cycleCount;
    } m_VDP1RenderContext;

    struct VDP1GouraudParams {
        Color555 colorA;
        Color555 colorB;
        Color555 colorC;
        Color555 colorD;
        uint32 U; // 16 fractional bits, from 0.0 to 1.0
        uint32 V; // 16 fractional bits, from 0.0 to 1.0
    };

    struct VDP1PixelParams {
        VDP1Command::DrawMode mode;
        uint16 color;
    };

    struct VDP1TexturedLineParams {
        VDP1Command::Control control;
        VDP1Command::DrawMode mode;
        uint32 colorBank;
        uint32 charAddr;
        uint32 charSizeH;
        uint32 charSizeV;
        uint64 texFracV;
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
        alignas(16) std::bitset<kMaxResH> transparent;
        alignas(16) std::bitset<kMaxResH> specialColorCalc;

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
    };

    // Layer state, containing the pixel output for the current scanline.
    struct alignas(4096) LayerState {
        LayerState() {
            rendered = true;
            Reset();
        }

        void Reset() {
            pixels.color.fill({});
            pixels.priority.fill({});
            pixels.transparent.reset();
            pixels.specialColorCalc.reset();
            enabled = false;
        }

        alignas(16) Pixels pixels;

        bool enabled; // Enabled by BGON and other factors

        bool rendered; // Enabled for rendering (externally configured - do not include in save state!)
    };

    // Layer state specific to the sprite layer.
    // Includes additional pixel attributes for each pixel in the scanline.
    struct SpriteLayerState {
        SpriteLayerState() {
            Reset();
        }

        void Reset() {
            attrs.fill({});
        }

        struct Attributes {
            uint8 colorCalcRatio = 0;
            bool shadowOrWindow = false;
            bool normalShadow = false;
        };

        alignas(16) std::array<Attributes, kMaxResH> attrs;
        alignas(16) std::array<bool, kMaxResH> window;
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
            mosaicCounterY = 0;
        }

        // Initial fractional X scroll coordinate.
        uint32 fracScrollX;

        // Fractional Y scroll coordinate.
        // Reset at the start of every frame and updated every scanline.
        uint32 fracScrollY;

        // Fractional X scroll coordinate increment.
        // Applied every pixel and updated every scanline.
        uint32 scrollIncH;

        // Current line scroll table address.
        // Reset at the start of every frame and incremented every 1/2/4/8/16 lines.
        uint32 lineScrollTableAddress;

        // Vertical cell scroll offset.
        // Based on CYCA0/A1/B0/B1 parameters.
        uint32 vertCellScrollOffset;

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
            pageBaseAddresses.fill(0);
            screenCoords.fill({});
            lineColor.fill({.u32 = 0});
            transparent.fill(0);
            scrX = scrY = 0;
            KA = 0;
        }

        // Page base addresses for RBG planes A-P using Rotation Parameters A and B.
        // Indexing: [Plane A-P]
        // Derived from mapIndices, CHCTLA/CHCTLB.xxCHSZ, PNCR.xxPNB and PLSZ.xxPLSZn
        std::array<uint32, 16> pageBaseAddresses;

        // Precomputed screen coordinates (with 16 fractional bits).
        alignas(16) std::array<CoordS32, kMaxResH> screenCoords;

        // Precomputed coefficient table line color.
        // Filled in only if the coefficient table is enabled and using line color data.
        alignas(16) std::array<Color888, kMaxResH> lineColor;

        // Prefetched coefficient table transparency bits.
        // Filled in only if the coefficient table is enabled.
        alignas(16) std::array<bool, kMaxResH> transparent;

        // Current base screen coordinates, updated every scanline.
        sint32 scrX, scrY;

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

    // Common layer states
    //     RBG0+RBG1   RBG0        no RBGs
    // [0] Sprite      Sprite      Sprite
    // [1] RBG0        RBG0        -
    // [2] RBG1        NBG0        NBG0
    // [3] EXBG        NBG1/EXBG   NBG1/EXBG
    // [4] -           NBG2        NBG2
    // [5] -           NBG3        NBG3
    std::array<LayerState, 6> m_layerStates;

    // Sprite layer state
    SpriteLayerState m_spriteLayerState;

    // Layer state for NBGs 0-3
    std::array<NormBGLayerState, 4> m_normBGLayerStates;

    // States for Rotation Parameters A and B.
    std::array<RotationParamState, 2> m_rotParamStates;

    // State for the line color and back screens.
    LineBackLayerState m_lineBackLayerState;

    // Window state for NBGs and RBGs.
    // [0] RBG0
    // [1] NBG0/RBG1
    // [2] NBG1/EXBG
    // [3] NBG2
    // [4] NBG3
    alignas(16) std::array<std::array<bool, kMaxResH>, 5> m_bgWindows;

    // Window state for rotation parameters.
    alignas(16) std::array<bool, kMaxResH> m_rotParamsWindow;

    // Window state for color calculation.
    alignas(16) std::array<bool, kMaxResH> m_colorCalcWindow;

    // Vertical cell scroll increment.
    // Based on CYCA0/A1/B0/B1 parameters.
    uint32 m_vertCellScrollInc;

    // Current display framebuffer.
    std::array<uint32, kMaxResH * kMaxResV> m_framebuffer;

    // Retrieves the current set of VDP1 registers.
    VDP1Regs &VDP1GetRegs();

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

    // Processes a single commmand from the VDP1 command table.
    void VDP1ProcessCommand();

    bool VDP1IsPixelUserClipped(CoordS32 coord) const;
    bool VDP1IsPixelSystemClipped(CoordS32 coord) const;
    bool VDP1IsLineSystemClipped(CoordS32 coord1, CoordS32 coord2) const;
    bool VDP1IsQuadSystemClipped(CoordS32 coord1, CoordS32 coord2, CoordS32 coord3, CoordS32 coord4) const;

    void VDP1PlotPixel(CoordS32 coord, const VDP1PixelParams &pixelParams, const VDP1GouraudParams &gouraudParams);
    void VDP1PlotLine(CoordS32 coord1, CoordS32 coord2, const VDP1PixelParams &pixelParams,
                      VDP1GouraudParams &gouraudParams);
    void VDP1PlotTexturedLine(CoordS32 coord1, CoordS32 coord2, const VDP1TexturedLineParams &lineParams,
                              VDP1GouraudParams &gouraudParams);

    // Individual VDP1 command processors

    void VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control);
    void VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control);
    void VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control);

    void VDP1Cmd_DrawPolygon(uint32 cmdAddress);
    void VDP1Cmd_DrawPolylines(uint32 cmdAddress);
    void VDP1Cmd_DrawLine(uint32 cmdAddress);

    void VDP1Cmd_SetSystemClipping(uint32 cmdAddress);
    void VDP1Cmd_SetUserClipping(uint32 cmdAddress);
    void VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress);

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

    // Initializes the specified RBG.
    template <uint32 index>
    void VDP2InitRotationBG();

    // Updates the enabled backgrounds.
    void VDP2UpdateEnabledBGs();

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
    void VDP2CalcWindows(uint32 y);

    // Precalculates window state for a given set of parameters.
    //
    // y is the scanline to draw
    // windowSet contains the windows
    // windowParams contains additional window parameters
    // windowState is the window state output
    template <bool hasSpriteWindow>
    void VDP2CalcWindow(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                        const std::array<WindowParams, 2> &windowParams, std::array<bool, kMaxResH> &windowState);

    // Precalculates window state for a given set of parameters using AND logic.
    //
    // y is the scanline to draw
    // windowSet contains the windows
    // windowParams contains additional window parameters
    // windowState is the window state output
    template <bool hasSpriteWindow>
    void VDP2CalcWindowAnd(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                           const std::array<WindowParams, 2> &windowParams, std::array<bool, kMaxResH> &windowState);

    // Precalculates window state for a given set of parameters using OR logic.
    //
    // y is the scanline to draw
    // windowSet contains the windows
    // windowParams contains additional window parameters
    // windowState is the window state output
    template <bool hasSpriteWindow>
    void VDP2CalcWindowOr(uint32 y, const WindowSet<hasSpriteWindow> &windowSet,
                          const std::array<WindowParams, 2> &windowParams, std::array<bool, kMaxResH> &windowState);

    // Computes the access cycles for NBGs and RBGs.
    void VDP2CalcAccessCycles();

    // Draws the specified VDP2 scanline.
    //
    // y is the scanline to draw
    void VDP2DrawLine(uint32 y);

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
    template <uint32 colorMode, bool rotate>
    void VDP2DrawSpriteLayer(uint32 y);

    // Draws the current VDP2 scanline of the specified normal background layer.
    //
    // y is the scanline to draw
    // colorMode is the CRAM color mode.
    //
    // bgIndex specifies the normal background index, from 0 to 3.
    template <uint32 bgIndex>
    void VDP2DrawNormalBG(uint32 y, uint32 colorMode);

    // Draws the current VDP2 scanline of the specified rotation background layer.
    //
    // y is the scanline to draw
    // colorMode is the CRAM color mode.
    //
    // bgIndex specifies the rotation background index, from 0 to 1.
    template <uint32 bgIndex>
    void VDP2DrawRotationBG(uint32 y, uint32 colorMode);

    // Composes the current VDP2 scanline out of the rendered lines.
    //
    // y is the scanline to draw
    void VDP2ComposeLine(uint32 y);

    // Draws a normal scroll BG scanline.
    //
    // y is the scanline to draw
    // bgParams contains the parameters for the BG to draw.
    // layerState is a reference to the common layer state for the background.
    // bgState is a reference to the background layer state for the background.
    // windowState is a reference to the window state for the layer.
    //
    // charMode indicates if character patterns use two words or one word with standard or extended character data.
    // fourCellChar indicates if character patterns are 1x1 cells (false) or 2x2 cells (true).
    // colorFormat is the color format for cell data.
    // colorMode is the CRAM color mode.
    template <CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode>
    void VDP2DrawNormalScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState, NormBGLayerState &bgState,
                                const std::array<bool, kMaxResH> &windowState);

    // Draws a normal bitmap BG scanline.
    //
    // y is the scanline to draw
    // bgParams contains the parameters for the BG to draw.
    // layerState is a reference to the common layer state for the background.
    // bgState is a reference to the background layer state for the background.
    // windowState is a reference to the window state for the layer.
    //
    // colorFormat is the color format for bitmap data.
    // colorMode is the CRAM color mode.
    template <ColorFormat colorFormat, uint32 colorMode>
    void VDP2DrawNormalBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState, NormBGLayerState &bgState,
                                const std::array<bool, kMaxResH> &windowState);

    // Draws a rotation scroll BG scanline.
    //
    // y is the scanline to draw
    // bgParams contains the parameters for the BG to draw.
    // layerState is a reference to the common layer state for the background.
    // windowState is a reference to the window state for the layer.
    //
    // selRotParam enables dynamic rotation parameter selection (for RBG0).
    // charMode indicates if character patterns use two words or one word with standard or extended character data.
    // fourCellChar indicates if character patterns are 1x1 cells (false) or 2x2 cells (true).
    // colorFormat is the color format for cell data.
    // colorMode is the CRAM color mode.
    template <bool selRotParam, CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode>
    void VDP2DrawRotationScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                  const std::array<bool, kMaxResH> &windowState);

    // Draws a rotation bitmap BG scanline.
    //
    // y is the scanline to draw
    // bgParams contains the parameters for the BG to draw.
    // layerState is a reference to the common layer state for the background.
    // windowState is a reference to the window state for the layer.
    //
    // selRotParam enables dynamic rotation parameter selection (for RBG0).
    // colorFormat is the color format for bitmap data.
    // colorMode is the CRAM color mode.
    template <bool selRotParam, ColorFormat colorFormat, uint32 colorMode>
    void VDP2DrawRotationBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                  const std::array<bool, kMaxResH> &windowState);

    // Selects a rotation parameter set based on the current parameter selection mode.
    //
    // x is the horizontal coordinate of the pixel
    // y is the vertical coordinate of the pixel
    RotParamSelector VDP2SelectRotationParameter(uint32 x, uint32 y);

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
    //
    // charMode indicates if character patterns use two words or one word with standard or extended character data.
    // fourCellChar indicates if character patterns are 1x1 cells (false) or 2x2 cells (true).
    // colorFormat is the color format for cell data.
    // colorMode is the CRAM color mode.
    template <bool rot, CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode>
    Pixel VDP2FetchScrollBGPixel(const BGParams &bgParams, std::span<const uint32> pageBaseAddresses, uint32 pageShiftH,
                                 uint32 pageShiftV, CoordU32 scrollCoord);

    // Fetches a two-word character from VRAM.
    //
    // pageBaseAddress specifies the base address of the page of character patterns.
    // charIndex is the index of the character to fetch.
    Character VDP2FetchTwoWordCharacter(uint32 pageBaseAddress, uint32 charIndex);

    // Fetches a one-word character from VRAM.
    //
    // bgParams contains the parameters for the BG to draw.
    // pageBaseAddress specifies the base address of the page of character patterns.
    // charIndex is the index of the character to fetch.
    // fourCellChar indicates if character patterns are 1x1 cells (false) or 2x2 cells (true).
    // largePalette indicates if the color format uses 16 colors (false) or more (true).
    // extChar indicates if the flip bits are available (false) or used to extend the character number (true).
    template <bool fourCellChar, bool largePalette, bool extChar>
    Character VDP2FetchOneWordCharacter(const BGParams &bgParams, uint32 pageBaseAddress, uint32 charIndex);

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
    //
    // colorFormat is the color format for pixel data.
    // bitmapBaseAddress is the base address of bitmap data.
    // colorMode is the CRAM color mode.
    template <ColorFormat colorFormat, uint32 colorMode>
    Pixel VDP2FetchBitmapPixel(const BGParams &bgParams, uint32 bitmapBaseAddress, CoordU32 dotCoord);

    // Fetches a color from CRAM using the current color mode specified by vramControl.colorRAMMode.
    //
    // cramOffset is the base CRAM offset computed from CRAOFA/CRAOFB.xxCAOSn and vramControl.colorRAMMode.
    // colorIndex specifies the color index.
    // colorMode is the CRAM color mode.
    template <uint32 colorMode>
    Color888 VDP2FetchCRAMColor(uint32 cramOffset, uint32 colorIndex);

    // Fetches sprite data based on the current sprite mode.
    //
    // fbOffset is the offset into the framebuffer (in bytes) where the sprite data is located.
    SpriteData VDP2FetchSpriteData(uint32 fbOffset);

    // Fetches 16-bit sprite data based on the current sprite mode.
    //
    // fbOffset is the offset into the framebuffer (in bytes) where the sprite data is located.
    // type is the sprite type (between 0 and 7).
    SpriteData VDP2FetchWordSpriteData(uint32 fbOffset, uint8 type);

    // Fetches 8-bit sprite data based on the current sprite mode.
    //
    // fbOffset is the offset into the framebuffer (in bytes) where the sprite data is located.
    // type is the sprite type (between 8 and 15).
    SpriteData VDP2FetchByteSpriteData(uint32 fbOffset, uint8 type);

    // Determines the type of sprite shadow (if any) based on color data.
    //
    // colorData is the raw color data.
    //
    // colorDataBits specifies the bit width of the color data.
    template <uint32 colorDataBits>
    static bool VDP2IsNormalShadow(uint16 colorData);

    // Retrieves the Y display coordinate based on the current interlace mode.
    //
    // y is the Y coordinate to translate
    uint32 VDP2GetY(uint32 y) const;

public:
    // -------------------------------------------------------------------------
    // Debugger

    class Probe {
    public:
        Probe(VDP &vdp);

        Dimensions GetResolution() const;
        InterlaceMode GetInterlaceMode() const;

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
