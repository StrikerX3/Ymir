#include <ymir/hw/sh2/sh2.hpp>

#include <ymir/util/process.hpp>

#include <ymir/core/types.hpp>

#include <fmt/format.h>

#include <chrono>

void runSH2PerfSandbox() {
    util::BoostCurrentProcessPriority(true);
    util::BoostCurrentThreadPriority(true);

    // static constexpr uint16 kInstr = 0b0000'0000'0000'1001; // nop
    // static constexpr uint16 kInstr = 0b0000'0000'0001'1011; // sleep
    // static constexpr uint16 kInstr = 0b0110'0000'0000'0011; // mov      Rm, Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'0000; // mov.b    @Rm, Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'0001; // mov.w    @Rm, Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'0010; // mov.l    @Rm, Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0000'1100; // mov.b    @(R0,Rm), Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0000'1101; // mov.w    @(R0,Rm), Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0000'1110; // mov.l    @(R0,Rm), Rn
    // static constexpr uint16 kInstr = 0b1000'0100'0000'0000; // mov.b    @(disp,Rm), R0
    // static constexpr uint16 kInstr = 0b1000'0101'0000'0000; // mov.w    @(disp,Rm), R0
    // static constexpr uint16 kInstr = 0b0101'0000'0000'0000; // mov.l    @(disp,Rm), Rn
    // static constexpr uint16 kInstr = 0b1100'0100'0000'0000; // mov.b    @(disp,GBR), R0
    // static constexpr uint16 kInstr = 0b1100'0101'0000'0000; // mov.w    @(disp,GBR), R0
    // static constexpr uint16 kInstr = 0b1100'0110'0000'0000; // mov.l    @(disp,GBR), R0
    // static constexpr uint16 kInstr = 0b0010'0000'0000'0100; // mov.b    Rm, @-Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'0101; // mov.w    Rm, @-Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'0110; // mov.l    Rm, @-Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'0100; // mov.b    @Rm+, Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'0101; // mov.w    @Rm+, Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'0110; // mov.l    @Rm+, Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'0000; // mov.b    Rm, @Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'0001; // mov.w    Rm, @Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'0010; // mov.l    Rm, @Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0000'0100; // mov.b    Rm, @(R0,Rn)
    // static constexpr uint16 kInstr = 0b0000'0000'0000'0101; // mov.w    Rm, @(R0,Rn)
    // static constexpr uint16 kInstr = 0b0000'0000'0000'0110; // mov.l    Rm, @(R0,Rn)
    // static constexpr uint16 kInstr = 0b1000'0000'0000'0000; // mov.b    R0, @(disp,Rn)
    // static constexpr uint16 kInstr = 0b1000'0001'0000'0000; // mov.w    R0, @(disp,Rn)
    // static constexpr uint16 kInstr = 0b0001'0000'0000'0000; // mov.l    Rm, @(disp,Rn)
    // static constexpr uint16 kInstr = 0b1100'0000'0000'0000; // mov.b    R0, @(disp,GBR)
    // static constexpr uint16 kInstr = 0b1100'0001'0000'0000; // mov.w    R0, @(disp,GBR)
    // static constexpr uint16 kInstr = 0b1100'0010'0000'0000; // mov.l    R0, @(disp,GBR)
    // static constexpr uint16 kInstr = 0b1110'0000'0000'0000; // mov      #imm, Rn
    // static constexpr uint16 kInstr = 0b1001'0000'0000'0000; // mov.w    @(disp,PC), Rn
    // static constexpr uint16 kInstr = 0b1101'0000'0000'0000; // mov.l    @(disp,PC), Rn
    // static constexpr uint16 kInstr = 0b1100'0111'0000'0000; // mova     @(disp,PC), R0
    // static constexpr uint16 kInstr = 0b0000'0000'0010'1001; // movt     Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0000'1000; // clrt
    // static constexpr uint16 kInstr = 0b0000'0000'0001'1000; // sett
    // static constexpr uint16 kInstr = 0b0110'0000'0000'1100; // extu.b   Rm,   Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'1101; // extu.w   Rm,   Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'1110; // exts.b   Rm,   Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'1111; // exts.w   Rm,   Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'1000; // swap.b   Rm,   Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'1001; // swap.w   Rm,   Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'1101; // xtrct    Rm,   Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0001'1110; // ldc      Rm,   GBR
    // static constexpr uint16 kInstr = 0b0100'0000'0000'1110; // ldc      Rm,   SR
    // static constexpr uint16 kInstr = 0b0100'0000'0010'1110; // ldc      Rm,   VBR
    // static constexpr uint16 kInstr = 0b0100'0000'0000'1010; // lds      Rm,   MACH
    // static constexpr uint16 kInstr = 0b0100'0000'0001'1010; // lds      Rm,   MACL
    // static constexpr uint16 kInstr = 0b0100'0000'0010'1010; // lds      Rm,   PR
    // static constexpr uint16 kInstr = 0b0000'0000'0001'0010; // stc      GBR,  Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0000'0010; // stc      SR,   Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0010'0010; // stc      VBR,  Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0000'1010; // sts      MACH, Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0001'1010; // sts      MACL, Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0010'1010; // sts      PR,   Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0001'0111; // ldc.l    @Rm+, GBR
    // static constexpr uint16 kInstr = 0b0100'0000'0000'0111; // ldc.l    @Rm+, SR
    // static constexpr uint16 kInstr = 0b0100'0000'0010'0111; // ldc.l    @Rm+, VBR
    // static constexpr uint16 kInstr = 0b0100'0000'0000'0110; // lds.l    @Rm+, MACH
    // static constexpr uint16 kInstr = 0b0100'0000'0001'0110; // lds.l    @Rm+, MACL
    // static constexpr uint16 kInstr = 0b0100'0000'0010'0110; // lds.l    @Rm+, PR
    // static constexpr uint16 kInstr = 0b0100'0000'0001'0011; // stc.l    GBR,  @-Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0000'0011; // stc.l    SR,   @-Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0010'0011; // stc.l    VBR,  @-Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0000'0010; // sts.l    MACH, @-Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0001'0010; // sts.l    MACL, @-Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0010'0010; // sts.l    PR,   @-Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'1100; // add      Rm, Rn
    // static constexpr uint16 kInstr = 0b0111'0000'0000'0000; // add      #imm, Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'1110; // addc     Rm, Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'1111; // addv     Rm, Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'1001; // and      Rm, Rn
    // static constexpr uint16 kInstr = 0b1100'1001'0000'0000; // and      #imm, R0
    // static constexpr uint16 kInstr = 0b1100'1101'0000'0000; // and.b    #imm, @(R0,GBR)
    // static constexpr uint16 kInstr = 0b0110'0000'0000'1011; // neg      Rm, Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'1010; // negc     Rm, Rn
    // static constexpr uint16 kInstr = 0b0110'0000'0000'0111; // not      Rm, Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'1011; // or       Rm, Rn
    // static constexpr uint16 kInstr = 0b1100'1011'0000'0000; // or       #imm, R0
    // static constexpr uint16 kInstr = 0b1100'1111'0000'0000; // or.b     #imm, @(R0,GBR)
    // static constexpr uint16 kInstr = 0b0100'0000'0010'0100; // rotcl    Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0010'0101; // rotcr    Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0000'0100; // rotl     Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0000'0101; // rotr     Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0010'0000; // shal     Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0010'0001; // shar     Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0000'0000; // shll     Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0000'1000; // shll2    Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0001'1000; // shll8    Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0010'1000; // shll16   Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0000'0001; // shlr     Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0000'1001; // shlr2    Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0001'1001; // shlr8    Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0010'1001; // shlr16   Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'1000; // sub      Rm, Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'1010; // subc     Rm, Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'1011; // subv     Rm, Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'1010; // xor      Rm, Rn
    // static constexpr uint16 kInstr = 0b1100'1010'0000'0000; // xor      #imm, R0
    // static constexpr uint16 kInstr = 0b1100'1110'0000'0000; // xor.b    #imm, @(R0,GBR)
    // static constexpr uint16 kInstr = 0b0100'0000'0001'0000; // dt       Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0010'1000; // clrmac
    // static constexpr uint16 kInstr = 0b0100'0000'0000'1111; // mac.w    @Rm+, @Rn+
    // static constexpr uint16 kInstr = 0b0000'0000'0000'1111; // mac.l    @Rm+, @Rn+
    // static constexpr uint16 kInstr = 0b0000'0000'0000'0111; // mul.l    Rm, Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'1111; // muls.w   Rm, Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'1110; // mulu.w   Rm, Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'1101; // dmuls.l  Rm, Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'0101; // dmulu.l  Rm, Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'0111; // div0s    Rm, Rn
    // static constexpr uint16 kInstr = 0b0000'0000'0001'1001; // div0u
    static constexpr uint16 kInstr = 0b0011'0000'0000'0100; // div1     Rm, Rn
    // static constexpr uint16 kInstr = 0b1000'1000'0000'0000; // cmp/eq   #imm, R0
    // static constexpr uint16 kInstr = 0b0011'0000'0000'0000; // cmp/eq   Rm, Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'0011; // cmp/ge   Rm, Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'0111; // cmp/gt   Rm, Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'0110; // cmp/hi   Rm, Rn
    // static constexpr uint16 kInstr = 0b0011'0000'0000'0010; // cmp/hs   Rm, Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0001'0101; // cmp/pl   Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0001'0001; // cmp/pz   Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'1100; // cmp/str  Rm, Rn
    // static constexpr uint16 kInstr = 0b0100'0000'0001'1011; // tas.b    @Rn
    // static constexpr uint16 kInstr = 0b0010'0000'0000'1000; // tst      Rm, Rn
    // static constexpr uint16 kInstr = 0b1100'1000'0000'0000; // tst      #imm, R0
    // static constexpr uint16 kInstr = 0b1100'1100'0000'0000; // tst.b    #imm, @(R0,GBR)
    // static constexpr uint16 kInstr = 0b1000'1011'0000'0000; // bf       <label>
    // static constexpr uint16 kInstr = 0b1000'1111'0000'0000; // bf/s     <label>
    // static constexpr uint16 kInstr = 0b1000'1001'0000'0000; // bt       <label>
    // static constexpr uint16 kInstr = 0b1000'1101'0000'0000; // bt/s     <label>
    // static constexpr uint16 kInstr = 0b1010'0000'0000'0000; // bra      <label>
    // static constexpr uint16 kInstr = 0b0000'0000'0010'0011; // braf     Rm
    // static constexpr uint16 kInstr = 0b1011'0000'0000'0000; // bsr      <label>
    // static constexpr uint16 kInstr = 0b0000'0000'0000'0011; // bsrf     Rm
    // static constexpr uint16 kInstr = 0b0100'0000'0010'1011; // jmp      @Rm
    // static constexpr uint16 kInstr = 0b0100'0000'0000'1011; // jsr      @Rm
    // static constexpr uint16 kInstr = 0b1100'0011'0000'0000; // trapa    #imm
    // static constexpr uint16 kInstr = 0b0000'0000'0010'1011; // rte
    // static constexpr uint16 kInstr = 0b0000'0000'0000'1011; // rts

    ymir::sys::SH2Bus bus{};
    ymir::sh2::SH2 cpu{bus, true};
    bus.MapBoth(
        0x000'0000, 0x7FF'FFFF, nullptr,                                                //
        [](uint32 address, void *) -> uint8 { return kInstr >> ((~address & 1) * 8); }, //
        [](uint32 address, void *) -> uint16 { return kInstr; },                        //
        [](uint32 address, void *) -> uint32 { return (kInstr << 16) | kInstr; });

    using namespace std::chrono_literals;

    /*static constexpr auto kDuration = 10s;
    const auto t0 = std::chrono::steady_clock::now();
    const auto tTarget = t0 + kDuration;
    uint64 iters = 0;
    uint64 totalIters = 0;
    auto t = t0;
    while (t < tTarget) {
        cpu.Step<false, false>();
        iters++;
        const auto t1 = std::chrono::steady_clock::now();
        if (t1 >= t + 1s) {
            t += 1s;
            fmt::println("{} iters", iters);
            totalIters += iters;
            iters = 0;
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto dt = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
    totalIters += iters;
    fmt::println("{} iters total", totalIters);
    fmt::println("{} iters/sec", totalIters / std::chrono::duration_cast<std::chrono::seconds>(dt).count());*/

    static constexpr uint64 kIters = 20;
    const auto t0 = std::chrono::steady_clock::now();
    for (uint64 j = 0; j < kIters; j++) {
        cpu.Reset(true);
        const auto t0 = std::chrono::steady_clock::now();
        for (uint64 i = 0; i < 250'000'000; i++) {
            cpu.Step<false, false>();
        }
        const auto t1 = std::chrono::steady_clock::now();
        const auto dt = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
        fmt::println("{} us", dt.count());
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto dt = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
    fmt::println("{} us total", dt.count());
    fmt::println("{} us/iter", dt.count() / kIters);
}
