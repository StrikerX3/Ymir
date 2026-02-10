#pragma once

/**
@file
@brief Software VDP1 and VDP2 renderer implementation.
*/

#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

#include <ymir/hw/vdp/vdp1_regs.hpp>
#include <ymir/hw/vdp/vdp2_regs.hpp>
#include <ymir/hw/vdp/vdp_callbacks.hpp>
#include <ymir/hw/vdp/vdp_defs.hpp>
#include <ymir/hw/vdp/vdp_state.hpp>

#include <ymir/hw/vdp/renderer/sw/vdp1_steppers.hpp>

#include <ymir/hw/hw_defs.hpp>

#include <ymir/util/event.hpp>
#include <ymir/util/inline.hpp>

#include <ymir/core/types.hpp>

#include <blockingconcurrentqueue.h>

#include <array>
#include <atomic>
#include <span>
#include <thread>
#include <type_traits>

namespace ymir::vdp {

/// @brief Invoked when the software VDP2 renderer finishes rendering a frame.
/// Framebuffer data is in little-endian XRGB8888 format.
///
/// @param[in] fb a pointer to the framebuffer data
/// @param[in] width the width of the framebuffer (in pixels)
/// @param[in] height the height of the framebuffer (in pixels)
using CBSoftwareFrameComplete = util::OptionalCallback<void(uint32 *fb, uint32 width, uint32 height)>;

/// @brief Callbacks specific to the software VDP renderer.
struct SoftwareRendererCallbacks {
    /// @brief Software frame complete callback, invoked when a framebuffer is ready to be copied to the frontend.
    CBSoftwareFrameComplete FrameComplete;
};

class SoftwareVDPRenderer : public IVDPRenderer {
public:
    SoftwareVDPRenderer(VDPState &state, config::VDP2DebugRender &vdp2DebugRenderOptions);
    ~SoftwareVDPRenderer();

    // -------------------------------------------------------------------------
    // Basics

    bool IsValid() const override {
        return true;
    }

    bool IsHardwareRenderer() const override {
        return false;
    }

protected:
    void ResetImpl(bool hard) override;

public:
    // -------------------------------------------------------------------------
    // Configuration

    void ConfigureEnhancements(const config::Enhancements &enhancements) override;

    /// @brief Software renderer callbacks.
    SoftwareRendererCallbacks SwCallbacks;

    /// @brief Enables or disables a dedicated thread to render VDP1 graphics.
    /// @param[in] enable `true` to render VDP1 in a dedicated thread, `false` to render on the caller thread.
    void EnableThreadedVDP1(bool enable);

    /// @brief Enables or disables a dedicated thread to render VDP2 graphics.
    /// @param[in] enable `true` to render VDP2 in a dedicated thread, `false` to render on the caller thread.
    void EnableThreadedVDP2(bool enable);

    /// @brief Enables or disables a dedicated thread to render deinterlaced graphics.
    /// @param[in] enable `true` to use a dedicated thread for the deinterlacer, `false` to render on the VDP2 thread.
    void EnableThreadedDeinterlacer(bool enable) {
        m_threadedDeinterlacer = enable;
    }

    // -------------------------------------------------------------------------
    // Save states

    void PreSaveStateSync() override;
    void PostLoadStateSync() override;

    void SaveState(state::VDPState::VDPRendererState &state) override;
    bool ValidateState(const state::VDPState::VDPRendererState &state) const override;
    void LoadState(const state::VDPState::VDPRendererState &state) override;

    // -------------------------------------------------------------------------
    // VDP1 memory and register writes

    void VDP1WriteVRAM(uint32 address, uint8 value) override;
    void VDP1WriteVRAM(uint32 address, uint16 value) override;

    template <mem_primitive_16 T>
    void VDP1WriteVRAMImpl(uint32 address, T value);

    void VDP1WriteFB(uint32 address, uint8 value) override;
    void VDP1WriteFB(uint32 address, uint16 value) override;

    template <mem_primitive_16 T>
    void VDP1WriteFBImpl(uint32 address, T value);

    void VDP1WriteReg(uint32 address, uint16 value) override;

    // -------------------------------------------------------------------------
    // VDP2 memory and register writes

    void VDP2WriteVRAM(uint32 address, uint8 value) override;
    void VDP2WriteVRAM(uint32 address, uint16 value) override;

