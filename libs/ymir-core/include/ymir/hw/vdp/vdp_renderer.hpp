#pragma once

#include <ymir/hw/hw_defs.hpp>
#include <ymir/hw/vdp/vdp_callbacks.hpp>
#include <ymir/hw/vdp/vdp_state.hpp>

#include <ymir/util/event.hpp>
#include <ymir/util/inline.hpp>

#include <ymir/core/types.hpp>

#include <blockingconcurrentqueue.h>

#include <iosfwd>
#include <span>
#include <thread>

namespace ymir::vdp {

// -----------------------------------------------------------------------------
// Forward declarations

class VDP;

// -----------------------------------------------------------------------------
// Enums

// Layers
enum class Layer { Sprite, RBG0, NBG0_RBG1, NBG1_EXBG, NBG2, NBG3 };

struct RenderEvent {
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
        VDP1StateSync,

        UpdateEffectiveRenderingFlags,

        Shutdown,
    };

    Type type;
    union {
        struct {
            bool hard;
        } reset;

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

    static RenderEvent Reset(bool hard) {
        return {Type::Reset, {.reset = {.hard = hard}}};
    }

    static RenderEvent OddField(bool odd) {
        return {Type::OddField, {.oddField = {.odd = odd}}};
    }

    static RenderEvent VDP1EraseFramebuffer() {
        return {Type::VDP1EraseFramebuffer};
    }

    static RenderEvent VDP1SwapFramebuffer() {
        return {Type::VDP1SwapFramebuffer};
    }

    static RenderEvent VDP1BeginFrame() {
        return {Type::VDP1BeginFrame};
    }

    /*static VDP1RenderEvent VDP1ProcessCommands(uint64 steps) {
        return {Type::VDP1ProcessCommands, {.processCommands = {.steps = steps}}};
    }*/

    static RenderEvent VDP2DrawLine(uint32 vcnt) {
        return {Type::VDP2DrawLine, {.drawLine = {.vcnt = vcnt}}};
    }

    static RenderEvent VDP2EndFrame() {
        return {Type::VDP2EndFrame};
    }

    template <mem_primitive T>
    static RenderEvent VDP1VRAMWrite(uint32 address, T value) {
        static_assert(!std::is_same_v<T, uint32>, "unsupported write size");

        if constexpr (std::is_same_v<T, uint8>) {
            return VDP1VRAMWriteByte(address, value);
        } else if constexpr (std::is_same_v<T, uint16>) {
            return VDP1VRAMWriteWord(address, value);
        }
    }

    static RenderEvent VDP1VRAMWriteByte(uint32 address, uint8 value) {
        return {Type::VDP1VRAMWriteByte, {.write = {.address = address, .value = value}}};
    }

    static RenderEvent VDP1VRAMWriteWord(uint32 address, uint16 value) {
        return {Type::VDP1VRAMWriteWord, {.write = {.address = address, .value = value}}};
    }

    /*template <mem_primitive T>
    static RenderEvent VDP1FBWrite(uint32 address, T value) {
        static_assert(!std::is_same_v<T, uint32>, "unsupported write size");

        if constexpr (std::is_same_v<T, uint8>) {
            return VDP1FBWriteByte(address, value);
        } else if constexpr (std::is_same_v<T, uint16>) {
            return VDP1FBWriteWord(address, value);
        }
    }

    static RenderEvent VDP1FBWriteByte(uint32 address, uint8 value) {
        return {Type::VDP1FBWriteByte, {.write = {.address = address, .value = value}}};
    }

    static RenderEvent VDP1FBWriteWord(uint32 address, uint16 value) {
        return {Type::VDP1FBWriteWord, {.write = {.address = address, .value = value}}};
    }*/

    static RenderEvent VDP1RegWrite(uint32 address, uint16 value) {
        return {Type::VDP1RegWrite, {.write = {.address = address, .value = value}}};
    }

    template <mem_primitive T>
    static RenderEvent VDP2VRAMWrite(uint32 address, T value) {
        static_assert(!std::is_same_v<T, uint32>, "unsupported write size");

        if constexpr (std::is_same_v<T, uint8>) {
            return VDP2VRAMWriteByte(address, value);
        } else if constexpr (std::is_same_v<T, uint16>) {
            return VDP2VRAMWriteWord(address, value);
        }
    }

    static RenderEvent VDP2VRAMWriteByte(uint32 address, uint8 value) {
        return {Type::VDP2VRAMWriteByte, {.write = {.address = address, .value = value}}};
    }

