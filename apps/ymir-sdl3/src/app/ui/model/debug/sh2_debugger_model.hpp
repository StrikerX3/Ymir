#pragma once

#include "sh2_breakpoints_manager.hpp"
#include "sh2_watchpoints_manager.hpp"

#include <ymir/core/types.hpp>

#include <imgui.h>

#include <filesystem>

namespace app::ui {

/// @brief SH-2 debugger model.
struct SH2DebuggerModel {
    bool followPC = true;           ///< Auto-follow PC in disassembly view
    bool followPCOnEvents = true;   ///< Auto-follow PC in disassembly view when hitting breakpoints or watchpoints
    bool jumpToPCRequested = false; ///< Whether to jump to PC on the next frame
    bool jumpRequested = false;     ///< Whether to jump to the target address on the next frame
    uint32 jumpAddress = 0;         ///< Pending jump target address

    SH2BreakpointsManager breakpoints{};
    SH2WatchpointsManager watchpoints{};

    struct Colors {
#define C(r, g, b) (r / 255.0f), (g / 255.0f), (b / 255.0f), 1.0f
#define CA(r, g, b, a) (r / 255.0f), (g / 255.0f), (b / 255.0f), a

        ImVec4 address{C(217, 216, 237)}; // 06005432 .... ..
        ImVec4 bytes{C(237, 236, 216)};   // ........ 4132 ..
        ImVec4 ascii = bytes;             // ........ .... A2

        struct Disassembly {
            ImVec4 delaySlot{C(96, 112, 156)};          // |_ (delay slot prefix)
            ImVec4 mnemonic{C(173, 216, 247)};          // mov rts xor jsr ...
            ImVec4 nopMnemonic{C(121, 159, 186)};       // nop
            ImVec4 loadStoreMnemonic{C(173, 216, 247)}; // mov
            ImVec4 aluMnemonic{C(151, 222, 215)};       // add sub and xor neg ...
            ImVec4 branchMnemonic{C(219, 206, 151)};    // bt bf b jsr jmp trapa rte rts ...
            ImVec4 controlMnemonic{C(185, 219, 147)};   // sett clrt ldc lds stc sts ...
            ImVec4 miscMnemonic{C(235, 157, 201)};      // sleep
            ImVec4 illegalMnemonic{C(247, 191, 173)};   // (illegal)
            ImVec4 sizeSuffix{C(128, 145, 194)};        // b w l

            ImVec4 condPass{C(143, 240, 132)}; // t f (bt/bf) eq ne pz pl ... (cmp/<cond>) (pass)
            ImVec4 condFail{C(222, 140, 135)}; // t f (bt/bf) eq ne pz pl ... (cmp/<cond>) (fail)

            ImVec4 immediate{C(221, 247, 173)};    // #0x1234
            ImVec4 regRead{C(173, 247, 206)};      // r0..r15 gbr vbr ... @( ) (read)
            ImVec4 regWrite{C(215, 173, 247)};     // r0..r15 gbr vbr ... @( ) (write)
            ImVec4 regReadWrite{C(247, 206, 173)}; // r0..r15 gbr vbr ... @( ) (read+write)

            ImVec4 separator{C(186, 191, 194)}; // , (operators) . (size)
            ImVec4 addrInc{C(147, 194, 155)};   // + (@Rn+)
            ImVec4 addrDec{C(194, 159, 147)};   // - (@-Rn)

            ImVec4 pcIconColor{C(15, 189, 219)};
            ImVec4 pcHoveredIconColor{C(65, 216, 242)};
            ImVec4 pcActiveIconColor{C(9, 155, 181)};

            ImVec4 prIconColor{C(17, 113, 237)};
            ImVec4 prHoveredIconColor{C(68, 148, 252)};
            ImVec4 prActiveIconColor{C(10, 93, 201)};

            ImVec4 bkptIconColor{C(250, 52, 17)};
            ImVec4 bkptHoveredIconColor{C(255, 97, 69)};
            ImVec4 bkptActiveIconColor{C(209, 40, 10)};

            ImVec4 wtptIconColor{C(193, 37, 250)};
            ImVec4 wtptHoveredIconColor{C(210, 87, 255)};
            ImVec4 wtptActiveIconColor{C(148, 18, 196)};

            ImVec4 lineHoverColor{C(61, 53, 2)};
            ImVec4 cursorBgColor{C(34, 61, 2)};
            ImVec4 pcBgColor{C(3, 61, 71)};
            ImVec4 prBgColor{C(6, 40, 84)};
            ImVec4 bkptBgColor{C(84, 15, 3)};
            ImVec4 wtptBgColor{C(62, 3, 84)};
            ImVec4 altLineBgColor{CA(38, 42, 46, 0.5f)};
        } disasm;

        struct Annotation {
            ImVec4 general{C(151, 154, 156)}; // -> ...
            ImVec4 condPass{C(93, 168, 89)};  // eq: Z ...
            ImVec4 condFail{C(184, 100, 95)}; // eq: Z ...
        } annotation;

        struct DataStack {
            ImVec4 local{C(210, 219, 151)};     // Local
            ImVec4 reg{C(151, 219, 195)};       // Saved <reg>
            ImVec4 trap{C(219, 167, 151)};      // Trap <vec>
            ImVec4 exception{C(170, 151, 219)}; // Exception <vec>
        } dataStack;

        struct CallStack {
            ImVec4 pc{C(65, 216, 242)};         // <current PC>
            ImVec4 call{C(219, 206, 151)};      // Call [target]
            ImVec4 trap{C(219, 167, 151)};      // Trap [vecNum] -> [target]
            ImVec4 exception{C(170, 151, 219)}; // Vector [vecNum] -> [target]
            ImVec4 vecNum{C(173, 216, 247)};    // [vecNum]
            ImVec4 arrow{C(186, 191, 194)};     // ->
            ImVec4 target{C(221, 247, 173)};    // [target]
        } callStack;

#undef C
    } colors;

    struct Style {
        // Spacing between address/instruction bytes and mnemonics
        float disasmSpacing = 10.0f;
        // Spacing between mnemonics and annotations
        float disasmAnnotationSpacing = 32.0f;
        // Spacing between different annotation elements
        float annotationInnerSpacing = 20.0f;

        // Thickness of disassembly icon contours
        float iconContourThickness = 2.0f;
        // Alpha factor to apply to colors of disabled (hovered/active) icons
        float iconDisabledAlphaFactor = 0.6f;
    } style;

    struct Settings {
        bool displayOpcodeBytes = true;
        bool displayOpcodeAscii = false;

        bool altLineColors = true;
        bool altLineAddresses = false;

        bool colorizeMnemonicsByType = true;

        bool displayCallStack = true;
        bool displayDataStack = true;
    } settings;

    void JumpTo(uint32 address) {
        jumpRequested = true;
        jumpAddress = address & ~1u;
    }

    void JumpToPC() {
        jumpToPCRequested = true;
    }
};

} // namespace app::ui