    template <mem_primitive_16 T>
    void VDP2WriteVRAMImpl(uint32 address, T value);

    void VDP2WriteCRAM(uint32 address, uint8 value) override;
    void VDP2WriteCRAM(uint32 address, uint16 value) override;

    template <mem_primitive_16 T>
    void VDP2WriteCRAMImpl(uint32 address, T value);

    void VDP2WriteReg(uint32 address, uint16 value) override;

    // -------------------------------------------------------------------------
    // Rendering process

    void VDP1EraseFramebuffer(uint64 cycles) override;
    void VDP1SwapFramebuffer() override;
    void VDP1BeginFrame() override;
    void VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) override;
    void VDP1EndFrame() override;

    void VDP2SetResolution(uint32 h, uint32 v, bool exclusive) override;
    void VDP2SetField(bool odd) override;
    void VDP2LatchTVMD() override;
    void VDP2BeginFrame() override;
    void VDP2RenderLine(uint32 y) override;
    void VDP2EndFrame() override;

    // -------------------------------------------------------------------------
    // Debugger

    void UpdateEnabledLayers() override;

    // -------------------------------------------------------------------------
    // Utilities

    void DumpExtraVDP1Framebuffers(std::ostream &out) const override;

private:
    VDPState &m_state;
    config::VDP2DebugRender &m_vdp2DebugRenderOptions;

    uint32 m_HRes;
    uint32 m_VRes;
    bool m_exclusiveMonitor;
    bool m_resolutionChanged = false;

    // Complementary (alternate) VDP1 framebuffers, for deinterlaced rendering.
    // When deinterlace mode is enabled, if the system is using double-density interlace, this buffer will contain the
    // field lines complementary to the standard VDP1 framebuffer memory (e.g. while displaying odd lines, this buffer
    // contains even lines).
    // VDP2 rendering will combine both buffers to draw a full-resolution progressive image in one go.
    alignas(16) std::array<SpriteFB, 2> m_altSpriteFB;

    // Transparent mesh sprite framebuffer.
    // Used when transparent meshes are enabled.
    // Indexing: [altFB][drawFB]
    alignas(16) std::array<std::array<SpriteFB, 2>, 2> m_meshFB;

    // -------------------------------------------------------------------------
    // Threading

    struct ConcQueueTraits : moodycamel::ConcurrentQueueDefaultTraits {
        static constexpr size_t BLOCK_SIZE = 64;
        static constexpr size_t EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD = 64;
        static constexpr std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = 512;
        static constexpr int MAX_SEMA_SPINS = 20000;
    };

    struct VDP1RenderEvent {
        enum class Type {
            Reset,

            SwapBuffers,
            Command,

            VRAMWriteByte,
            VRAMWriteWord,
            RegWrite,

            PreSaveStateSync,
            PostLoadStateSync,

            Shutdown,
        };

        Type type;
        union {
            struct {
                uint32 address;
                VDP1Command::Control control;
            } command;

            struct {
                uint32 address;
                uint32 value;
            } write;
        };

        static VDP1RenderEvent Reset() {
            return {Type::Reset};
        }

        static VDP1RenderEvent SwapBuffers() {
            return {Type::SwapBuffers};
        }

        static VDP1RenderEvent Command(uint32 address, VDP1Command::Control control) {
            return {Type::Command, {.command = {.address = address, .control = control}}};
        }

        static VDP1RenderEvent VRAMWriteByte(uint32 address, uint8 value) {
            return {Type::VRAMWriteByte, {.write = {.address = address, .value = value}}};
        }

        static VDP1RenderEvent VRAMWriteWord(uint32 address, uint16 value) {
            return {Type::VRAMWriteWord, {.write = {.address = address, .value = value}}};
        }

        template <mem_primitive_16 T>
        static VDP1RenderEvent VRAMWrite(uint32 address, T value) {
            if constexpr (std::is_same_v<T, uint8>) {
                return VRAMWriteByte(address, value);
            } else if constexpr (std::is_same_v<T, uint16>) {
                return VRAMWriteWord(address, value);
            }
            util::unreachable();
        }

        static VDP1RenderEvent RegWrite(uint32 address, uint16 value) {
            return {Type::RegWrite, {.write = {.address = address, .value = value}}};
        }

        static VDP1RenderEvent PreSaveStateSync() {
            return {Type::PreSaveStateSync};
        }