    static RenderEvent VDP2VRAMWriteWord(uint32 address, uint16 value) {
        return {Type::VDP2VRAMWriteWord, {.write = {.address = address, .value = value}}};
    }

    template <mem_primitive T>
    static RenderEvent VDP2CRAMWrite(uint32 address, T value) {
        static_assert(!std::is_same_v<T, uint32>, "unsupported write size");

        if constexpr (std::is_same_v<T, uint8>) {
            return VDP2CRAMWriteByte(address, value);
        } else if constexpr (std::is_same_v<T, uint16>) {
            return VDP2CRAMWriteWord(address, value);
        }
    }

    static RenderEvent VDP2CRAMWriteByte(uint32 address, uint8 value) {
        return {Type::VDP2CRAMWriteByte, {.write = {.address = address, .value = value}}};
    }

    static RenderEvent VDP2CRAMWriteWord(uint32 address, uint16 value) {
        return {Type::VDP2CRAMWriteWord, {.write = {.address = address, .value = value}}};
    }

    static RenderEvent VDP2RegWrite(uint32 address, uint16 value) {
        return {Type::VDP2RegWrite, {.write = {.address = address, .value = value}}};
    }

    static RenderEvent PreSaveStateSync() {
        return {Type::PreSaveStateSync};
    }

    static RenderEvent PostLoadStateSync() {
        return {Type::PostLoadStateSync};
    }

    static RenderEvent VDP1StateSync() {
        return {Type::VDP1StateSync};
    }

    static RenderEvent UpdateEffectiveRenderingFlags() {
        return {Type::UpdateEffectiveRenderingFlags};
    }

    static RenderEvent Shutdown() {
        return {Type::Shutdown};
    }
};

// -----------------------------------------------------------------------------
// Renderer declaration

class VDPRenderer {
public:
    VDPRenderer(VDPState &mainState);
    ~VDPRenderer();

    void Reset(bool hard);

    template <bool debug>
    void Advance(uint64 cycles);

    void EnqueueEvent(RenderEvent &&event);

    void SetRenderCallback(CBFrameComplete callback) {
        m_cbFrameComplete = callback;
    }

    void SetVDP1Callback(CBVDP1FrameComplete callback) {
        m_cbVDP1FrameComplete = callback;
    }

    // -------------------------------------------------------------------------
    // Memory accessors

    template <mem_primitive T>
    FORCE_INLINE void VDP1WriteVRAM(uint32 address, T value) {
        if (m_effectiveRenderVDP1InVDP2Thread) {
            EnqueueEvent(RenderEvent::VDP1VRAMWrite<T>(address, value));
        }
    }

    template <mem_primitive T>
    FORCE_INLINE void VDP1WriteFB(uint32 address, T value) {
        if (m_deinterlaceRender) {
            util::WriteBE<T>(&m_altSpriteFB[m_mainState.displayFB ^ 1][address & 0x3FFFF], value);
        }
        // if (m_effectiveRenderVDP1InVDP2Thread) {
        //     EnqueueEvent(RenderEvent::VDP1FBWrite<T>(address, value));
        // }
    }

    template <bool poke>
    FORCE_INLINE void VDP1WriteReg(uint32 address, uint16 value) {
        if (m_effectiveRenderVDP1InVDP2Thread) {
            EnqueueEvent(RenderEvent::VDP1RegWrite(address, value));
        }

        if (address == 0x0C) {
            // ENDR

            // TODO: schedule drawing termination after 30 cycles
            m_VDP1RenderContext.rendering = false;
        }
    }

    template <mem_primitive T>
    FORCE_INLINE void VDP2WriteVRAM(uint32 address, T value) {
        if (m_threadedRendering) {
            EnqueueEvent(RenderEvent::VDP2VRAMWrite<T>(address, value));
        }
    }

    template <mem_primitive T>
    FORCE_INLINE void VDP2WriteCRAM(uint32 address, T value) {
        if (m_threadedRendering) {
            EnqueueEvent(RenderEvent::VDP2CRAMWrite<T>(address, value));
        } else {
            VDP2UpdateCRAMCache<T>(address);
        }
    }

    FORCE_INLINE void VDP2WriteReg(uint32 address, uint16 value) {
        if (m_threadedRendering) {
            EnqueueEvent(RenderEvent::VDP2RegWrite(address, value));
        }

        switch (address) {
        case 0x020: [[fallthrough]]; // BGON
        case 0x028: [[fallthrough]]; // CHCTLA
        case 0x02A:                  // CHCTLB
            VDP2UpdateEnabledBGs();
            break;
        }
    }

