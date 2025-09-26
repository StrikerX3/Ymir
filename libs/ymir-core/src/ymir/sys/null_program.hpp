#pragma once

// An extremely basic IPL/CD block ROM that locks the CPU in an infinite do-nothing loop.

#include <ymir/sys/memory_defs.hpp>

#include <ymir/util/data_ops.hpp>

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::nullprog {

inline constexpr uint32 kResetPC = 0x200u; // Must not be less than 0x200
inline constexpr uint32 kStackLocation = 0x6008000u;

inline const auto kNullProgram = [] {
    std::array<uint8, 0x218> program{};
    program.fill(0);

    // Write vector table
    util::WriteBE<uint32>(&program[0x0], kResetPC | 0x20000000u); // Power-on reset PC value
    util::WriteBE<uint32>(&program[0x4], kStackLocation);         // Power-on reset SP value
    util::WriteBE<uint32>(&program[0x8], kResetPC | 0x20000000u); // Manual reset PC value
    util::WriteBE<uint32>(&program[0xC], kStackLocation);         // Manual reset SP value

    // Write the code
    uint32 pc = kResetPC;
    auto write = [&](uint16 opcode) {
        util::WriteBE<uint16>(&program[pc], opcode);
        pc += sizeof(uint16);
    };

    // Infinite loop version
    // write(0x9001); // [200]  mov.w @(pc+2), r0   (= 0x00F0)
    // write(0x8BFE); // [202]  bf <self>
    // write(0x00F0); // [204]  data.w #0x00F0

    // Sleep forever version
    write(0x9006); // [200]  mov.w @(<srval>), r0     ; get value of SR (=0x00F0)
    write(0x400E); // [202]  ldc   r0, sr             ; set SR -> disable interrupts, clear T
    write(0x9005); // [204]  mov.w @(<sbycrval>), r0  ; get address of SBYCR
    write(0xE19F); // [206]  mov #0x9F, r1            ; value of SBYCR: standby mode, halt all modules
    write(0x2010); // [208]  mov r1, @r0              ; set SBYCR
                   // loop:
    write(0x001B); // [20A]  sleep                    ; good night!
    write(0xAFFD); // [20C]  bra <loop>               ; in case you have NMIghtmares,
    write(0x0009); // [20E]  > nop                    ;   do nothing and go back to sleep
                   // srval:
    write(0x00F0); // [210]  data.w #0x00F0           ; M=0, Q=0, T=0, I3-0=0xF
    write(0xFE91); // [212]  data.w #0xFE91           ; address of SBYCR

    // Noop interrupt handler routine
    const uint32 intrHandlerAddr = pc;
    write(0x000B); // [214]  rte
    write(0x0009); // [216]  nop

    // Point every vector to the noop interrupt handler
    for (uint32 addr = 0x10; addr < 0x200; addr += 4) {
        util::WriteBE<uint32>(&program[addr], intrHandlerAddr);
    }
    return program;
}();

inline void CopyNullProgram(std::span<uint8> program) {
    assert(program.size() >= kNullProgram.size());
    std::fill(program.begin(), program.end(), 0);
    std::copy(kNullProgram.begin(), kNullProgram.end(), program.begin());
}

} // namespace ymir::nullprog