        static VDP1RenderEvent PostLoadStateSync() {
            return {Type::PostLoadStateSync};
        }

        static VDP1RenderEvent Shutdown() {
            return {Type::Shutdown};
        }
    };

    mutable struct VDP1RenderContext {
        moodycamel::BlockingConcurrentQueue<VDP1RenderEvent, ConcQueueTraits> eventQueue;
        moodycamel::ProducerToken pTok{eventQueue};
        moodycamel::ConsumerToken cTok{eventQueue};

        util::Event swapBuffersSignal{false};
        util::Event preSaveSyncSignal{false};
        util::Event postLoadSyncSignal{false};

        std::array<VDP1RenderEvent, 64> pendingEvents;
        size_t pendingEventsCount = 0;

        struct VDP1 {
            VDP1Regs regs;
            alignas(16) std::array<uint8, kVDP1VRAMSize> VRAM;
        } vdp1;

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
        }

        void EnqueueEvent(VDP1RenderEvent &&event) {
            switch (event.type) {
            case VDP1RenderEvent::Type::VRAMWriteByte:
            case VDP1RenderEvent::Type::VRAMWriteWord:
            case VDP1RenderEvent::Type::RegWrite:
                // Batch VRAM and register writes to send in bulk
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
    } m_vdp1RenderingContext;

    struct VDP2RenderEvent {
        enum class Type {
            Reset,
            OddField,
            VDP2LatchTVMD,
            VDP1EraseFramebuffer,
            VDP1SwapFramebuffer,

            VDP2BeginFrame,
            VDP2UpdateEnabledBGs,
            VDP2DrawLine,
            VDP2EndFrame,

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

        static VDP2RenderEvent Reset() {
            return {Type::Reset};
        }

        static VDP2RenderEvent OddField(bool odd) {
            return {Type::OddField, {.oddField = {.odd = odd}}};
        }

        static VDP2RenderEvent VDP2LatchTVMD() {
            return {Type::VDP2LatchTVMD};
        }

        static VDP2RenderEvent VDP1EraseFramebuffer() {
            return {Type::VDP1EraseFramebuffer};
        }

        static VDP2RenderEvent VDP1SwapFramebuffer() {
            return {Type::VDP1SwapFramebuffer};
        }

        static VDP2RenderEvent VDP2BeginFrame() {
            return {Type::VDP2BeginFrame};
        }

        static VDP2RenderEvent VDP2UpdateEnabledBGs() {
            return {Type::VDP2UpdateEnabledBGs};
        }

        static VDP2RenderEvent VDP2DrawLine(uint32 vcnt) {
            return {Type::VDP2DrawLine, {.drawLine = {.vcnt = vcnt}}};
        }

        static VDP2RenderEvent VDP2EndFrame() {
            return {Type::VDP2EndFrame};
        }

        template <mem_primitive_16 T>
        static VDP2RenderEvent VDP2VRAMWrite(uint32 address, T value) {
            if constexpr (std::is_same_v<T, uint8>) {
                return VDP2VRAMWriteByte(address, value);
            } else if constexpr (std::is_same_v<T, uint16>) {
                return VDP2VRAMWriteWord(address, value);
            }
            util::unreachable();
        }

        static VDP2RenderEvent VDP2VRAMWriteByte(uint32 address, uint8 value) {
            return {Type::VDP2VRAMWriteByte, {.write = {.address = address, .value = value}}};
        }

        static VDP2RenderEvent VDP2VRAMWriteWord(uint32 address, uint16 value) {
            return {Type::VDP2VRAMWriteWord, {.write = {.address = address, .value = value}}};
        }

        template <mem_primitive_16 T>
        static VDP2RenderEvent VDP2CRAMWrite(uint32 address, T value) {
            if constexpr (std::is_same_v<T, uint8>) {
                return VDP2CRAMWriteByte(address, value);
            } else if constexpr (std::is_same_v<T, uint16>) {
                return VDP2CRAMWriteWord(address, value);
            }
            util::unreachable();
        }

        static VDP2RenderEvent VDP2CRAMWriteByte(uint32 address, uint8 value) {
            return {Type::VDP2CRAMWriteByte, {.write = {.address = address, .value = value}}};
        }

        static VDP2RenderEvent VDP2CRAMWriteWord(uint32 address, uint16 value) {
            return {Type::VDP2CRAMWriteWord, {.write = {.address = address, .value = value}}};
        }

        static VDP2RenderEvent VDP2RegWrite(uint32 address, uint16 value) {
            return {Type::VDP2RegWrite, {.write = {.address = address, .value = value}}};
        }

        static VDP2RenderEvent PreSaveStateSync() {
            return {Type::PreSaveStateSync};
        }

        static VDP2RenderEvent PostLoadStateSync() {
            return {Type::PostLoadStateSync};
        }

        static VDP2RenderEvent Shutdown() {
            return {Type::Shutdown};
        }
    };

    mutable struct VDP2RenderContext {
        moodycamel::BlockingConcurrentQueue<VDP2RenderEvent, ConcQueueTraits> eventQueue;
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

        std::array<VDP2RenderEvent, 64> pendingEvents;
        size_t pendingEventsCount = 0;

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
            vdp2.regs.Reset();
            vdp2.VRAM.fill(0);
            vdp2.CRAM.fill(0);
            vdp2.CRAMCache.fill({.u32 = 0});
            displayFB = 0;
        }

        void EnqueueEvent(VDP2RenderEvent &&event) {
            switch (event.type) {
            case VDP2RenderEvent::Type::VDP2VRAMWriteByte:
            case VDP2RenderEvent::Type::VDP2VRAMWriteWord:
            case VDP2RenderEvent::Type::VDP2CRAMWriteByte:
            case VDP2RenderEvent::Type::VDP2CRAMWriteWord:
            case VDP2RenderEvent::Type::VDP2RegWrite:
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
    } m_vdp2RenderingContext;

    std::thread m_VDP1RenderThread;
    bool m_threadedVDP1Rendering = false;

    std::thread m_VDP2RenderThread;
    std::thread m_VDP2DeinterlaceRenderThread;
    bool m_threadedVDP2Rendering = false;

    void VDP1RenderThread();
    void VDP2RenderThread();
    void VDP2DeinterlaceRenderThread();

    std::array<uint8, kVDP2VRAMSize> &VDP2GetRendererVRAM();

    template <mem_primitive T>
    T VDP1ReadRendererVRAM(uint32 address);

    template <mem_primitive T>
    T VDP2ReadRendererVRAM(uint32 address);

    template <mem_primitive T>
    T VDP2ReadRendererCRAM(uint32 address);

    Color888 VDP2ReadRendererColor5to8(uint32 address) const;

    // -------------------------------------------------------------------------
    // Configuration

    // Local copy of the current VDP enhancements configuration.
    config::Enhancements m_enhancements;

    // Runs the deinterlacer in a dedicated thread.
    bool m_threadedDeinterlacer = false;

    using FnVDP1ProcessCommand = void (SoftwareVDPRenderer::*)();
    using FnVDP1HandleCommand = void (SoftwareVDPRenderer::*)(uint32 cmdAddress, VDP1Command::Control control);
    using FnVDP2DrawLine = void (SoftwareVDPRenderer::*)(uint32 y, bool altField);

    FnVDP1HandleCommand m_fnVDP1HandleCommand;
    FnVDP2DrawLine m_fnVDP2DrawLine;

    /// @brief Updates function pointers based on the current rendering settings.
    void UpdateFunctionPointers();

    /// @brief Helper template to convert runtime parameters into compile-time constants for building function pointers.
    template <bool... t_features>
    void UpdateFunctionPointersTemplate(bool feature, auto... features);

    /// @brief Terminal case for helper template.
    template <bool... t_features>
    void UpdateFunctionPointersTemplate();

    // -------------------------------------------------------------------------
    // VDP1

    struct VDP1State {
        VDP1State() {
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
        }

        // System clipping dimensions
        uint16 sysClipH, sysClipV;
        uint16 doubleV;

        // User clipping area
        uint16 userClipX0, userClipY0; // Top-left
        uint16 userClipX1, userClipY1; // Bottom-right

        // Local coordinates offset
        sint32 localCoordX;
        sint32 localCoordY;
    } m_VDP1State;

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

    // Retrieves the current set of VDP1 registers.
    VDP1Regs &VDP1GetRegs();

    // Retrieves the current set of VDP1 registers.
    const VDP1Regs &VDP1GetRegs() const;

    // Retrieves the current index of the VDP1 display framebuffer.
    uint8 VDP1GetDisplayFBIndex() const;

    // Erases the current VDP1 display framebuffer.
    template <bool countCycles>
    void VDP1DoEraseFramebuffer(uint64 cycles = ~0ull);

#define TPL_TRAITS template <bool deinterlace, bool transparentMeshes>
#define TPL_LINE_TRAITS template <bool antiAlias, bool deinterlace, bool transparentMeshes>
#define TPL_DEINTERLACE template <bool deinterlace>

    // Processes a single commmand from the VDP1 command table.
    TPL_DEINTERLACE bool VDP1IsPixelClipped(CoordS32 coord, bool userClippingEnable, bool clippingMode) const;

    TPL_DEINTERLACE bool VDP1IsPixelUserClipped(CoordS32 coord) const;
    TPL_DEINTERLACE bool VDP1IsPixelSystemClipped(CoordS32 coord) const;
    TPL_DEINTERLACE bool VDP1IsLineSystemClipped(CoordS32 coord1, CoordS32 coord2) const;
    TPL_DEINTERLACE bool VDP1IsQuadSystemClipped(CoordS32 coord1, CoordS32 coord2, CoordS32 coord3,
                                                 CoordS32 coord4) const;

    // Plotting functions.
    // Should return true if at least one pixel of the line is inside the system + user clipping areas, regardless of
    // transparency, mesh, end codes, etc.

    TPL_TRAITS bool VDP1PlotPixel(CoordS32 coord, const VDP1PixelParams &pixelParams);
    TPL_LINE_TRAITS bool VDP1PlotLine(CoordS32 coord1, CoordS32 coord2, VDP1LineParams &lineParams);
    TPL_TRAITS bool VDP1PlotTexturedLine(CoordS32 coord1, CoordS32 coord2, VDP1TexturedLineParams &lineParams);
    TPL_TRAITS void VDP1PlotTexturedQuad(uint32 cmdAddress, VDP1Command::Control control, VDP1Command::Size size,
                                         CoordS32 coordA, CoordS32 coordB, CoordS32 coordC, CoordS32 coordD);

    // Individual VDP1 command processors

    uint64 VDP1CalcCommandTiming(uint32 cmdAddress, VDP1Command::Control control);
    TPL_TRAITS void VDP1Cmd_Handle(uint32 cmdAddress, VDP1Command::Control control);

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
#undef TPL_LINE_TRAITS
#undef TPL_DEINTERLACE

    // -------------------------------------------------------------------------
    // VDP2 rendering

    // Character modes, a combination of Character Size from the Character Control Register (CHCTLA-B) and Character
    // Number Supplement from the Pattern Name Control Register (PNCN0-3/PNCR)
    enum class CharacterMode {
        TwoWord,         // 2 word characters
        OneWordStandard, // 1 word characters with standard character data, H/V flip available
        OneWordExtended, // 1 word characters with extended character data; H/V flip unavailable
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

    // Scaneline state for Rotation Parameters A and B.
    struct RotationParamLineState {
        RotationParamLineState() {
            Reset();
        }

        void Reset() {
            for (auto &addrs : pageBaseAddresses) {
                addrs.fill(0);
            }
            screenCoords.fill({});
            lineColor.fill({.u32 = 0});
            transparent.fill(false);
        }

        // Page base addresses for RBG planes A-P using Rotation Parameters A and B.
        // Indexing: [RBG0-1][Plane A-P]
        // Derived from mapIndices, CHCTLA/CHCTLB.xxCHSZ, PNCR.xxPNB and PLSZ.xxPLSZn
        std::array<std::array<uint32, 16>, 2> pageBaseAddresses;

        // Precomputed screen coordinates (with 16 fractional bits).
        alignas(16) std::array<CoordS32, kMaxResH / 2> screenCoords;

        // Precomputed sprite coordinates (without fractional bits).
        alignas(16) std::array<CoordS32, kMaxResH / 2> spriteCoords;

        // Precomputed coefficient table line color.
        // Filled in only if the coefficient table is enabled and using line color data.
        alignas(16) std::array<Color888, kMaxResH / 2> lineColor;

        // Prefetched coefficient table transparency bits.
        // Filled in only if the coefficient table is enabled.
        alignas(16) std::array<bool, kMaxResH / 2> transparent;
    };

    enum RotParamSelector { RotParamA, RotParamB };

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

    // Cached CRAM colors converted from RGB555 to RGB888.
    // Only valid when color RAM mode is one of the RGB555 modes.
    alignas(16) std::array<Color888, kVDP2CRAMSize / sizeof(uint16)> m_CRAMCache;

    template <mem_primitive T>
    FORCE_INLINE uint32 MapRendererCRAMAddress(uint32 address) const {
        m_state.MapVDP2CRAMAddress<T>(address);
        return kVDP2CRAMAddressMapping[m_vdp2RenderingContext.vdp2.regs.vramControl.colorRAMMode >> 1][address & 0xFFF];
    }

    template <mem_primitive T>
    void VDP2UpdateCRAMCache(uint32 address);

    // Common layer states.
    // Entry [0] is primary and [1] is alternate field for deinterlacing.
    //     RBG0+RBG1   RBG0        RBG1        no RBGs
    // [0] Sprite      Sprite      Sprite      Sprite
    // [1] RBG0        RBG0        -           -
    // [2] RBG1        NBG0        RBG1        NBG0
    // [3] EXBG        NBG1/EXBG   NBG1/EXBG   NBG1/EXBG
    // [4] -           NBG2        NBG2        NBG2
    // [5] -           NBG3        NBG3        NBG3
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

    // Scanline states for Rotation Parameters A and B.
    std::array<RotationParamLineState, 2> m_rotParamLineStates;

    // Line colors per RBG per pixel.
    std::array<std::array<Color888, kMaxResH / 2>, 2> m_rbgLineColors;

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

    // Current display framebuffer.
    std::array<uint32, kMaxResH * kMaxResV> m_framebuffer;

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
    // y is the scanline to draw.
    // bgParams contains the parameters for the BG to draw.
    // layerState is a reference to the common layer state for the background.
    // windowState is a reference to the window state for the layer.
    // altField selects the complementary field when rendering deinterlaced frames
    // vramFetcher is the corresponding background layer's VRAM fetcher.
    //
    // bgIndex specifies the rotation background index, from 0 to 1.
    // charMode indicates if character patterns use two words or one word with standard or extended character data.
    // fourCellChar indicates if character patterns are 1x1 cells (false) or 2x2 cells (true).
    // colorFormat is the color format for cell data.
    // colorMode is the CRAM color mode.
    template <uint32 bgIndex, CharacterMode charMode, bool fourCellChar, ColorFormat colorFormat, uint32 colorMode>
    void VDP2DrawRotationScrollBG(uint32 y, const BGParams &bgParams, LayerState &layerState, VRAMFetcher &vramFetcher,
                                  std::span<const bool> windowState, bool altField);

    // Draws a rotation bitmap BG scanline.
    //
    // y is the scanline to draw.
    // bgParams contains the parameters for the BG to draw.
    // layerState is a reference to the common layer state for the background.
    // windowState is a reference to the window state for the layer.
    // altField selects the complementary field when rendering deinterlaced frames
    //
    // bgIndex specifies the rotation background index, from 0 to 1.
    // colorFormat is the color format for bitmap data.
    // colorMode is the CRAM color mode.
    template <uint32 bgIndex, ColorFormat colorFormat, uint32 colorMode>
    void VDP2DrawRotationBitmapBG(uint32 y, const BGParams &bgParams, LayerState &layerState,
                                  std::span<const bool> windowState, bool altField);

    // Stores the line color for the specified pixel of the RBG.
    //
    // x is the horizontal coordinate of the pixel.
    // bgParams contains the parameters for the BG to draw.
    // rotParamSelector is the rotation parameter in use.
    //
    // bgIndex specifies the rotation background index, from 0 to 1.
    template <uint32 bgIndex>
    void VDP2StoreRotationLineColorData(uint32 x, const BGParams &bgParams, RotParamSelector rotParamSelector);

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
    //
    // applyMesh determines if the pixel to be fetched is a transparent mesh pixel (true) or a regular sprite layer
    // pixel (false).
    template <bool applyMesh>
    SpriteData VDP2FetchSpriteData(const SpriteFB &fb, uint32 fbOffset);

    // Retrieves the Y display coordinate based on the current interlace mode.
    //
    // y is the Y coordinate to translate
    //
    // deinterlace determines whether to deinterlace video output
    template <bool deinterlace>
    uint32 VDP2GetY(uint32 y) const;
};

} // namespace ymir::vdp