    // -------------------------------------------------------------------------
    // Configuration

    // Enable or disable deinterlacing of double-density interlaced frames.
    void SetDeinterlaceRender(bool enable) {
        m_deinterlaceRender = enable;
    }

    bool IsDeinterlaceRender() const {
        return m_deinterlaceRender;
    }

    // Enables or disables a layer.
    // Useful for debugging and troubleshooting.
    void SetLayerEnabled(Layer layer, bool enabled);

    // Determines if a layer is forcibly disabled.
    bool IsLayerEnabled(Layer layer) const;

    // -------------------------------------------------------------------------
    // Save states

private:
    void SaveStatePrologue();

public:
    void SaveState(state::VDPState &state) const;
    bool ValidateState(const state::VDPState &state) const;
    void LoadState(const state::VDPState &state);

private:
    friend class VDP;

    VDPState &m_mainState;

    // Cached CRAM colors converted from RGB555 to RGB888.
    // Only valid when color RAM mode is one of the RGB555 modes.
    alignas(16) std::array<Color888, kVDP2CRAMSize / sizeof(uint16)> m_CRAMCache;

    template <mem_primitive T>
    FORCE_INLINE void VDP2UpdateCRAMCache(uint32 address) {
        address &= ~1;
        const Color555 color5{.u16 = util::ReadBE<uint16>(&m_mainState.CRAM[address])};
        m_CRAMCache[address / sizeof(uint16)] = ConvertRGB555to888(color5);
        if constexpr (std::is_same_v<T, uint32>) {
            const Color555 color5{.u16 = util::ReadBE<uint16>(&m_mainState.CRAM[address + 2])};
            m_CRAMCache[(address + 2) / sizeof(uint16)] = ConvertRGB555to888(color5);
        }
    }

    // -------------------------------------------------------------------------
    // Frontend callbacks

    // Invoked when the VDP1 finishes drawing a frame.
    CBVDP1FrameComplete m_cbVDP1FrameComplete;

    // Invoked when the renderer finishes drawing a frame.
    CBFrameComplete m_cbFrameComplete;

    // -------------------------------------------------------------------------
    // Rendering control

    // Note: these functions are called from the emulator thread and must use the main state regardless of threading.

    // Initializes renderer state for a new frame.
    void BeginFrame();

    // Finalizes renderer state for the current frame.
    void EndFrame();

    // Begins processing VDP1 commands.
    void BeginVDP1();

    // Processes the given scanline.
    void ProcessLine(uint32 y);

    // Processes events that occur during the first H-Blank of V-Blank.
    void ProcessVBlankHBlank();

    // Processes events that occur during the V-Blank OUT.
    void ProcessVBlankOUT();

    // Processes the even/odd field switch event.
    void ProcessEvenOddFieldSwitch();

    // -------------------------------------------------------------------------
    // Configuration

    // Enables deinterlacing of double-density interlace frames in the renderer.
    // When false, double-density interlace mode is rendered normally - only even or odd lines are updated every frame.
    // When true, double-density interlace modes is rendered in full resolution image every frame.
    // - the standard even/odd frame is rendered into m_spriteFB
    // - the complementary field is rendered into m_altSpriteFB
    // - VDP2 renders two lines per line into the output framebuffer instead of just the even or odd lines
    // - data written by the CPU on the VDP1 framebuffer is mirrored to the same position in m_altSpriteFB
    bool m_deinterlaceRender = false;

    // Complementary (alternate) VDP1 framebuffers, for deinterlaced rendering.
    // When deinterlace mode is enabled, if the system is using double-density interlace, this buffer will contain the
    // field lines complementary to the standard VDP1 framebuffer memory (e.g. while displaying odd lines, this buffer
    // contains even lines).
    // VDP2 rendering will combine both buffers to draw a full-resolution progressive image in one go.
    alignas(16) std::array<std::array<uint8, kVDP1FramebufferRAMSize>, 2> m_altSpriteFB;

    // -------------------------------------------------------------------------
    // Rendering state

    // Retrieves the current VDP state for the renderer:
    // - Threaded mode: the local VDPState copy
    // - Synchronous mode: the main VDPState reference
    FORCE_INLINE VDPState &GetRendererVDPState() {
        return m_threadedRendering ? m_localState : m_mainState;
    }

    // Retrieves the current VDP state for the renderer:
    // - Threaded mode: the local VDPState copy
    // - Synchronous mode: the main VDPState reference
    FORCE_INLINE const VDPState &GetRendererVDPState() const {
        return m_threadedRendering ? m_localState : m_mainState;
    }

    // Retrieves the current VDP state for the VDP1 renderer:
    // - Threaded mode and VDP1 included in renderer thread: the local VDPState copy
    // - Threaded mode and VDP1 excluded from renderer thread or synchronous mode: the main VDPState reference
    FORCE_INLINE VDPState &GetRendererVDP1State() {
        return m_effectiveRenderVDP1InVDP2Thread ? m_localState : m_mainState;
    }

    // Retrieves the current VDP state for the renderer:
    // - Threaded mode and VDP1 included in renderer thread: the local VDPState copy
    // - Threaded mode and VDP1 excluded from renderer thread or synchronous mode: the main VDPState reference
    FORCE_INLINE const VDPState &GetRendererVDP1State() const {
        return m_effectiveRenderVDP1InVDP2Thread ? m_localState : m_mainState;
    }

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

    template <typename It>
    FORCE_INLINE size_t DequeueEvents(It first, size_t count) {
        return m_eventQueue.wait_dequeue_bulk(m_cTok, first, count);
    }

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
            pixels.transparent.fill(false);
            pixels.specialColorCalc.fill(false);
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

    // Erases the current VDP1 display framebuffer.
    void VDP1EraseFramebuffer();

    // Swaps VDP1 framebuffers.
    void VDP1SwapFramebuffer();

    // Ends the current VDP1 frame.
    void VDP1EndFrame();

#define TPL_DEINTERLACE template <bool deinterlace>

    // Processes a single commmand from the VDP1 command table.
    TPL_DEINTERLACE void VDP1ProcessCommand();

    TPL_DEINTERLACE bool VDP1IsPixelUserClipped(CoordS32 coord) const;
    TPL_DEINTERLACE bool VDP1IsPixelSystemClipped(CoordS32 coord) const;
    TPL_DEINTERLACE bool VDP1IsLineSystemClipped(CoordS32 coord1, CoordS32 coord2) const;
    TPL_DEINTERLACE bool VDP1IsQuadSystemClipped(CoordS32 coord1, CoordS32 coord2, CoordS32 coord3,
                                                 CoordS32 coord4) const;

    TPL_DEINTERLACE void VDP1PlotPixel(CoordS32 coord, const VDP1PixelParams &pixelParams,
                                       const VDP1GouraudParams &gouraudParams);
    TPL_DEINTERLACE void VDP1PlotLine(CoordS32 coord1, CoordS32 coord2, const VDP1PixelParams &pixelParams,
                                      VDP1GouraudParams &gouraudParams);
    TPL_DEINTERLACE void VDP1PlotTexturedLine(CoordS32 coord1, CoordS32 coord2,
                                              const VDP1TexturedLineParams &lineParams,
                                              VDP1GouraudParams &gouraudParams);

    // Individual VDP1 command processors

    TPL_DEINTERLACE void VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control);
    TPL_DEINTERLACE void VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control);
    TPL_DEINTERLACE void VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control);

    TPL_DEINTERLACE void VDP1Cmd_DrawPolygon(uint32 cmdAddress);
    TPL_DEINTERLACE void VDP1Cmd_DrawPolylines(uint32 cmdAddress);
    TPL_DEINTERLACE void VDP1Cmd_DrawLine(uint32 cmdAddress);

    void VDP1Cmd_SetSystemClipping(uint32 cmdAddress);
    void VDP1Cmd_SetUserClipping(uint32 cmdAddress);
    void VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress);

#undef TPL_DEINTERLACE

    // -------------------------------------------------------------------------
    // VDP2

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
    //
    // update indicates if the line scroll table address should be updated (used with deinterlacing).
    template <bool update>
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
    // altField selects the complementary field when rendering deinterlaced double-interlace frames
    template <bool deinterlace, bool altField>
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
    //
    // deinterlace determines whether to deinterlace video output
    template <bool deinterlace>
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
    // altField selects the complementary field when rendering deinterlaced double-interlace frames
    template <uint32 colorMode, bool rotate, bool altField>
    void VDP2DrawSpriteLayer(uint32 y);

    // Draws the current VDP2 scanline of the specified normal background layer.
    //
    // y is the scanline to draw
    // colorMode is the CRAM color mode.
    //
    // bgIndex specifies the normal background index, from 0 to 3.
    // deinterlace determines whether to deinterlace video output
    // altField selects the complementary field when rendering deinterlaced double-interlace frames
    template <uint32 bgIndex, bool deinterlace, bool altField>
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
    //
    // deinterlace determines whether to deinterlace video output
    // altField selects the complementary field when rendering deinterlaced double-interlace frames
    template <bool deinterlace, bool altField>
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
    // deinterlace determines whether to deinterlace video output
    template <CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode, bool deinterlace>
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
    // deinterlace determines whether to deinterlace video output
    template <ColorFormat colorFormat, uint32 colorMode, bool deinterlace>
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
    //
    // altField selects the complementary field when rendering deinterlaced double-interlace frames
    template <bool altField>
    SpriteData VDP2FetchSpriteData(uint32 fbOffset);

    // Fetches 16-bit sprite data based on the current sprite mode.
    //
    // fbOffset is the offset into the framebuffer (in bytes) where the sprite data is located.
    // type is the sprite type (between 0 and 7).
    //
    // altField selects the complementary field when rendering deinterlaced double-interlace frames
    template <bool altField>
    SpriteData VDP2FetchWordSpriteData(uint32 fbOffset, uint8 type);

    // Fetches 8-bit sprite data based on the current sprite mode.
    //
    // fbOffset is the offset into the framebuffer (in bytes) where the sprite data is located.
    // type is the sprite type (between 8 and 15).
    //
    // altField selects the complementary field when rendering deinterlaced double-interlace frames
    template <bool altField>
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
    //
    // deinterlace determines whether to deinterlace video output
    template <bool deinterlace>
    uint32 VDP2GetY(uint32 y) const;

    // -------------------------------------------------------------------------
    // Threading

    std::thread m_renderThread;
    bool m_threadedRendering = false;
    bool m_renderVDP1OnVDP2Thread = false;

    // Managed by the render thread.
    // == m_threadedVDPRendering && m_renderVDP1OnVDP2Thread
    bool m_effectiveRenderVDP1InVDP2Thread = false;

    void UpdateEffectiveRenderingFlags();

    // Local copy of the state used by the renderer thread and updated through events.
    VDPState m_localState;

    struct QueueTraits : moodycamel::ConcurrentQueueDefaultTraits {
        static const size_t BLOCK_SIZE = 64;
        static const size_t EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD = 64;
        static const std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = 512;
        static const int MAX_SEMA_SPINS = 20000;
    };

    // The event queue itself.
    moodycamel::BlockingConcurrentQueue<RenderEvent, QueueTraits> m_eventQueue;
    moodycamel::ProducerToken m_pTok{m_eventQueue};
    moodycamel::ConsumerToken m_cTok{m_eventQueue};

    // List of events to send in batches for improved performance.
    // This array should accumulate minor events like RAM or register writes and be flushed on major events like Reset
    // or VDP2DrawLine.
    std::array<RenderEvent, 64> m_pendingEvents;
    size_t m_pendingEventsCount = 0;

    // Various synchronization signals (condvars).

    util::Event m_renderFinishedSignal{false};        // Signaled when the renderer has finished drawing the frame
    util::Event m_framebufferSwapSignal{false};       // Sync emu+render threads when swapping the VDP1 framebuffer
    util::Event m_eraseFramebufferReadySignal{false}; // Sync emu+render threads when erasing the VDP1 framebuffer
    util::Event m_preSaveSyncSignal{false};           // Sync emu+render threads before saving a savestate
    util::Event m_postLoadSyncSignal{false};          // Sync emu+render threads after loading a savestate

    // HACK to safely signal and invoke callbacks when VDP1 rendering is finished
    bool m_vdp1Done;

    void EnableThreadedVDP(bool enable);
    void IncludeVDP1RenderInVDPThread(bool enable);

    void RenderThread();

    template <mem_primitive T>
    T VDP1ReadRendererVRAM(uint32 address);

    template <mem_primitive T>
    T VDP2ReadRendererVRAM(uint32 address);

    template <mem_primitive T>
    T VDP2ReadRendererCRAM(uint32 address);

    Color888 VDP2ReadRendererColor5to8(uint32 address);

    // -------------------------------------------------------------------------
    // Memory dumps

    void DumpVDP1AltFramebuffers(std::ostream &out) const;
};

} // namespace ymir::vdp
