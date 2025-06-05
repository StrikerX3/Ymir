#include <ymir/hw/m68k/m68k.hpp>

#include <ymir/hw/scsp/scsp.hpp> // because M68kBus *is* SCSP

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/unreachable.hpp>

#include <cassert>
#include <cstdlib>
#include <type_traits>

namespace ymir::m68k {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   exec

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "M68K";
    };

    struct exec : public base {
        static constexpr std::string_view name = "M68K-Exec";
    };

} // namespace grp

MC68EC000::MC68EC000(M68kBus &bus)
    : m_bus(bus) {
    Reset(true);
}

void MC68EC000::Reset(bool hard) {
    // TODO: check reset values
    if (hard) {
        regs.DA.fill(0);

        m_externalInterruptLevel = 0;
    }

    regs.SP = MemRead<uint32, false>(0x00000000);
    SP_swap = 0;

    PC = MemRead<uint32, false>(0x00000004);
    FullPrefetch();

    SR.u16 = 0;
    SR.S = 1;
    SR.T = 0;
    SR.IPM = 7;
}

FLATTEN uint64 MC68EC000::Step() {
    return Execute();
}

void MC68EC000::SetExternalInterruptLevel(uint8 level) {
    assert(level <= 7);
    m_externalInterruptLevel = level;
}

void MC68EC000::SaveState(state::M68KState &state) const {
    state.DA = regs.DA;
    state.SP_swap = SP_swap;
    state.PC = PC;
    state.SR = SR.u16;
    state.prefetchQueue = m_prefetchQueue;
    state.extIntrLevel = m_externalInterruptLevel;
}

bool MC68EC000::ValidateState(const state::M68KState &state) const {
    return true;
}

void MC68EC000::LoadState(const state::M68KState &state) {
    regs.DA = state.DA;
    SP_swap = state.SP_swap;
    PC = state.PC;
    SR.u16 = state.SR & 0xA71F;
    m_prefetchQueue = state.prefetchQueue;
    m_externalInterruptLevel = state.extIntrLevel;
}

template <mem_primitive T, bool instrFetch>
T MC68EC000::MemRead(uint32 address) {
    if constexpr (std::is_same_v<T, uint32>) {
        const uint32 hi = MemRead<uint16, instrFetch>(address + 0);
        const uint32 lo = MemRead<uint16, instrFetch>(address + 2);
        return (hi << 16u) | lo;
    } else {
        static constexpr uint32 addrMask = ~(sizeof(T) - 1) & 0xFFFFFF;
        address &= addrMask;

        return m_bus.Read<T, instrFetch>(address);
    }
}

template <mem_primitive T, bool instrFetch>
T MC68EC000::MemReadDesc(uint32 address) {
    if constexpr (std::is_same_v<T, uint32>) {
        T value = MemRead<uint16, instrFetch>(address + 2);
        value |= MemRead<uint16, instrFetch>(address + 0) << 16u;
        return value;
    } else {
        return MemRead<T, instrFetch>(address);
    }
}

template <mem_primitive T>
void MC68EC000::MemWrite(uint32 address, T value) {
    if constexpr (std::is_same_v<T, uint32>) {
        MemWrite<uint16>(address + 2, value >> 0u);
        MemWrite<uint16>(address + 0, value >> 16u);
    } else {
        static constexpr uint32 addrMask = ~(sizeof(T) - 1) & 0xFFFFFF;
        address &= addrMask;

        m_bus.Write<T>(address, value);
    }
}

template <mem_primitive T>
void MC68EC000::MemWriteAsc(uint32 address, T value) {
    if constexpr (std::is_same_v<T, uint32>) {
        MemWrite<uint16>(address + 0, value >> 16u);
        MemWrite<uint16>(address + 2, value >> 0u);
    } else {
        MemWrite<T>(address, value);
    }
}

FLATTEN FORCE_INLINE uint16 MC68EC000::FetchInstruction() {
    uint16 instr = MemRead<uint16, true>(PC);
    PC += 2;
    return instr;
}

FORCE_INLINE void MC68EC000::SetSR(uint16 value) {
    const bool oldS = SR.S;
    SR.u16 = value & 0xA71F;

    if (SR.S != oldS) {
        std::swap(regs.SP, SP_swap);
    }
}

FORCE_INLINE void MC68EC000::SetSSP(uint32 value) {
    if (SR.S) {
        regs.SP = value;
    } else {
        SP_swap = value;
    }
}

FORCE_INLINE void MC68EC000::EnterException(ExceptionVector vector) {
    HandleExceptionCommon(vector, SR.IPM);
}

FORCE_INLINE void MC68EC000::HandleInterrupt(ExceptionVector vector, uint8 level) {
    PC -= 4;
    HandleExceptionCommon(vector, level);
}

FORCE_INLINE void MC68EC000::HandleExceptionCommon(ExceptionVector vector, uint8 intrLevel) {
    const uint16 oldSR = SR.u16;
    if (SR.S == 0) {
        std::swap(regs.SP, SP_swap);
    }
    SR.S = 1;
    SR.T = 0;
    SR.IPM = intrLevel;

    MemWrite<uint16>(regs.SP - 2, PC);
    MemWrite<uint16>(regs.SP - 6, oldSR);
    MemWrite<uint16>(regs.SP - 4, PC >> 16u);
    regs.SP -= 6;
    PC = MemRead<uint32, false>(static_cast<uint32>(vector) << 2u);
    FullPrefetch();
}

FORCE_INLINE bool MC68EC000::CheckPrivilege() {
    if (!SR.S) {
        PC -= 4;
        EnterException(ExceptionVector::PrivilegeViolation);
        return false;
    }
    return true;
}

FORCE_INLINE bool MC68EC000::CheckInterrupt() {
    const uint8 level = m_externalInterruptLevel;
    if (level == 7 || level > SR.IPM) {
        ExceptionVector vector = m_bus.AcknowledgeInterrupt(level);
        if (vector == ExceptionVector::AutoVectorRequest) {
            vector = static_cast<ExceptionVector>(static_cast<uint32>(ExceptionVector::BaseAutovector) + level);
        }
        devlog::trace<grp::exec>("Entering interrupt handler for vector {:X}, level {:X}, PC {:X}",
                                 static_cast<uint32>(vector), level, PC);
        HandleInterrupt(vector, level);
        devlog::trace<grp::exec>("Interrupt handler PC: {:X}", PC);
        return true;
    }
    return false;
}

// M   Xn
// 000 <reg>  D<reg>               Data register
// 001 <reg>  A<reg>               Address register
// 010 <reg>  (A<reg>)             Address
// 011 <reg>  (A<reg>)+            Address with postincrement
// 100 <reg>  -(A<reg>)            Address with predecrement
// 101 <reg>  disp(A<reg>)         Address with displacement
// 110 <reg>  disp(A<reg>, <ix>)   Address with index
// 111 010    disp(PC)             Program counter with displacement
// 111 011    disp(PC, <ix>)       Program counter with index
// 111 000    (xxx).w              Absolute short
// 111 001    (xxx).l              Absolute long
// 111 100    #imm                 Immediate

template <mem_primitive T>
FORCE_INLINE T MC68EC000::ReadEffectiveAddress(uint8 M, uint8 Xn) {
    switch (M) {
    case 0b000: return regs.D[Xn];
    case 0b001: return regs.A[Xn];
    case 0b010: return MemRead<T, false>(regs.A[Xn]);
    case 0b011: {
        const T value = MemRead<T, false>(regs.A[Xn]);
        AdvanceAddress<T, true>(Xn);
        return value;
    }
    case 0b100: AdvanceAddress<T, false>(Xn); return MemRead<T, false>(regs.A[Xn]);
    case 0b101: {
        const sint16 disp = static_cast<sint16>(PrefetchNext());
        return MemRead<T, false>(regs.A[Xn] + disp);
    }
    case 0b110: {
        const uint16 briefExtWord = PrefetchNext();

        const sint8 disp = static_cast<sint8>(bit::extract<0, 7>(briefExtWord));
        const bool s = bit::test<11>(briefExtWord);
        const uint8 extXn = bit::extract<12, 14>(briefExtWord);
        const bool m = bit::test<15>(briefExtWord);

        sint32 index = m ? regs.A[extXn] : regs.D[extXn];
        if (!s) {
            // Word index
            index = static_cast<sint16>(index);
        }
        return MemRead<T, false>(regs.A[Xn] + disp + index);
    }
    case 0b111:
        switch (Xn) {
        case 0b010: {
            const sint16 disp = static_cast<sint16>(PrefetchNext());
            return MemRead<T, true>(PC - 4 + disp);
        }
        case 0b011: {
            const uint32 pc = PC - 2;
            const uint16 extWord = PrefetchNext();

            const sint8 disp = bit::extract_signed<0, 7>(extWord);
            const bool wl = bit::test<11>(extWord);
            const uint8 extXn = bit::extract<12, 14>(extWord);
            const bool da = bit::test<15>(extWord);

            sint32 index = da ? regs.A[extXn] : regs.D[extXn];
            if (!wl) {
                // Word index
                index = static_cast<sint16>(index);
            }

            const uint32 address = pc + static_cast<sint8>(disp) + index;
            return MemRead<T, true>(address);
        }
        case 0b000: {
            const sint32 address = static_cast<sint16>(PrefetchNext());
            return MemRead<T, false>(address);
        }
        case 0b001: {
            const uint32 addressHigh = PrefetchNext();
            const uint32 addressLow = PrefetchNext();
            return MemRead<T, false>((addressHigh << 16u) | addressLow);
        }
        case 0b100: {
            uint32 value = PrefetchNext();
            if constexpr (std::is_same_v<T, uint32>) {
                value = (value << 16u) | PrefetchNext();
            }
            return value;
        }
        }
        break;
    }

    util::unreachable();
}

template <mem_primitive T>
FORCE_INLINE void MC68EC000::WriteEffectiveAddress(uint8 M, uint8 Xn, T value) {
    switch (M) {
    case 0b000: bit::deposit_into<0, sizeof(T) * 8 - 1>(regs.D[Xn], value); break;
    case 0b001: regs.A[Xn] = value; break;
    case 0b010: MemWrite<T>(regs.A[Xn], value); break;
    case 0b011:
        MemWrite<T>(regs.A[Xn], value);
        AdvanceAddress<T, true>(Xn);
        break;
    case 0b100:
        AdvanceAddress<T, false>(Xn);
        MemWrite<T>(regs.A[Xn], value);
        break;
    case 0b101: {
        const sint16 disp = static_cast<sint16>(PrefetchNext());
        MemWrite<T>(regs.A[Xn] + disp, value);
        break;
    }
    case 0b110: {
        const uint16 briefExtWord = PrefetchNext();

        const sint8 disp = static_cast<sint8>(bit::extract<0, 7>(briefExtWord));
        const bool s = bit::test<11>(briefExtWord);
        const uint8 extXn = bit::extract<12, 14>(briefExtWord);
        const bool m = bit::test<15>(briefExtWord);

        sint32 index = m ? regs.A[extXn] : regs.D[extXn];
        if (!s) {
            // Word index
            index = static_cast<sint16>(index);
        }
        MemWrite<T>(regs.A[Xn] + disp + index, value);
        break;
    }
    case 0b111:
        switch (Xn) {
        case 0b000: {
            const sint32 address = static_cast<sint16>(PrefetchNext());
            MemWrite<T>(address, value);
            break;
        }
        case 0b001: {
            const uint32 addressHigh = PrefetchNext();
            const uint32 addressLow = m_prefetchQueue[0];
            MemWrite<T>((addressHigh << 16u) | addressLow, value);
            PrefetchNext();
            break;
        }
        }
        break;
    }
}

template <mem_primitive T, bool prefetch, typename FnModify>
FORCE_INLINE void MC68EC000::ModifyEffectiveAddress(uint8 M, uint8 Xn, FnModify &&modify) {
    switch (M) {
    case 0b000: {
        const T value = modify(regs.D[Xn]);
        if constexpr (prefetch) {
            PrefetchTransfer();
        }
        bit::deposit_into<0, sizeof(T) * 8 - 1>(regs.D[Xn], value);
        break;
    }
    case 0b001: {
        const uint32 value = modify(regs.A[Xn]);
        if constexpr (prefetch) {
            PrefetchTransfer();
        }
        regs.A[Xn] = value;
        break;
    }
    case 0b010: {
        const T value = MemRead<T, false>(regs.A[Xn]);
        const T result = modify(value);
        if constexpr (prefetch) {
            PrefetchTransfer();
        }
        MemWrite<T>(regs.A[Xn], result);
        break;
    }
    case 0b011: {
        const T value = MemRead<T, false>(regs.A[Xn]);
        const T result = modify(value);
        if constexpr (prefetch) {
            PrefetchTransfer();
        }
        MemWrite<T>(regs.A[Xn], result);
        AdvanceAddress<T, true>(Xn);
        break;
    }
    case 0b100: {
        AdvanceAddress<T, false>(Xn);
        const T value = MemRead<T, false>(regs.A[Xn]);
        const T result = modify(value);
        if constexpr (prefetch) {
            PrefetchTransfer();
        }
        MemWrite<T>(regs.A[Xn], result);
        break;
    }
    case 0b101: {
        const sint16 disp = static_cast<sint16>(PrefetchNext());
        const uint32 address = regs.A[Xn] + disp;
        const T value = MemRead<T, false>(address);
        const T result = modify(value);
        if constexpr (prefetch) {
            PrefetchTransfer();
        }
        MemWrite<T>(address, result);
        break;
    }
    case 0b110: {
        const uint16 briefExtWord = PrefetchNext();

        const sint8 disp = static_cast<sint8>(bit::extract<0, 7>(briefExtWord));
        const bool s = bit::test<11>(briefExtWord);
        const uint8 extXn = bit::extract<12, 14>(briefExtWord);
        const bool m = bit::test<15>(briefExtWord);

        sint32 index = m ? regs.A[extXn] : regs.D[extXn];
        if (!s) {
            // Word index
            index = static_cast<sint16>(index);
        }
        const uint32 address = regs.A[Xn] + disp + index;
        const T value = MemRead<T, false>(address);
        const T result = modify(value);
        if constexpr (prefetch) {
            PrefetchTransfer();
        }
        MemWrite<T>(address, result);
        break;
    }
    case 0b111:
        switch (Xn) {
        case 0b000: {
            const sint32 address = static_cast<sint16>(PrefetchNext());
            const T value = MemRead<T, false>(address);
            const T result = modify(value);
            if constexpr (prefetch) {
                PrefetchTransfer();
            }
            MemWrite<T>(address, result);
            break;
        }
        case 0b001: {
            const uint32 addressHigh = PrefetchNext();
            const uint32 addressLow = PrefetchNext();
            const uint32 address = (addressHigh << 16u) | addressLow;
            const T value = MemRead<T, false>(address);
            const T result = modify(value);
            if constexpr (prefetch) {
                PrefetchTransfer();
            }
            MemWrite<T>(address, result);
            break;
        }
        }
        break;
    }
}

template <mem_primitive T>
FORCE_INLINE T MC68EC000::MoveEffectiveAddress(uint8 srcM, uint8 srcXn, uint8 dstM, uint8 dstXn) {
    const T value = ReadEffectiveAddress<T>(srcM, srcXn);

    switch (dstM) {
    case 0b000:
        bit::deposit_into<0, sizeof(T) * 8 - 1>(regs.D[dstXn], value);
        PrefetchTransfer();
        break;
    case 0b001:
        regs.A[dstXn] = value;
        PrefetchTransfer();
        break;
    case 0b010: {
        MemWriteAsc<T>(regs.A[dstXn], value);
        PrefetchTransfer();
        break;
    }
    case 0b011: {
        MemWriteAsc<T>(regs.A[dstXn], value);
        AdvanceAddress<T, true>(dstXn);
        PrefetchTransfer();
        break;
    }
    case 0b100: {
        AdvanceAddress<T, false>(dstXn);
        PrefetchTransfer();
        MemWrite<T>(regs.A[dstXn], value);
        break;
    }
    case 0b101: {
        const sint16 disp = static_cast<sint16>(PrefetchNext());
        const uint32 address = regs.A[dstXn] + disp;
        MemWriteAsc<T>(address, value);
        PrefetchTransfer();
        break;
    }
    case 0b110: {
        const uint16 briefExtWord = PrefetchNext();

        const sint8 disp = static_cast<sint8>(bit::extract<0, 7>(briefExtWord));
        const bool s = bit::test<11>(briefExtWord);
        const uint8 extXn = bit::extract<12, 14>(briefExtWord);
        const bool m = bit::test<15>(briefExtWord);

        sint32 index = m ? regs.A[extXn] : regs.D[extXn];
        if (!s) {
            // Word index
            index = static_cast<sint16>(index);
        }
        const uint32 address = regs.A[dstXn] + disp + index;
        MemWriteAsc<T>(address, value);
        PrefetchTransfer();
        break;
    }
    case 0b111:
        switch (dstXn) {
        case 0b000: {
            const sint32 address = static_cast<sint16>(PrefetchNext());
            MemWriteAsc<T>(address, value);
            PrefetchTransfer();
            break;
        }
        case 0b001: {
            const uint32 addressHigh = PrefetchNext();
            const uint32 addressLow = m_prefetchQueue[0];
            const uint32 address = (addressHigh << 16u) | addressLow;
            const bool prefetchEarly = srcM < 2 || (srcM == 7 && srcXn == 4);
            if (prefetchEarly) {
                PrefetchNext();
            }
            MemWriteAsc<T>(address, value);
            if (!prefetchEarly) {
                PrefetchNext();
            }
            PrefetchTransfer();
            break;
        }
        }
        break;
    }

    return value;
}

template <bool fetch>
FORCE_INLINE uint32 MC68EC000::CalcEffectiveAddress(uint8 M, uint8 Xn) {
    auto prefetchLast = [&]() -> uint16 {
        if constexpr (fetch) {
            return PrefetchNext();
        } else {
            return m_prefetchQueue[0];
        }
    };

    static constexpr uint32 pcOffset = fetch ? 4 : 2;

    switch (M) {
    case 0b010: return regs.A[Xn];
    case 0b101: {
        const sint16 disp = static_cast<sint16>(prefetchLast());
        return regs.A[Xn] + disp;
    }
    case 0b110: {
        const uint16 briefExtWord = prefetchLast();

        const sint8 disp = static_cast<sint8>(bit::extract<0, 7>(briefExtWord));
        const bool s = bit::test<11>(briefExtWord);
        const uint8 extXn = bit::extract<12, 14>(briefExtWord);
        const bool m = bit::test<15>(briefExtWord);

        uint32 index = m ? regs.A[extXn] : regs.D[extXn];
        if (!s) {
            // Word index
            index = static_cast<sint16>(index);
        }
        return regs.A[Xn] + disp + index;
    }
    case 0b111:
        switch (Xn) {
        case 0b010: {
            const sint16 disp = static_cast<sint16>(prefetchLast());
            return PC - pcOffset + disp;
        }
        case 0b011: {
            const uint16 briefExtWord = prefetchLast();

            const uint32 address = PC - pcOffset + static_cast<sint8>(bit::extract<0, 7>(briefExtWord));
            const bool s = bit::test<11>(briefExtWord);
            const uint8 extXn = bit::extract<12, 14>(briefExtWord);
            const bool m = bit::test<15>(briefExtWord);

            sint32 index = m ? regs.A[extXn] : regs.D[extXn];
            if (!s) {
                // Word index
                index = static_cast<sint16>(index);
            }
            return address + index;
        }
        case 0b000: {
            const sint32 address = static_cast<sint16>(prefetchLast());
            return address;
        }
        case 0b001: {
            const uint32 addressHigh = PrefetchNext();
            const uint32 addressLow = prefetchLast();
            return (addressHigh << 16u) | addressLow;
        }
        }
    }

    util::unreachable();
}

static constexpr auto kEffectiveAddressCyclesBW = [] {
    std::array<std::array<uint64, 8>, 8> arr{};
    for (uint8 M = 0; M < 8; ++M) {
        for (uint8 Xn = 0; Xn < 8; ++Xn) {
            switch (M) {
            case 0b000: arr[M][Xn] = 0; break;  // Dn
            case 0b001: arr[M][Xn] = 0; break;  // An
            case 0b010: arr[M][Xn] = 4; break;  // (An)
            case 0b011: arr[M][Xn] = 4; break;  // (An)+
            case 0b100: arr[M][Xn] = 6; break;  // -(An)
            case 0b101: arr[M][Xn] = 8; break;  // d(An)
            case 0b110: arr[M][Xn] = 10; break; // d(An,ix)
            case 0b111:
                switch (Xn) {
                case 0b010: arr[M][Xn] = 8; break;  // d(PC)
                case 0b011: arr[M][Xn] = 10; break; // d(PC,ix)
                case 0b000: arr[M][Xn] = 8; break;  // xxx.W
                case 0b001: arr[M][Xn] = 12; break; // xxx.L
                case 0b100: arr[M][Xn] = 4; break;  // #xxx
                }
                break;
            }
        }
    }
    return arr;
}();

static constexpr auto kEffectiveAddressCyclesL = [] {
    std::array<std::array<uint64, 8>, 8> arr{};
    for (uint8 M = 0; M < 8; ++M) {
        for (uint8 Xn = 0; Xn < 8; ++Xn) {
            switch (M) {
            case 0b000: arr[M][Xn] = 0; break;  // Dn
            case 0b001: arr[M][Xn] = 0; break;  // An
            case 0b010: arr[M][Xn] = 8; break;  // (An)
            case 0b011: arr[M][Xn] = 8; break;  // (An)+
            case 0b100: arr[M][Xn] = 10; break; // -(An)
            case 0b101: arr[M][Xn] = 12; break; // d(An)
            case 0b110: arr[M][Xn] = 14; break; // d(An,ix)
            case 0b111:
                switch (Xn) {
                case 0b010: arr[M][Xn] = 12; break; // d(PC)
                case 0b011: arr[M][Xn] = 14; break; // d(PC,ix)
                case 0b000: arr[M][Xn] = 12; break; // xxx.W
                case 0b001: arr[M][Xn] = 16; break; // xxx.L
                case 0b100: arr[M][Xn] = 8; break;  // #xxx
                }
                break;
            }
        }
    }
    return arr;
}();

template <mem_primitive T>
static constexpr auto kEffectiveAddressCycles =
    std::is_same_v<T, uint32> ? kEffectiveAddressCyclesL : kEffectiveAddressCyclesBW;

// Determines if the value is negative
template <std::integral T>
FORCE_INLINE static bool IsNegative(T value) {
    return static_cast<std::make_signed_t<T>>(value) < 0;
}

// Determines if op2+op1 results in a carry
template <std::integral T>
FORCE_INLINE static bool IsAddCarry(T op1, T op2, T result) {
    static constexpr T shift = sizeof(T) * 8 - 1;
    return ((op1 & op2) | (~result & (op1 | op2))) >> shift;
}

// Determines if op2-op1 results in a borrow
template <std::integral T>
FORCE_INLINE static bool IsSubCarry(T op1, T op2, T result) {
    static constexpr T shift = sizeof(T) * 8 - 1;
    return ((op1 & result) | (~op2 & (op1 | result))) >> shift;
}

// Determines if op2+op1 results in an overflow
template <std::integral T>
FORCE_INLINE static bool IsAddOverflow(T op1, T op2, T result) {
    static constexpr T shift = sizeof(T) * 8 - 1;
    return ((op1 ^ result) & (op2 ^ result)) >> shift;
}

// Determines if op2-op1 results in an overflow
template <std::integral T>
FORCE_INLINE static bool IsSubOverflow(T op1, T op2, T result) {
    static constexpr T shift = sizeof(T) * 8 - 1;
    return ((op1 ^ op2) & (result ^ op2)) >> shift;
}

template <std::integral T>
static constexpr auto kShiftTable = [] {
    std::array<T, 65> table{};
    for (T i = 0; i < 65; i++) {
        if (i == 0) {
            table[i] = 0;
        } else if (i < sizeof(T) * 8) {
            table[i] = ~static_cast<T>(0) << (sizeof(T) * 8 - i);
        } else {
            table[i] = ~static_cast<T>(0);
        }
    }
    return table;
}();

// Determines if a left shift causes the most significant bit of the value to change
template <std::integral T>
FORCE_INLINE static bool IsLeftShiftOverflow(T value, T shift) {
    if (shift < sizeof(T) * 8) {
        value &= kShiftTable<T>[shift + 1];
        return value != 0 && value != kShiftTable<T>[shift + 1];
    } else {
        return value != 0;
    }
}

template <std::integral T, bool increment>
FORCE_INLINE void MC68EC000::AdvanceAddress(uint32 An) {
    sint32 amount = sizeof(T);
    if (An == 7 && sizeof(T) == 1) {
        // Turn byte-sized increments into word-sized increments if targeting SP
        amount = sizeof(uint16);
    }
    if constexpr (increment) {
        regs.A[An] += amount;
    } else {
        regs.A[An] -= amount;
    }
}

// -----------------------------------------------------------------------------
// Helper functions for rotates through X flag

template <typename T>
struct ROXOut {
    T value;
    bool X;
};

template <std::integral T>
FORCE_INLINE T shl(T val, T shift) {
    if (shift < sizeof(T) * 8) {
        return val << shift;
    } else {
        return 0;
    }
}

template <std::integral T>
FORCE_INLINE T shr(T val, T shift) {
    if (shift < sizeof(T) * 8) {
        return val >> shift;
    } else {
        return 0;
    }
}

template <typename T>
FORCE_INLINE ROXOut<T> roxl(T val, T shift, bool X) {
    constexpr auto numBits = sizeof(T) * 8 + 1u;
    shift %= numBits;
    return {
        .value = static_cast<T>(shl<T>(val, shift) | shr<T>(val, numBits - shift) | shl<T>(X, shift - 1u)),
        .X = (shift == 0) ? X : static_cast<bool>(shr<T>(val, numBits - shift - 1u) & 1),
    };
}

template <typename T>
FORCE_INLINE ROXOut<T> roxr(T val, T shift, bool X) {
    constexpr auto numBits = sizeof(T) * 8 + 1u;
    shift %= numBits;
    return {
        .value = static_cast<T>(shr<T>(val, shift) | shl<T>(val, numBits - shift) | shl<T>(X, numBits - shift - 1u)),
        .X = (shift == 0) ? X : static_cast<bool>(shr<T>(val, shift - 1u) & 1),
    };
}

// -----------------------------------------------------------------------------
// Prefetch queue

FLATTEN FORCE_INLINE void MC68EC000::FullPrefetch() {
    PrefetchNext();
    PrefetchTransfer();
}

FORCE_INLINE uint16 MC68EC000::PrefetchNext() {
    const uint16 prev = m_prefetchQueue[0];
    m_prefetchQueue[0] = FetchInstruction();
    return prev;
}

FORCE_INLINE void MC68EC000::PrefetchTransfer() {
    // NOTE: consolidating IRC -> IR and IR -> IRD steps here
    // technically they should be separate
    m_prefetchQueue[1] = m_prefetchQueue[0];
    PrefetchNext();
}

// -----------------------------------------------------------------------------
// Interpreter

uint64 MC68EC000::Execute() {
    if (CheckInterrupt()) [[unlikely]] {
        return 44;
    }

    const uint16 instr = m_prefetchQueue[1];

    const OpcodeType type = g_decodeTable.opcodeTypes[instr];
    switch (type) {
    case OpcodeType::Move_EA_EA_B: return Instr_Move_EA_EA<uint8>(instr);
    case OpcodeType::Move_EA_EA_W: return Instr_Move_EA_EA<uint16>(instr);
    case OpcodeType::Move_EA_EA_L: return Instr_Move_EA_EA<uint32>(instr);
    case OpcodeType::MoveA_W: return Instr_MoveA<uint16>(instr);
    case OpcodeType::MoveA_L: return Instr_MoveA<uint32>(instr);
    case OpcodeType::Move_EA_CCR: return Instr_Move_EA_CCR(instr);
    case OpcodeType::Move_EA_SR: return Instr_Move_EA_SR(instr);
    case OpcodeType::Move_CCR_EA: return Instr_Move_CCR_EA(instr);
    case OpcodeType::Move_SR_EA: return Instr_Move_SR_EA(instr);
    case OpcodeType::Move_An_USP: return Instr_Move_An_USP(instr);
    case OpcodeType::Move_USP_An: return Instr_Move_USP_An(instr);
    case OpcodeType::MoveM_EA_Rs_C_W: return Instr_MoveM_EA_Rs<uint16, true>(instr);
    case OpcodeType::MoveM_EA_Rs_C_L: return Instr_MoveM_EA_Rs<uint32, true>(instr);
    case OpcodeType::MoveM_EA_Rs_D_W: return Instr_MoveM_EA_Rs<uint16, false>(instr);
    case OpcodeType::MoveM_EA_Rs_D_L: return Instr_MoveM_EA_Rs<uint32, false>(instr);
    case OpcodeType::MoveM_PI_Rs_W: return Instr_MoveM_PI_Rs<uint16>(instr);
    case OpcodeType::MoveM_PI_Rs_L: return Instr_MoveM_PI_Rs<uint32>(instr);
    case OpcodeType::MoveM_Rs_EA_W: return Instr_MoveM_Rs_EA<uint16>(instr);
    case OpcodeType::MoveM_Rs_EA_L: return Instr_MoveM_Rs_EA<uint32>(instr);
    case OpcodeType::MoveM_Rs_PD_W: return Instr_MoveM_Rs_PD<uint16>(instr);
    case OpcodeType::MoveM_Rs_PD_L: return Instr_MoveM_Rs_PD<uint32>(instr);
    case OpcodeType::MoveP_Ay_Dx_W: return Instr_MoveP_Ay_Dx<uint16>(instr);
    case OpcodeType::MoveP_Ay_Dx_L: return Instr_MoveP_Ay_Dx<uint32>(instr);
    case OpcodeType::MoveP_Dx_Ay_W: return Instr_MoveP_Dx_Ay<uint16>(instr);
    case OpcodeType::MoveP_Dx_Ay_L: return Instr_MoveP_Dx_Ay<uint32>(instr);
    case OpcodeType::MoveQ: return Instr_MoveQ(instr);

    case OpcodeType::Clr_B: return Instr_Clr<uint8>(instr);
    case OpcodeType::Clr_W: return Instr_Clr<uint16>(instr);
    case OpcodeType::Clr_L: return Instr_Clr<uint32>(instr);
    case OpcodeType::Exg_An_An: return Instr_Exg_An_An(instr);
    case OpcodeType::Exg_Dn_An: return Instr_Exg_Dn_An(instr);
    case OpcodeType::Exg_Dn_Dn: return Instr_Exg_Dn_Dn(instr);
    case OpcodeType::Ext_W: return Instr_Ext_W(instr);
    case OpcodeType::Ext_L: return Instr_Ext_L(instr);
    case OpcodeType::Swap: return Instr_Swap(instr);

    case OpcodeType::ABCD_M: return Instr_ABCD_M(instr);
    case OpcodeType::ABCD_R: return Instr_ABCD_R(instr);
    case OpcodeType::NBCD: return Instr_NBCD(instr);
    case OpcodeType::SBCD_M: return Instr_SBCD_M(instr);
    case OpcodeType::SBCD_R: return Instr_SBCD_R(instr);

    case OpcodeType::Add_Dn_EA_B: return Instr_Add_Dn_EA<uint8>(instr);
    case OpcodeType::Add_Dn_EA_W: return Instr_Add_Dn_EA<uint16>(instr);
    case OpcodeType::Add_Dn_EA_L: return Instr_Add_Dn_EA<uint32>(instr);
    case OpcodeType::Add_EA_Dn_B: return Instr_Add_EA_Dn<uint8>(instr);
    case OpcodeType::Add_EA_Dn_W: return Instr_Add_EA_Dn<uint16>(instr);
    case OpcodeType::Add_EA_Dn_L: return Instr_Add_EA_Dn<uint32>(instr);
    case OpcodeType::AddA_W: return Instr_AddA<uint16>(instr);
    case OpcodeType::AddA_L: return Instr_AddA<uint32>(instr);
    case OpcodeType::AddI_B: return Instr_AddI<uint8>(instr);
    case OpcodeType::AddI_W: return Instr_AddI<uint16>(instr);
    case OpcodeType::AddI_L: return Instr_AddI<uint32>(instr);
    case OpcodeType::AddQ_An_W: return Instr_AddQ_An<uint16>(instr);
    case OpcodeType::AddQ_An_L: return Instr_AddQ_An<uint32>(instr);
    case OpcodeType::AddQ_EA_B: return Instr_AddQ_EA<uint8>(instr);
    case OpcodeType::AddQ_EA_W: return Instr_AddQ_EA<uint16>(instr);
    case OpcodeType::AddQ_EA_L: return Instr_AddQ_EA<uint32>(instr);
    case OpcodeType::AddX_M_B: return Instr_AddX_M<uint8>(instr);
    case OpcodeType::AddX_M_W: return Instr_AddX_M<uint16>(instr);
    case OpcodeType::AddX_M_L: return Instr_AddX_M<uint32>(instr);
    case OpcodeType::AddX_R_B: return Instr_AddX_R<uint8>(instr);
    case OpcodeType::AddX_R_W: return Instr_AddX_R<uint16>(instr);
    case OpcodeType::AddX_R_L: return Instr_AddX_R<uint32>(instr);
    case OpcodeType::And_Dn_EA_B: return Instr_And_Dn_EA<uint8>(instr);
    case OpcodeType::And_Dn_EA_W: return Instr_And_Dn_EA<uint16>(instr);
    case OpcodeType::And_Dn_EA_L: return Instr_And_Dn_EA<uint32>(instr);
    case OpcodeType::And_EA_Dn_B: return Instr_And_EA_Dn<uint8>(instr);
    case OpcodeType::And_EA_Dn_W: return Instr_And_EA_Dn<uint16>(instr);
    case OpcodeType::And_EA_Dn_L: return Instr_And_EA_Dn<uint32>(instr);
    case OpcodeType::AndI_EA_B: return Instr_AndI_EA<uint8>(instr);
    case OpcodeType::AndI_EA_W: return Instr_AndI_EA<uint16>(instr);
    case OpcodeType::AndI_EA_L: return Instr_AndI_EA<uint32>(instr);
    case OpcodeType::AndI_CCR: return Instr_AndI_CCR(instr);
    case OpcodeType::AndI_SR: return Instr_AndI_SR(instr);
    case OpcodeType::Eor_Dn_EA_B: return Instr_Eor_Dn_EA<uint8>(instr);
    case OpcodeType::Eor_Dn_EA_W: return Instr_Eor_Dn_EA<uint16>(instr);
    case OpcodeType::Eor_Dn_EA_L: return Instr_Eor_Dn_EA<uint32>(instr);
    case OpcodeType::EorI_EA_B: return Instr_EorI_EA<uint8>(instr);
    case OpcodeType::EorI_EA_W: return Instr_EorI_EA<uint16>(instr);
    case OpcodeType::EorI_EA_L: return Instr_EorI_EA<uint32>(instr);
    case OpcodeType::EorI_CCR: return Instr_EorI_CCR(instr);
    case OpcodeType::EorI_SR: return Instr_EorI_SR(instr);
    case OpcodeType::Neg_B: return Instr_Neg<uint8>(instr);
    case OpcodeType::Neg_W: return Instr_Neg<uint16>(instr);
    case OpcodeType::Neg_L: return Instr_Neg<uint32>(instr);
    case OpcodeType::NegX_B: return Instr_NegX<uint8>(instr);
    case OpcodeType::NegX_W: return Instr_NegX<uint16>(instr);
    case OpcodeType::NegX_L: return Instr_NegX<uint32>(instr);
    case OpcodeType::Not_B: return Instr_Not<uint8>(instr);
    case OpcodeType::Not_W: return Instr_Not<uint16>(instr);
    case OpcodeType::Not_L: return Instr_Not<uint32>(instr);
    case OpcodeType::Or_Dn_EA_B: return Instr_Or_Dn_EA<uint8>(instr);
    case OpcodeType::Or_Dn_EA_W: return Instr_Or_Dn_EA<uint16>(instr);
    case OpcodeType::Or_Dn_EA_L: return Instr_Or_Dn_EA<uint32>(instr);
    case OpcodeType::Or_EA_Dn_B: return Instr_Or_EA_Dn<uint8>(instr);
    case OpcodeType::Or_EA_Dn_W: return Instr_Or_EA_Dn<uint16>(instr);
    case OpcodeType::Or_EA_Dn_L: return Instr_Or_EA_Dn<uint32>(instr);
    case OpcodeType::OrI_EA_B: return Instr_OrI_EA<uint8>(instr);
    case OpcodeType::OrI_EA_W: return Instr_OrI_EA<uint16>(instr);
    case OpcodeType::OrI_EA_L: return Instr_OrI_EA<uint32>(instr);
    case OpcodeType::OrI_CCR: return Instr_OrI_CCR(instr);
    case OpcodeType::OrI_SR: return Instr_OrI_SR(instr);
    case OpcodeType::Sub_Dn_EA_B: return Instr_Sub_Dn_EA<uint8>(instr);
    case OpcodeType::Sub_Dn_EA_W: return Instr_Sub_Dn_EA<uint16>(instr);
    case OpcodeType::Sub_Dn_EA_L: return Instr_Sub_Dn_EA<uint32>(instr);
    case OpcodeType::Sub_EA_Dn_B: return Instr_Sub_EA_Dn<uint8>(instr);
    case OpcodeType::Sub_EA_Dn_W: return Instr_Sub_EA_Dn<uint16>(instr);
    case OpcodeType::Sub_EA_Dn_L: return Instr_Sub_EA_Dn<uint32>(instr);
    case OpcodeType::SubA_W: return Instr_SubA<uint16>(instr);
    case OpcodeType::SubA_L: return Instr_SubA<uint32>(instr);
    case OpcodeType::SubI_B: return Instr_SubI<uint8>(instr);
    case OpcodeType::SubI_W: return Instr_SubI<uint16>(instr);
    case OpcodeType::SubI_L: return Instr_SubI<uint32>(instr);
    case OpcodeType::SubQ_An_W: return Instr_SubQ_An<uint16>(instr);
    case OpcodeType::SubQ_An_L: return Instr_SubQ_An<uint32>(instr);
    case OpcodeType::SubQ_EA_B: return Instr_SubQ_EA<uint8>(instr);
    case OpcodeType::SubQ_EA_W: return Instr_SubQ_EA<uint16>(instr);
    case OpcodeType::SubQ_EA_L: return Instr_SubQ_EA<uint32>(instr);
    case OpcodeType::SubX_M_B: return Instr_SubX_M<uint8>(instr);
    case OpcodeType::SubX_M_W: return Instr_SubX_M<uint16>(instr);
    case OpcodeType::SubX_M_L: return Instr_SubX_M<uint32>(instr);
    case OpcodeType::SubX_R_B: return Instr_SubX_R<uint8>(instr);
    case OpcodeType::SubX_R_W: return Instr_SubX_R<uint16>(instr);
    case OpcodeType::SubX_R_L: return Instr_SubX_R<uint32>(instr);

    case OpcodeType::DivS: return Instr_DivS(instr);
    case OpcodeType::DivU: return Instr_DivU(instr);
    case OpcodeType::MulS: return Instr_MulS(instr);
    case OpcodeType::MulU: return Instr_MulU(instr);

    case OpcodeType::BChg_I_Dn: return Instr_BChg_I_Dn(instr);
    case OpcodeType::BChg_I_EA: return Instr_BChg_I_EA(instr);
    case OpcodeType::BChg_R_Dn: return Instr_BChg_R_Dn(instr);
    case OpcodeType::BChg_R_EA: return Instr_BChg_R_EA(instr);
    case OpcodeType::BClr_I_Dn: return Instr_BClr_I_Dn(instr);
    case OpcodeType::BClr_I_EA: return Instr_BClr_I_EA(instr);
    case OpcodeType::BClr_R_Dn: return Instr_BClr_R_Dn(instr);
    case OpcodeType::BClr_R_EA: return Instr_BClr_R_EA(instr);
    case OpcodeType::BSet_I_Dn: return Instr_BSet_I_Dn(instr);
    case OpcodeType::BSet_I_EA: return Instr_BSet_I_EA(instr);
    case OpcodeType::BSet_R_Dn: return Instr_BSet_R_Dn(instr);
    case OpcodeType::BSet_R_EA: return Instr_BSet_R_EA(instr);
    case OpcodeType::BTst_I_Dn: return Instr_BTst_I_Dn(instr);
    case OpcodeType::BTst_I_EA: return Instr_BTst_I_EA(instr);
    case OpcodeType::BTst_R_Dn: return Instr_BTst_R_Dn(instr);
    case OpcodeType::BTst_R_EA: return Instr_BTst_R_EA(instr);

    case OpcodeType::ASL_I_B: return Instr_ASL_I<uint8>(instr);
    case OpcodeType::ASL_I_W: return Instr_ASL_I<uint16>(instr);
    case OpcodeType::ASL_I_L: return Instr_ASL_I<uint32>(instr);
    case OpcodeType::ASL_M: return Instr_ASL_M(instr);
    case OpcodeType::ASL_R_B: return Instr_ASL_R<uint8>(instr);
    case OpcodeType::ASL_R_W: return Instr_ASL_R<uint16>(instr);
    case OpcodeType::ASL_R_L: return Instr_ASL_R<uint32>(instr);
    case OpcodeType::ASR_I_B: return Instr_ASR_I<uint8>(instr);
    case OpcodeType::ASR_I_W: return Instr_ASR_I<uint16>(instr);
    case OpcodeType::ASR_I_L: return Instr_ASR_I<uint32>(instr);
    case OpcodeType::ASR_M: return Instr_ASR_M(instr);
    case OpcodeType::ASR_R_B: return Instr_ASR_R<uint8>(instr);
    case OpcodeType::ASR_R_W: return Instr_ASR_R<uint16>(instr);
    case OpcodeType::ASR_R_L: return Instr_ASR_R<uint32>(instr);
    case OpcodeType::LSL_I_B: return Instr_LSL_I<uint8>(instr);
    case OpcodeType::LSL_I_W: return Instr_LSL_I<uint16>(instr);
    case OpcodeType::LSL_I_L: return Instr_LSL_I<uint32>(instr);
    case OpcodeType::LSL_M: return Instr_LSL_M(instr);
    case OpcodeType::LSL_R_B: return Instr_LSL_R<uint8>(instr);
    case OpcodeType::LSL_R_W: return Instr_LSL_R<uint16>(instr);
    case OpcodeType::LSL_R_L: return Instr_LSL_R<uint32>(instr);
    case OpcodeType::LSR_I_B: return Instr_LSR_I<uint8>(instr);
    case OpcodeType::LSR_I_W: return Instr_LSR_I<uint16>(instr);
    case OpcodeType::LSR_I_L: return Instr_LSR_I<uint32>(instr);
    case OpcodeType::LSR_M: return Instr_LSR_M(instr);
    case OpcodeType::LSR_R_B: return Instr_LSR_R<uint8>(instr);
    case OpcodeType::LSR_R_W: return Instr_LSR_R<uint16>(instr);
    case OpcodeType::LSR_R_L: return Instr_LSR_R<uint32>(instr);
    case OpcodeType::ROL_I_B: return Instr_ROL_I<uint8>(instr);
    case OpcodeType::ROL_I_W: return Instr_ROL_I<uint16>(instr);
    case OpcodeType::ROL_I_L: return Instr_ROL_I<uint32>(instr);
    case OpcodeType::ROL_M: return Instr_ROL_M(instr);
    case OpcodeType::ROL_R_B: return Instr_ROL_R<uint8>(instr);
    case OpcodeType::ROL_R_W: return Instr_ROL_R<uint16>(instr);
    case OpcodeType::ROL_R_L: return Instr_ROL_R<uint32>(instr);
    case OpcodeType::ROR_I_B: return Instr_ROR_I<uint8>(instr);
    case OpcodeType::ROR_I_W: return Instr_ROR_I<uint16>(instr);
    case OpcodeType::ROR_I_L: return Instr_ROR_I<uint32>(instr);
    case OpcodeType::ROR_M: return Instr_ROR_M(instr);
    case OpcodeType::ROR_R_B: return Instr_ROR_R<uint8>(instr);
    case OpcodeType::ROR_R_W: return Instr_ROR_R<uint16>(instr);
    case OpcodeType::ROR_R_L: return Instr_ROR_R<uint32>(instr);
    case OpcodeType::ROXL_I_B: return Instr_ROXL_I<uint8>(instr);
    case OpcodeType::ROXL_I_W: return Instr_ROXL_I<uint16>(instr);
    case OpcodeType::ROXL_I_L: return Instr_ROXL_I<uint32>(instr);
    case OpcodeType::ROXL_M: return Instr_ROXL_M(instr);
    case OpcodeType::ROXL_R_B: return Instr_ROXL_R<uint8>(instr);
    case OpcodeType::ROXL_R_W: return Instr_ROXL_R<uint16>(instr);
    case OpcodeType::ROXL_R_L: return Instr_ROXL_R<uint32>(instr);
    case OpcodeType::ROXR_I_B: return Instr_ROXR_I<uint8>(instr);
    case OpcodeType::ROXR_I_W: return Instr_ROXR_I<uint16>(instr);
    case OpcodeType::ROXR_I_L: return Instr_ROXR_I<uint32>(instr);
    case OpcodeType::ROXR_M: return Instr_ROXR_M(instr);
    case OpcodeType::ROXR_R_B: return Instr_ROXR_R<uint8>(instr);
    case OpcodeType::ROXR_R_W: return Instr_ROXR_R<uint16>(instr);
    case OpcodeType::ROXR_R_L: return Instr_ROXR_R<uint32>(instr);

    case OpcodeType::Cmp_B: return Instr_Cmp<uint8>(instr);
    case OpcodeType::Cmp_W: return Instr_Cmp<uint16>(instr);
    case OpcodeType::Cmp_L: return Instr_Cmp<uint32>(instr);
    case OpcodeType::CmpA_W: return Instr_CmpA<uint16>(instr);
    case OpcodeType::CmpA_L: return Instr_CmpA<uint32>(instr);
    case OpcodeType::CmpI_B: return Instr_CmpI<uint8>(instr);
    case OpcodeType::CmpI_W: return Instr_CmpI<uint16>(instr);
    case OpcodeType::CmpI_L: return Instr_CmpI<uint32>(instr);
    case OpcodeType::CmpM_B: return Instr_CmpM<uint8>(instr);
    case OpcodeType::CmpM_W: return Instr_CmpM<uint16>(instr);
    case OpcodeType::CmpM_L: return Instr_CmpM<uint32>(instr);
    case OpcodeType::Scc: return Instr_Scc(instr);
    case OpcodeType::TAS: return Instr_TAS(instr);
    case OpcodeType::Tst_B: return Instr_Tst<uint8>(instr);
    case OpcodeType::Tst_W: return Instr_Tst<uint16>(instr);
    case OpcodeType::Tst_L: return Instr_Tst<uint32>(instr);

    case OpcodeType::LEA: return Instr_LEA(instr);
    case OpcodeType::PEA: return Instr_PEA(instr);

    case OpcodeType::Link: return Instr_Link(instr);
    case OpcodeType::Unlink: return Instr_Unlink(instr);

    case OpcodeType::BRA: return Instr_BRA(instr);
    case OpcodeType::BSR: return Instr_BSR(instr);
    case OpcodeType::Bcc: return Instr_Bcc(instr);
    case OpcodeType::DBcc: return Instr_DBcc(instr);
    case OpcodeType::JSR: return Instr_JSR(instr);
    case OpcodeType::Jmp: return Instr_Jmp(instr);

    case OpcodeType::RTE: return Instr_RTE(instr);
    case OpcodeType::RTR: return Instr_RTR(instr);
    case OpcodeType::RTS: return Instr_RTS(instr);

    case OpcodeType::Chk: return Instr_Chk(instr);
    case OpcodeType::Reset: return Instr_Reset(instr);
    case OpcodeType::Stop: return Instr_Stop(instr);
    case OpcodeType::Trap: return Instr_Trap(instr);
    case OpcodeType::TrapV: return Instr_TrapV(instr);

    case OpcodeType::Noop: return Instr_Noop(instr);

    case OpcodeType::Illegal1010: return Instr_Illegal1010(instr);
    case OpcodeType::Illegal1111: return Instr_Illegal1111(instr);
    case OpcodeType::Illegal: return Instr_Illegal(instr);

    default:
#ifndef NDEBUG
        // this should absolutely never happen
        fmt::println("M68K: unexpected instruction type {} for opcode {:04X} at {:08X} -- this is a bug!",
                     static_cast<uint32>(type), instr, PC - 2);
        return 0;
#else
        util::unreachable();
#endif
    }
}

// -----------------------------------------------------------------------------
// Instruction interpreters

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Move_EA_EA(uint16 instr) {
    const uint32 dstXn = bit::extract<9, 11>(instr);
    const uint32 dstM = bit::extract<6, 8>(instr);
    const uint32 srcXn = bit::extract<0, 2>(instr);
    const uint32 srcM = bit::extract<3, 5>(instr);

    const T value = MoveEffectiveAddress<T>(srcM, srcXn, dstM, dstXn);
    SR.N = IsNegative(value);
    SR.Z = value == 0;
    SR.V = SR.C = 0;
    return kEffectiveAddressCycles<T>[srcM][srcXn] + kEffectiveAddressCycles<T>[dstM][dstXn] + 4;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_MoveA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 An = bit::extract<9, 11>(instr);

    using ST = std::make_signed_t<T>;

    regs.A[An] = static_cast<ST>(ReadEffectiveAddress<T>(M, Xn));

    PrefetchTransfer();
    return kEffectiveAddressCycles<T>[M][Xn] + 4;
}

FORCE_INLINE uint64 MC68EC000::Instr_Move_EA_CCR(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 value = ReadEffectiveAddress<uint16>(M, Xn);
    SR.xflags = value;

    PC -= 2;
    FullPrefetch();
    return M <= 1 ? 12 : kEffectiveAddressCycles<uint16>[M][Xn] + 12;
}

FORCE_INLINE uint64 MC68EC000::Instr_Move_EA_SR(uint16 instr) {
    if (CheckPrivilege()) {
        const uint16 Xn = bit::extract<0, 2>(instr);
        const uint16 M = bit::extract<3, 5>(instr);
        const uint16 value = ReadEffectiveAddress<uint16>(M, Xn);
        SetSR(value);

        PC -= 2;
        FullPrefetch();
        return M <= 1 ? 12 : kEffectiveAddressCycles<uint16>[M][Xn] + 12;
    }
    return 34;
}

FORCE_INLINE uint64 MC68EC000::Instr_Move_CCR_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 value = SR.xflags;
    WriteEffectiveAddress<uint16>(M, Xn, value);

    PrefetchTransfer();
    return M <= 1 ? 6 : kEffectiveAddressCycles<uint16>[M][Xn] + 8;
}

FORCE_INLINE uint64 MC68EC000::Instr_Move_SR_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    ModifyEffectiveAddress<uint16>(M, Xn, [&](uint16) { return SR.u16; });
    return M <= 1 ? 6 : kEffectiveAddressCycles<uint16>[M][Xn] + 8;
}

FORCE_INLINE uint64 MC68EC000::Instr_Move_An_USP(uint16 instr) {
    if (CheckPrivilege()) {
        const uint16 An = bit::extract<0, 2>(instr);
        SP_swap = regs.A[An];

        PrefetchTransfer();
        return 4;
    }
    return 34;
}

FORCE_INLINE uint64 MC68EC000::Instr_Move_USP_An(uint16 instr) {
    if (CheckPrivilege()) {
        const uint16 An = bit::extract<0, 2>(instr);
        regs.A[An] = SP_swap;

        PrefetchTransfer();
        return 4;
    }
    return 34;
}

template <mem_primitive T, bool instrFetch>
FORCE_INLINE uint64 MC68EC000::Instr_MoveM_EA_Rs(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 regList = PrefetchNext();
    uint32 address = CalcEffectiveAddress(M, Xn);

    for (uint32 i = 0; i < 16; i++) {
        if (regList & (1u << i)) {
            const T value = MemRead<T, instrFetch>(address);
            regs.DA[i] = static_cast<std::make_signed_t<T>>(value);
            address += sizeof(T);
        }
    }
    // An extra memory fetch occurs after the transfers are done
    MemRead<T, instrFetch>(address);

    PrefetchTransfer();

    static constexpr auto kCycles = [] {
        std::array<std::array<uint64, 8>, 8> arr{};
        for (uint8 M = 0; M < 8; ++M) {
            for (uint8 Xn = 0; Xn < 8; ++Xn) {
                switch (M) {
                case 0b010: arr[M][Xn] = 12; break; // (An)
                case 0b101: arr[M][Xn] = 16; break; // d(An)
                case 0b110: arr[M][Xn] = 18; break; // d(An,ix)
                case 0b111:
                    switch (Xn) {
                    case 0b010: arr[M][Xn] = 16; break; // d(PC)
                    case 0b011: arr[M][Xn] = 18; break; // d(PC,ix)
                    case 0b000: arr[M][Xn] = 16; break; // xxx.W
                    case 0b001: arr[M][Xn] = 20; break; // xxx.L
                    }
                    break;
                }
            }
        }
        return arr;
    }();

    static constexpr uint64 kCyclesPerAccess = std::is_same_v<T, uint32> ? 8 : 4;
    return kCycles[M][Xn] + 4 + std::popcount(regList) * kCyclesPerAccess;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_MoveM_PI_Rs(uint16 instr) {
    const uint16 An = bit::extract<0, 2>(instr);
    const uint16 regList = PrefetchNext();

    for (uint32 i = 0; i < 16; i++) {
        if (regList & (1u << i)) {
            const uint32 address = regs.A[An];
            const T value = MemRead<T, false>(address);
            regs.DA[i] = static_cast<std::make_signed_t<T>>(value);
            regs.A[An] = address + sizeof(T);
        }
    }
    // An extra memory fetch occurs after the transfers are done
    MemRead<uint16, false>(regs.A[An]);

    PrefetchTransfer();
    static constexpr uint64 kCyclesPerAccess = std::is_same_v<T, uint32> ? 8 : 4;
    return 12 + std::popcount(regList) * kCyclesPerAccess;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_MoveM_Rs_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 regList = PrefetchNext();
    uint32 address = CalcEffectiveAddress(M, Xn);

    for (uint32 i = 0; i < 16; i++) {
        if (regList & (1u << i)) {
            const T value = regs.DA[i];
            MemWriteAsc<T>(address, value);
            address += sizeof(T);
        }
    }

    PrefetchTransfer();

    static constexpr auto kCycles = [] {
        std::array<std::array<uint64, 8>, 8> arr{};
        for (uint8 M = 0; M < 8; ++M) {
            for (uint8 Xn = 0; Xn < 8; ++Xn) {
                switch (M) {
                case 0b010: arr[M][Xn] = 8; break;  // (An)
                case 0b101: arr[M][Xn] = 12; break; // d(An)
                case 0b110: arr[M][Xn] = 14; break; // d(An,ix)
                case 0b111:
                    switch (Xn) {
                    case 0b000: arr[M][Xn] = 12; break; // xxx.W
                    case 0b001: arr[M][Xn] = 16; break; // xxx.L
                    }
                    break;
                }
            }
        }
        return arr;
    }();

    static constexpr uint64 kCyclesPerAccess = std::is_same_v<T, uint32> ? 8 : 4;
    return kCycles[M][Xn] + std::popcount(regList) * kCyclesPerAccess;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_MoveM_Rs_PD(uint16 instr) {
    const uint16 An = bit::extract<0, 2>(instr);
    const uint16 regList = PrefetchNext();

    const uint32 baseAddress = regs.A[An];

    for (uint32 i = 0; i < 16; i++) {
        if (regList & (1u << i)) {
            const uint32 address = regs.A[An] - sizeof(T);
            const T value = 7 - i == An ? baseAddress : regs.DA[15 - i];
            MemWrite<T>(address, value);
            regs.A[An] = address;
        }
    }

    PrefetchTransfer();
    static constexpr uint64 kCyclesPerAccess = std::is_same_v<T, uint32> ? 8 : 4;
    return 8 + std::popcount(regList) * kCyclesPerAccess;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_MoveP_Ay_Dx(uint16 instr) {
    const uint16 Ay = bit::extract<0, 2>(instr);
    const uint16 Dx = bit::extract<9, 11>(instr);

    const sint16 disp = PrefetchNext();
    const uint32 baseAddress = regs.A[Ay] + disp;

    T finalValue = 0;
    for (uint32 i = 0; i < sizeof(T); i++) {
        const uint32 address = baseAddress + i * sizeof(uint16);
        const uint8 value = MemRead<uint8, false>(address);
        finalValue = (finalValue << 8u) | value;
    }
    bit::deposit_into<0, sizeof(T) * 8 - 1>(regs.D[Dx], finalValue);

    PrefetchTransfer();
    return std::is_same_v<T, uint32> ? 24 : 16;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_MoveP_Dx_Ay(uint16 instr) {
    const uint16 Ay = bit::extract<0, 2>(instr);
    const uint16 Dx = bit::extract<9, 11>(instr);

    const sint16 disp = PrefetchNext();
    const uint32 baseAddress = regs.A[Ay] + disp;

    for (uint32 i = 0; i < sizeof(T); i++) {
        const uint8 value = regs.D[Dx] >> ((sizeof(T) - 1 - i) * 8u);
        const uint32 address = baseAddress + i * sizeof(uint16);
        MemWrite<uint8>(address, value);
    }

    PrefetchTransfer();
    return std::is_same_v<T, uint32> ? 24 : 16;
}

FORCE_INLINE uint64 MC68EC000::Instr_MoveQ(uint16 instr) {
    const sint32 value = static_cast<sint8>(bit::extract<0, 7>(instr));
    const uint32 Dn = bit::extract<9, 11>(instr);
    regs.D[Dn] = value;
    SR.N = IsNegative(value);
    SR.Z = value == 0;
    SR.V = SR.C = 0;

    PrefetchTransfer();
    return 4;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Clr(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<T>(M, Xn, [&](T) {
        SR.Z = 1;
        SR.N = SR.V = SR.C = 0;
        return 0;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M <= 1 ? 6 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    } else {
        return M <= 1 ? 4 : kEffectiveAddressCycles<T>[M][Xn] + 8;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_Exg_An_An(uint16 instr) {
    const uint32 Ry = bit::extract<0, 2>(instr);
    const uint32 Rx = bit::extract<9, 11>(instr);

    std::swap(regs.A[Rx], regs.A[Ry]);

    PrefetchTransfer();
    return 6;
}

FORCE_INLINE uint64 MC68EC000::Instr_Exg_Dn_An(uint16 instr) {
    const uint32 Ry = bit::extract<0, 2>(instr);
    const uint32 Rx = bit::extract<9, 11>(instr);

    std::swap(regs.D[Rx], regs.A[Ry]);

    PrefetchTransfer();
    return 6;
}

FORCE_INLINE uint64 MC68EC000::Instr_Exg_Dn_Dn(uint16 instr) {
    const uint32 Ry = bit::extract<0, 2>(instr);
    const uint32 Rx = bit::extract<9, 11>(instr);

    std::swap(regs.D[Rx], regs.D[Ry]);

    PrefetchTransfer();
    return 6;
}

FORCE_INLINE uint64 MC68EC000::Instr_Ext_W(uint16 instr) {
    const uint32 Dn = bit::extract<0, 2>(instr);

    const sint8 value = regs.D[Dn];
    bit::deposit_into<0, 15>(regs.D[Dn], value);
    SR.N = value < 0;
    SR.Z = value == 0;
    SR.V = 0;
    SR.C = 0;

    PrefetchTransfer();
    return 4;
}

FORCE_INLINE uint64 MC68EC000::Instr_Ext_L(uint16 instr) {
    const uint32 Dn = bit::extract<0, 2>(instr);

    const sint16 value = regs.D[Dn];
    regs.D[Dn] = value;
    SR.N = value < 0;
    SR.Z = value == 0;
    SR.V = 0;
    SR.C = 0;

    PrefetchTransfer();
    return 4;
}

FORCE_INLINE uint64 MC68EC000::Instr_Swap(uint16 instr) {
    const uint32 Dn = bit::extract<0, 2>(instr);
    const uint32 value = (regs.D[Dn] >> 16u) | (regs.D[Dn] << 16u);
    regs.D[Dn] = value;
    SR.N = IsNegative(value);
    SR.Z = value == 0;
    SR.V = SR.C = 0;

    PrefetchTransfer();
    return 4;
}

FORCE_INLINE uint64 MC68EC000::Instr_ABCD_M(uint16 instr) {
    const uint16 Ay = bit::extract<0, 2>(instr);
    const uint16 Ax = bit::extract<9, 11>(instr);

    AdvanceAddress<uint8, false>(Ay);
    const uint8 op1 = MemRead<uint8, false>(regs.A[Ay]);
    AdvanceAddress<uint8, false>(Ax);
    const uint8 op2 = MemRead<uint8, false>(regs.A[Ax]);

    // Thanks to raddad772 for the implementation

    const uint16 unadjustedResult = op1 + op2 + SR.X;
    sint16 result = (op2 & 0xF) + (op1 & 0xF) + SR.X;
    result += (op2 & 0xF0) + (op1 & 0xF0) + (((9 - result) >> 4) & 6);
    result += ((0x9F - result) >> 4) & 0x60;

    SR.Z &= (result & 0xFF) == 0;
    SR.X = SR.C = result > 0xFF;
    SR.N = IsNegative<uint8>(result);
    SR.V = IsNegative<uint8>(~unadjustedResult & result);

    PrefetchTransfer();

    MemWrite<uint8>(regs.A[Ax], result);
    return 18;
}

FORCE_INLINE uint64 MC68EC000::Instr_ABCD_R(uint16 instr) {
    const uint16 Dy = bit::extract<0, 2>(instr);
    const uint16 Dx = bit::extract<9, 11>(instr);

    const uint8 op1 = regs.D[Dy];
    const uint8 op2 = regs.D[Dx];

    // Thanks to raddad772 for the implementation

    const uint16 unadjustedResult = op2 + op1 + SR.X;
    sint16 result = (op2 & 0xF) + (op1 & 0xF) + SR.X;
    result += (op2 & 0xF0) + (op1 & 0xF0) + (((9 - result) >> 4) & 6);
    result += ((0x9F - result) >> 4) & 0x60;

    SR.Z &= (result & 0xFF) == 0;
    SR.X = SR.C = result > 0xFF;
    SR.N = IsNegative<uint8>(result);
    SR.V = IsNegative<uint8>(~unadjustedResult & result);

    bit::deposit_into<0, 7>(regs.D[Dx], result);

    PrefetchTransfer();
    return 6;
}

FORCE_INLINE uint64 MC68EC000::Instr_NBCD(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<uint8>(M, Xn, [&](uint8 op1) {
        // Thanks to raddad772 and the ares emulator for the implementation

        uint16 result = 0 - op1 - SR.X;
        const bool adjustLo = bit::test<4>(op1 ^ result);
        const bool adjustHi = bit::test<8>(result);

        bool c = false;
        bool v = false;

        if (adjustLo) {
            uint16 previous = result;
            result -= 0x06;
            c = bit::test<7>(~previous & result);
            v |= bit::test<7>(previous & ~result);
        }
        if (adjustHi) {
            uint16 previous = result;
            result -= 0x60;
            c = true;
            v |= bit::test<7>(previous & ~result);
        }

        SR.Z &= (result & 0xFF) == 0;
        SR.X = SR.C = c;
        SR.N = IsNegative<uint8>(result);
        SR.V = v;

        bit::deposit_into<0, 7>(op1, result);
        return op1;
    });
    return M <= 1 ? 6 : kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

FORCE_INLINE uint64 MC68EC000::Instr_SBCD_M(uint16 instr) {
    const uint16 Ay = bit::extract<0, 2>(instr);
    const uint16 Ax = bit::extract<9, 11>(instr);

    AdvanceAddress<uint8, false>(Ay);
    const uint8 op1 = MemRead<uint8, false>(regs.A[Ay]);
    AdvanceAddress<uint8, false>(Ax);
    const uint8 op2 = MemRead<uint8, false>(regs.A[Ax]);

    // Thanks to raddad772 for the implementation

    const uint16 unadjustedResult = op2 - op1 - SR.X;
    const sint16 top = (op2 & 0xF0) - (op1 & 0xF0) - (0x60 & (unadjustedResult >> 4));
    sint16 result = (op2 & 0xF) - (op1 & 0xF) - SR.X;
    const sint16 lowAdjustment = 0x06 & (result >> 4);
    result += top - lowAdjustment;
    SR.Z &= (result & 0xFF) == 0;
    SR.X = SR.C = bit::extract<8, 9>(unadjustedResult - lowAdjustment) != 0;
    SR.N = IsNegative<uint8>(result);
    SR.V = IsNegative<uint8>(unadjustedResult & ~result);

    PrefetchTransfer();

    MemWrite<uint8>(regs.A[Ax], result);
    return 18;
}

FORCE_INLINE uint64 MC68EC000::Instr_SBCD_R(uint16 instr) {
    const uint16 Dy = bit::extract<0, 2>(instr);
    const uint16 Dx = bit::extract<9, 11>(instr);

    const uint8 op1 = regs.D[Dy];
    const uint8 op2 = regs.D[Dx];

    // Thanks to raddad772 for the implementation

    const uint16 unadjustedResult = op2 - op1 - SR.X;
    const sint16 top = (op2 & 0xF0) - (op1 & 0xF0) - (0x60 & (unadjustedResult >> 4));
    sint16 result = (op2 & 0xF) - (op1 & 0xF) - SR.X;
    const sint16 lowAdjustment = 0x06 & (result >> 4);
    result += top - lowAdjustment;
    SR.Z &= (result & 0xFF) == 0;
    SR.X = SR.C = bit::extract<8, 9>(unadjustedResult - lowAdjustment) != 0;
    SR.N = IsNegative<uint8>(result);
    SR.V = IsNegative<uint8>(unadjustedResult & ~result);

    bit::deposit_into<0, 7>(regs.D[Dx], result);

    PrefetchTransfer();
    return 6;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Add_Dn_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const T op1 = regs.D[Dn];
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 + op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = IsAddOverflow(op1, op2, result);
        SR.C = SR.X = IsAddCarry(op1, op2, result);
        return result;
    });
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 12 : 8);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Add_EA_Dn(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const T op1 = ReadEffectiveAddress<T>(M, Xn);
    const T op2 = regs.D[Dn];
    const T result = op2 + op1;
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = IsAddOverflow(op1, op2, result);
    SR.C = SR.X = IsAddCarry(op1, op2, result);

    PrefetchTransfer();
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 6 : 4);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_AddA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 An = bit::extract<9, 11>(instr);

    using ST = std::make_signed_t<T>;

    regs.A[An] += static_cast<ST>(ReadEffectiveAddress<T>(M, Xn));

    PrefetchTransfer();
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 6 : 8);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_AddI(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    T op1 = PrefetchNext();
    if constexpr (sizeof(T) == sizeof(uint32)) {
        op1 = (op1 << 16u) | PrefetchNext();
    }
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 + op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = IsAddOverflow(op1, op2, result);
        SR.C = SR.X = IsAddCarry(op1, op2, result);

        return result;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M == 0 ? 16 : kEffectiveAddressCycles<T>[M][Xn] + 20;
    } else {
        return M == 0 ? 8 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_AddQ_An(uint16 instr) {
    const uint16 An = bit::extract<0, 2>(instr);
    const uint16 data = bit::extract<9, 11>(instr);

    const uint32 op1 = data == 0 ? 8 : data;
    const uint32 op2 = regs.A[An];
    const uint32 result = op2 + op1;
    regs.A[An] = result;

    PrefetchTransfer();
    return 8;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_AddQ_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 data = bit::extract<9, 11>(instr);

    const T op1 = data == 0 ? 8 : data;
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 + op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = IsAddOverflow(op1, op2, result);
        SR.C = SR.X = IsAddCarry(op1, op2, result);

        return result;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M == 0 ? 8 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    } else {
        return M == 0 ? 4 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_AddX_M(uint16 instr) {
    const uint16 Ry = bit::extract<0, 2>(instr);
    const uint16 Rx = bit::extract<9, 11>(instr);

    AdvanceAddress<T, false>(Ry);
    const T op1 = MemReadDesc<T, false>(regs.A[Ry]);
    AdvanceAddress<T, false>(Rx);
    const T op2 = MemReadDesc<T, false>(regs.A[Rx]);
    const T result = op2 + op1 + SR.X;
    SR.N = IsNegative(result);
    SR.Z &= result == 0;
    SR.V = IsAddOverflow(op1, op2, result);
    SR.C = SR.X = IsAddCarry(op1, op2, result);

    if constexpr (std::is_same_v<T, uint32>) {
        MemWrite<uint16>(regs.A[Rx] + 2, result >> 0u);
        PrefetchTransfer();
        MemWrite<uint16>(regs.A[Rx] + 0, result >> 16u);
    } else {
        PrefetchTransfer();
        MemWrite<T>(regs.A[Rx], result);
    }
    return std::is_same_v<T, uint32> ? 30 : 18;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_AddX_R(uint16 instr) {
    const uint16 Ry = bit::extract<0, 2>(instr);
    const uint16 Rx = bit::extract<9, 11>(instr);

    const T op1 = regs.D[Ry];
    const T op2 = regs.D[Rx];
    const T result = op2 + op1 + SR.X;
    SR.N = IsNegative(result);
    SR.Z &= result == 0;
    SR.V = IsAddOverflow(op1, op2, result);
    SR.C = SR.X = IsAddCarry(op1, op2, result);
    bit::deposit_into<0, sizeof(T) * 8 - 1>(regs.D[Rx], result);

    PrefetchTransfer();
    return std::is_same_v<T, uint32> ? 8 : 4;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_And_Dn_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const T op1 = regs.D[Dn];
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 & op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
        return result;
    });
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 12 : 8);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_And_EA_Dn(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const T op1 = ReadEffectiveAddress<T>(M, Xn);
    const T op2 = regs.D[Dn];
    const T result = op2 & op1;
    bit::deposit_into<0, sizeof(T) * 8 - 1>(regs.D[Dn], result);
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = SR.C = 0;

    PrefetchTransfer();
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 6 : 4);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_AndI_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    T op1 = PrefetchNext();
    if constexpr (sizeof(T) == sizeof(uint32)) {
        op1 = (op1 << 16u) | PrefetchNext();
    }
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 & op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
        return result;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M == 0 ? 16 : kEffectiveAddressCycles<T>[M][Xn] + 20;
    } else {
        return M == 0 ? 8 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_AndI_CCR(uint16 instr) {
    const uint8 value = PrefetchNext();
    SR.xflags &= value;

    PC -= 2;
    PrefetchNext();
    PrefetchTransfer();
    return 20;
}

FORCE_INLINE uint64 MC68EC000::Instr_AndI_SR(uint16 instr) {
    if (CheckPrivilege()) {
        const uint16 value = PrefetchNext();
        const uint16 newSR = SR.u16 & value;
        PC -= 2;
        SetSR(newSR);

        PrefetchNext();
        PrefetchTransfer();
        return 20;
    }
    return 34;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Eor_Dn_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const T op1 = regs.D[Dn];
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 ^ op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
        return result;
    });
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 12 : 8);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_EorI_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    T op1 = PrefetchNext();
    if constexpr (sizeof(T) == sizeof(uint32)) {
        op1 = (op1 << 16u) | PrefetchNext();
    }
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 ^ op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
        return result;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M == 0 ? 16 : kEffectiveAddressCycles<T>[M][Xn] + 20;
    } else {
        return M == 0 ? 8 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_EorI_CCR(uint16 instr) {
    const uint8 value = PrefetchNext();
    SR.xflags ^= value;

    PC -= 2;
    PrefetchNext();
    PrefetchTransfer();
    return 20;
}

FORCE_INLINE uint64 MC68EC000::Instr_EorI_SR(uint16 instr) {
    if (CheckPrivilege()) {
        const uint16 value = PrefetchNext();
        const uint16 newSR = SR.u16 ^ value;
        PC -= 2;
        SetSR(newSR);

        PrefetchNext();
        PrefetchTransfer();
        return 20;
    }
    return 34;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Neg(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<T>(M, Xn, [&](T value) {
        const T result = 0 - value;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = static_cast<std::make_signed_t<T>>(value & result) < 0;
        SR.C = SR.X = ~SR.Z;
        return result;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M <= 1 ? 6 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    } else {
        return M <= 1 ? 4 : kEffectiveAddressCycles<T>[M][Xn] + 8;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_NegX(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<T>(M, Xn, [&](T value) {
        const T result = 0 - value - SR.X;
        SR.N = result >> (sizeof(T) * 8 - 1);
        SR.Z &= result == 0;
        SR.V = static_cast<std::make_signed_t<T>>(value & result) < 0;
        SR.X = SR.C = (value | result) >> (sizeof(T) * 8 - 1);
        return result;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M <= 1 ? 6 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    } else {
        return M <= 1 ? 4 : kEffectiveAddressCycles<T>[M][Xn] + 8;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Not(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<T>(M, Xn, [&](T value) {
        const T result = ~value;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
        return result;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M <= 1 ? 6 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    } else {
        return M <= 1 ? 4 : kEffectiveAddressCycles<T>[M][Xn] + 8;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Or_Dn_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const T op1 = regs.D[Dn];
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 | op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
        return result;
    });
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 12 : 8);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Or_EA_Dn(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const T op1 = ReadEffectiveAddress<T>(M, Xn);
    const T op2 = regs.D[Dn];
    const T result = op2 | op1;
    bit::deposit_into<0, sizeof(T) * 8 - 1>(regs.D[Dn], result);
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = SR.C = 0;

    PrefetchTransfer();
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 6 : 4);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_OrI_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    T op1 = PrefetchNext();
    if constexpr (sizeof(T) == sizeof(uint32)) {
        op1 = (op1 << 16u) | PrefetchNext();
    }
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 | op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
        return result;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M == 0 ? 16 : kEffectiveAddressCycles<T>[M][Xn] + 20;
    } else {
        return M == 0 ? 8 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_OrI_CCR(uint16 instr) {
    const uint8 value = PrefetchNext();
    SR.xflags |= value;

    PC -= 2;
    PrefetchNext();
    PrefetchTransfer();
    return 20;
}

FORCE_INLINE uint64 MC68EC000::Instr_OrI_SR(uint16 instr) {
    if (CheckPrivilege()) {
        const uint16 value = PrefetchNext();
        const uint16 newSR = SR.u16 | value;
        PC -= 2;
        SetSR(newSR);

        PrefetchNext();
        PrefetchTransfer();
        return 20;
    }
    return 34;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Sub_Dn_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const T op1 = regs.D[Dn];
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 - op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = IsSubOverflow(op1, op2, result);
        SR.C = SR.X = IsSubCarry(op1, op2, result);
        return result;
    });
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 12 : 8);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Sub_EA_Dn(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const T op1 = ReadEffectiveAddress<T>(M, Xn);
    const T op2 = regs.D[Dn];
    const T result = op2 - op1;
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = IsSubOverflow(op1, op2, result);
    SR.C = SR.X = IsSubCarry(op1, op2, result);

    PrefetchTransfer();
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 6 : 4);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_SubA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 An = bit::extract<9, 11>(instr);

    using ST = std::make_signed_t<T>;

    regs.A[An] -= static_cast<ST>(ReadEffectiveAddress<T>(M, Xn));

    PrefetchTransfer();
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 6 : 8);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_SubI(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    T op1 = PrefetchNext();
    if constexpr (sizeof(T) == sizeof(uint32)) {
        op1 = (op1 << 16u) | PrefetchNext();
    }
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 - op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = IsSubOverflow(op1, op2, result);
        SR.C = SR.X = IsSubCarry(op1, op2, result);

        return result;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M == 0 ? 16 : kEffectiveAddressCycles<T>[M][Xn] + 20;
    } else {
        return M == 0 ? 8 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_SubQ_An(uint16 instr) {
    const uint16 An = bit::extract<0, 2>(instr);
    const uint16 data = bit::extract<9, 11>(instr);

    const uint32 op1 = data == 0 ? 8 : data;
    const uint32 op2 = regs.A[An];
    const uint32 result = op2 - op1;
    regs.A[An] = result;

    PrefetchTransfer();
    return 8;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_SubQ_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 data = bit::extract<9, 11>(instr);

    const T op1 = data == 0 ? 8 : data;
    ModifyEffectiveAddress<T>(M, Xn, [&](T op2) {
        const T result = op2 - op1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = IsSubOverflow(op1, op2, result);
        SR.C = SR.X = IsSubCarry(op1, op2, result);

        return result;
    });
    if constexpr (std::is_same_v<T, uint32>) {
        return M == 0 ? 8 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    } else {
        return M == 0 ? 4 : kEffectiveAddressCycles<T>[M][Xn] + 8;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_SubX_M(uint16 instr) {
    const uint16 Ay = bit::extract<0, 2>(instr);
    const uint16 Ax = bit::extract<9, 11>(instr);

    AdvanceAddress<T, false>(Ay);
    const T op1 = MemReadDesc<T, false>(regs.A[Ay]);
    AdvanceAddress<T, false>(Ax);
    const T op2 = MemReadDesc<T, false>(regs.A[Ax]);
    const T result = op2 - op1 - SR.X;
    SR.N = IsNegative(result);
    SR.Z &= result == 0;
    SR.V = IsSubOverflow(op1, op2, result);
    SR.C = SR.X = IsSubCarry(op1, op2, result);

    if constexpr (std::is_same_v<T, uint32>) {
        MemWrite<uint16>(regs.A[Ax] + 2, result >> 0u);
        PrefetchTransfer();
        MemWrite<uint16>(regs.A[Ax] + 0, result >> 16u);
    } else {
        PrefetchTransfer();
        MemWrite<T>(regs.A[Ax], result);
    }
    return std::is_same_v<T, uint32> ? 30 : 18;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_SubX_R(uint16 instr) {
    const uint16 Dy = bit::extract<0, 2>(instr);
    const uint16 Dx = bit::extract<9, 11>(instr);

    const T op1 = regs.D[Dy];
    const T op2 = regs.D[Dx];
    const T result = op2 - op1 - SR.X;
    SR.N = IsNegative(result);
    SR.Z &= result == 0;
    SR.V = IsSubOverflow(op1, op2, result);
    SR.C = SR.X = IsSubCarry(op1, op2, result);
    bit::deposit_into<0, sizeof(T) * 8 - 1>(regs.D[Dx], result);

    PrefetchTransfer();
    return std::is_same_v<T, uint32> ? 8 : 6;
}

FORCE_INLINE uint64 MC68EC000::Instr_DivS(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    sint32 dividend = regs.D[Dn];
    sint16 divisor = ReadEffectiveAddress<uint16>(M, Xn);

    if (divisor == 0) {
        EnterException(ExceptionVector::ZeroDivide);
        SR.N = 0;
        SR.Z = 0;
        SR.V = 0;
        SR.C = 0;
        return 42;
    }

    SR.Z = 0;
    SR.C = 0;

    const sint32 quotient = dividend / divisor;
    const sint32 remainder = dividend % divisor;

    const uint32 udividend = static_cast<uint32>(abs(dividend));
    const uint32 udivisor = static_cast<uint32>(abs(divisor)) << 16u;

    if (udividend >= udivisor && divisor != -0x8000) {
        SR.N = 1;
        SR.V = 1;
    } else {
        SR.Z = quotient == 0;
        SR.V = quotient != static_cast<sint16>(quotient);
        if (SR.V) {
            SR.N = 1;
        } else {
            regs.D[Dn] = static_cast<uint16>(quotient) | (static_cast<uint16>(remainder) << 16u);
            SR.N = IsNegative<uint16>(quotient);
        }
    }

    PrefetchTransfer();
    return kEffectiveAddressCycles<uint16>[M][Xn] + 158;
}

FORCE_INLINE uint64 MC68EC000::Instr_DivU(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    uint32 dividend = regs.D[Dn];
    uint16 divisor = ReadEffectiveAddress<uint16>(M, Xn);

    if (divisor == 0) {
        EnterException(ExceptionVector::ZeroDivide);
        SR.N = 0;
        SR.Z = 0;
        SR.V = 0;
        SR.C = 0;
        return 42;
    }

    SR.Z = 0;
    SR.C = 0;

    const uint32 quotient = dividend / divisor;
    const uint32 remainder = dividend % divisor;

    SR.Z = quotient == 0;
    SR.V = quotient != static_cast<uint16>(quotient);
    if (SR.V) {
        SR.N = 1;
    } else {
        regs.D[Dn] = static_cast<uint16>(quotient) | (static_cast<uint16>(remainder) << 16u);
        SR.N = IsNegative<uint16>(quotient);
    }

    PrefetchTransfer();
    return kEffectiveAddressCycles<uint16>[M][Xn] + 140;
}

FORCE_INLINE uint64 MC68EC000::Instr_MulS(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const sint16 op1 = ReadEffectiveAddress<uint16>(M, Xn);
    const sint16 op2 = regs.D[Dn];
    const sint32 result = static_cast<sint32>(op2) * op1;
    regs.D[Dn] = result;
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = 0;
    SR.C = 0;

    PrefetchTransfer();
    const uint32 count = std::popcount<uint16>(op1 ^ (op1 << 1u)); // 10 and 01 patterns in op1 << 1
    return kEffectiveAddressCycles<uint16>[M][Xn] + 38 + 2ull * count;
}

FORCE_INLINE uint64 MC68EC000::Instr_MulU(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const uint16 op1 = ReadEffectiveAddress<uint16>(M, Xn);
    const uint16 op2 = regs.D[Dn];
    const uint32 result = static_cast<uint32>(op2) * op1;
    regs.D[Dn] = result;
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = 0;
    SR.C = 0;

    PrefetchTransfer();
    return kEffectiveAddressCycles<uint16>[M][Xn] + 38 + 2ull * std::popcount(op1);
}

FORCE_INLINE uint64 MC68EC000::Instr_BChg_I_Dn(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 index = PrefetchNext() & 31;

    const uint32 bit = 1 << index;
    const uint32 value = regs.D[Dn];
    SR.Z = (value & bit) == 0;
    regs.D[Dn] ^= bit;

    PrefetchTransfer();
    return 12;
}

FORCE_INLINE uint64 MC68EC000::Instr_BChg_I_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 index = PrefetchNext() & 7;

    const uint8 bit = 1 << index;
    ModifyEffectiveAddress<uint8>(M, Xn, [&](uint8 value) {
        SR.Z = (value & bit) == 0;
        return value ^ bit;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 12;
}

FORCE_INLINE uint64 MC68EC000::Instr_BChg_R_Dn(uint16 instr) {
    const uint16 dstDn = bit::extract<0, 2>(instr);
    const uint16 srcDn = bit::extract<9, 11>(instr);
    const uint16 index = regs.D[srcDn] & 31;

    const uint32 bit = 1 << index;
    const uint32 value = regs.D[dstDn];
    SR.Z = (value & bit) == 0;
    regs.D[dstDn] ^= bit;

    PrefetchTransfer();
    return 8;
}

FORCE_INLINE uint64 MC68EC000::Instr_BChg_R_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 srcDn = bit::extract<9, 11>(instr);
    const uint16 index = regs.D[srcDn] & 7;

    const uint8 bit = 1 << index;
    ModifyEffectiveAddress<uint8>(M, Xn, [&](uint8 value) {
        SR.Z = (value & bit) == 0;
        return value ^ bit;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

FORCE_INLINE uint64 MC68EC000::Instr_BClr_I_Dn(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 index = PrefetchNext() & 31;

    const uint32 bit = 1 << index;
    const uint32 value = regs.D[Dn];
    SR.Z = (value & bit) == 0;
    regs.D[Dn] &= ~bit;

    PrefetchTransfer();
    return 14;
}

FORCE_INLINE uint64 MC68EC000::Instr_BClr_I_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 index = PrefetchNext() & 7;

    const uint8 bit = 1 << index;
    ModifyEffectiveAddress<uint8>(M, Xn, [&](uint8 value) {
        SR.Z = (value & bit) == 0;
        return value & ~bit;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 12;
}

FORCE_INLINE uint64 MC68EC000::Instr_BClr_R_Dn(uint16 instr) {
    const uint16 dstDn = bit::extract<0, 2>(instr);
    const uint16 srcDn = bit::extract<9, 11>(instr);
    const uint16 index = regs.D[srcDn] & 31;

    const uint32 bit = 1 << index;
    const uint32 value = regs.D[dstDn];
    SR.Z = (value & bit) == 0;
    regs.D[dstDn] &= ~bit;

    PrefetchTransfer();
    return 10;
}

FORCE_INLINE uint64 MC68EC000::Instr_BClr_R_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 srcDn = bit::extract<9, 11>(instr);
    const uint16 index = regs.D[srcDn] & 7;

    const uint8 bit = 1 << index;
    ModifyEffectiveAddress<uint8>(M, Xn, [&](uint8 value) {
        SR.Z = (value & bit) == 0;
        return value & ~bit;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

FORCE_INLINE uint64 MC68EC000::Instr_BSet_I_Dn(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 index = PrefetchNext() & 31;

    const uint32 bit = 1 << index;
    const uint32 value = regs.D[Dn];
    SR.Z = (value & bit) == 0;
    regs.D[Dn] |= bit;

    PrefetchTransfer();
    return 12;
}

FORCE_INLINE uint64 MC68EC000::Instr_BSet_I_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 index = PrefetchNext() & 7;

    const uint8 bit = 1 << index;
    ModifyEffectiveAddress<uint8>(M, Xn, [&](uint8 value) {
        SR.Z = (value & bit) == 0;
        return value | bit;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 12;
}

FORCE_INLINE uint64 MC68EC000::Instr_BSet_R_Dn(uint16 instr) {
    const uint16 dstDn = bit::extract<0, 2>(instr);
    const uint16 srcDn = bit::extract<9, 11>(instr);
    const uint16 index = regs.D[srcDn] & 31;

    const uint32 bit = 1 << index;
    const uint32 value = regs.D[dstDn];
    SR.Z = (value & bit) == 0;
    regs.D[dstDn] |= bit;

    PrefetchTransfer();
    return 8;
}

FORCE_INLINE uint64 MC68EC000::Instr_BSet_R_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 srcDn = bit::extract<9, 11>(instr);
    const uint16 index = regs.D[srcDn] & 7;

    const uint8 bit = 1 << index;
    ModifyEffectiveAddress<uint8>(M, Xn, [&](uint8 value) {
        SR.Z = (value & bit) == 0;
        return value | bit;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

FORCE_INLINE uint64 MC68EC000::Instr_BTst_I_Dn(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 index = PrefetchNext() & 31;

    const uint32 value = regs.D[Dn];
    SR.Z = !((value >> index) & 1);

    PrefetchTransfer();
    return 10;
}

FORCE_INLINE uint64 MC68EC000::Instr_BTst_I_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 index = PrefetchNext() & 7;

    const uint8 value = ReadEffectiveAddress<uint8>(M, Xn);
    SR.Z = !((value >> index) & 1);

    PrefetchTransfer();
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

FORCE_INLINE uint64 MC68EC000::Instr_BTst_R_Dn(uint16 instr) {
    const uint16 dstDn = bit::extract<0, 2>(instr);
    const uint16 srcDn = bit::extract<9, 11>(instr);
    const uint16 index = regs.D[srcDn] & 31;

    const uint32 value = regs.D[dstDn];
    SR.Z = !((value >> index) & 1);

    PrefetchTransfer();
    return 6;
}

FORCE_INLINE uint64 MC68EC000::Instr_BTst_R_EA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 srcDn = bit::extract<9, 11>(instr);
    const uint16 index = regs.D[srcDn] & 7;

    const uint8 value = ReadEffectiveAddress<uint8>(M, Xn);
    SR.Z = !((value >> index) & 1);

    PrefetchTransfer();
    return kEffectiveAddressCycles<uint8>[M][Xn] + 4;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ASL_I(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    uint32 shift = bit::extract<9, 11>(instr);
    if (shift == 0) {
        shift = 8;
    }

    if (std::is_same_v<T, uint8> && shift == 8) {
        const T value = regs.D[Dn];
        const T result = 0;
        const bool carry = value & 1;
        bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = IsLeftShiftOverflow<T>(value, shift);
        SR.C = SR.X = carry;
    } else {
        const T value = regs.D[Dn];
        const T result = value << shift;
        const bool carry = (value >> (sizeof(T) * 8 - shift)) & 1;
        bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = IsLeftShiftOverflow<T>(value, shift);
        SR.C = SR.X = carry;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_ASL_M(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<uint16>(M, Xn, [&](uint16 value) {
        const uint16 result = value << 1u;
        const bool carry = value >> 15u;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = IsLeftShiftOverflow<uint16>(value, 1u);
        SR.C = SR.X = carry;
        return result;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ASL_R(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 shiftReg = bit::extract<9, 11>(instr);
    const uint32 shift = regs.D[shiftReg] & 63;

    const T value = regs.D[Dn];
    T result;
    bool carry;
    if (shift > sizeof(T) * 8) {
        result = 0;
        carry = false;
    } else if (shift == sizeof(T) * 8) {
        result = 0;
        carry = value & 1;
    } else if (shift != 0) {
        result = value << shift;
        carry = (value >> (sizeof(T) * 8 - shift)) & 1;
    } else {
        result = value;
        carry = false;
    }
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    if (shift != 0) {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = IsLeftShiftOverflow<T>(value, shift);
        SR.C = SR.X = carry;
    } else {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ASR_I(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    uint32 shift = bit::extract<9, 11>(instr);
    if (shift == 0) {
        shift = 8;
    }

    using ST = std::make_signed_t<T>;

    if (std::is_same_v<T, uint8> && shift == 8) {
        const ST value = regs.D[Dn];
        const ST result = value >> 7;
        const bool carry = value >> 7;
        bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;
    } else {
        const ST value = regs.D[Dn];
        const ST result = value >> shift;
        const bool carry = (value >> (shift - 1)) & 1;
        bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_ASR_M(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<uint16>(M, Xn, [&](uint16 value) {
        const uint16 result = static_cast<sint16>(value) >> 1u;
        const bool carry = value & 1u;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;

        return result;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ASR_R(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 shiftReg = bit::extract<9, 11>(instr);
    const uint32 shift = regs.D[shiftReg] & 63;

    using ST = std::make_signed_t<T>;

    const ST value = regs.D[Dn];
    ST result;
    bool carry;
    if (shift >= sizeof(T) * 8) {
        result = value >> (sizeof(T) * 8 - 1);
        carry = value >> (sizeof(T) * 8 - 1);
    } else if (shift != 0) {
        result = value >> shift;
        carry = (value >> (shift - 1)) & 1;
    } else {
        result = value;
        carry = false;
    }
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    if (shift != 0) {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;
    } else {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_LSL_I(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    uint32 shift = bit::extract<9, 11>(instr);
    if (shift == 0) {
        shift = 8;
    }

    if (std::is_same_v<T, uint8> && shift == 8) {
        const T value = regs.D[Dn];
        const T result = 0;
        const bool carry = value & 1;
        bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;

    } else {
        const T value = regs.D[Dn];
        const T result = value << shift;
        const bool carry = (value >> (sizeof(T) * 8 - shift)) & 1;
        bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_LSL_M(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<uint16>(M, Xn, [&](uint16 value) {
        const uint16 result = value << 1u;
        const bool carry = value >> 15u;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;

        return result;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_LSL_R(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 shiftReg = bit::extract<9, 11>(instr);
    const uint32 shift = regs.D[shiftReg] & 63;

    const T value = regs.D[Dn];
    T result;
    bool carry;
    if (shift > sizeof(T) * 8) {
        result = 0;
        carry = false;
    } else if (shift == sizeof(T) * 8) {
        result = 0;
        carry = value & 1;
    } else if (shift != 0) {
        result = value << shift;
        carry = (value >> (sizeof(T) * 8 - shift)) & 1;
    } else {
        result = value;
        carry = false;
    }
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    if (shift != 0) {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;
    } else {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_LSR_I(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    uint32 shift = bit::extract<9, 11>(instr);
    if (shift == 0) {
        shift = 8;
    }

    if (std::is_same_v<T, uint8> && shift == 8) {
        const T value = regs.D[Dn];
        const T result = 0;
        const bool carry = value >> 7;
        bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;

    } else {
        const T value = regs.D[Dn];
        const T result = value >> shift;
        const bool carry = (value >> (shift - 1)) & 1;
        bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_LSR_M(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<uint16>(M, Xn, [&](uint16 value) {
        const uint16 result = value >> 1u;
        const bool carry = value & 1u;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;

        return result;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_LSR_R(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 shiftReg = bit::extract<9, 11>(instr);
    const uint32 shift = regs.D[shiftReg] & 63;

    const T value = regs.D[Dn];
    T result;
    bool carry;
    if (shift > sizeof(T) * 8) {
        result = 0;
        carry = false;
    } else if (shift == sizeof(T) * 8) {
        result = 0;
        carry = value >> (sizeof(T) * 8 - 1);
    } else if (shift != 0) {
        result = value >> shift;
        carry = (value >> (shift - 1)) & 1;
    } else {
        result = value;
        carry = false;
    }
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    if (shift != 0) {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;
    } else {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ROL_I(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    uint32 shift = bit::extract<9, 11>(instr);
    if (shift == 0) {
        shift = 8;
    }

    const T value = regs.D[Dn];
    const T result = std::rotl(value, shift);
    const bool carry = result & 1;
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = 0;
    SR.C = carry;

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_ROL_M(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<uint16>(M, Xn, [&](uint16 value) {
        const uint16 result = std::rotl(value, 1u);
        const bool carry = result & 1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = carry;

        return result;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ROL_R(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 shiftReg = bit::extract<9, 11>(instr);
    const uint32 shift = regs.D[shiftReg] & 63;

    const T value = regs.D[Dn];
    const T result = std::rotl(value, shift);
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    if (shift != 0) {
        const bool carry = result & 1;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = carry;
    } else {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ROR_I(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    uint32 shift = bit::extract<9, 11>(instr);
    if (shift == 0) {
        shift = 8;
    }

    const T value = regs.D[Dn];
    const T result = std::rotr(value, shift);
    const bool carry = result >> (sizeof(T) * 8 - 1);
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = 0;
    SR.C = carry;

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_ROR_M(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<uint16>(M, Xn, [&](uint16 value) {
        const uint16 result = std::rotr(value, 1u);
        const bool carry = result >> 15;
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = carry;

        return result;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ROR_R(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 shiftReg = bit::extract<9, 11>(instr);
    const uint32 shift = regs.D[shiftReg] & 63;

    const T value = regs.D[Dn];
    const T result = std::rotr(value, shift);
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    if (shift != 0) {
        const bool carry = result >> (sizeof(T) * 8 - 1);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = carry;
    } else {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = SR.C = 0;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ROXL_I(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    uint32 shift = bit::extract<9, 11>(instr);
    if (shift == 0) {
        shift = 8;
    }

    const T value = regs.D[Dn];
    auto [result, carry] = roxl<T>(value, shift, SR.X);
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = 0;
    SR.C = SR.X = carry;

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_ROXL_M(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<uint16>(M, Xn, [&](uint16 value) {
        auto [result, carry] = roxl<uint16>(value, 1u, SR.X);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;

        return result;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ROXL_R(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 shiftReg = bit::extract<9, 11>(instr);
    const uint32 shift = regs.D[shiftReg] & 63;

    const T value = regs.D[Dn];
    auto [result, carry] = roxl<T>(value, shift, SR.X);
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    if (shift != 0) {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;
    } else {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ROXR_I(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    uint32 shift = bit::extract<9, 11>(instr);
    if (shift == 0) {
        shift = 8;
    }

    const T value = regs.D[Dn];
    auto [result, carry] = roxr<T>(value, shift, SR.X);
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = 0;
    SR.C = SR.X = carry;

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_ROXR_M(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    ModifyEffectiveAddress<uint16>(M, Xn, [&](uint16 value) {
        auto [result, carry] = roxr<uint16>(value, 1u, SR.X);
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;

        return result;
    });
    return kEffectiveAddressCycles<uint8>[M][Xn] + 8;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_ROXR_R(uint16 instr) {
    const uint16 Dn = bit::extract<0, 2>(instr);
    const uint16 shiftReg = bit::extract<9, 11>(instr);
    const uint32 shift = regs.D[shiftReg] & 63;

    const T value = regs.D[Dn];
    auto [result, carry] = roxr<T>(value, shift, SR.X);
    bit::deposit_into<0, sizeof(T) * 8 - 1, uint32>(regs.D[Dn], result);
    if (shift != 0) {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X = carry;
    } else {
        SR.N = IsNegative(result);
        SR.Z = result == 0;
        SR.V = 0;
        SR.C = SR.X;
    }

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return 2ull * shift + 8;
    } else {
        return 2ull * shift + 6;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Cmp(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const T op1 = ReadEffectiveAddress<T>(M, Xn);
    const T op2 = regs.D[Dn];
    const T result = op2 - op1;
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = IsSubOverflow(op1, op2, result);
    SR.C = IsSubCarry(op1, op2, result);

    PrefetchTransfer();
    return kEffectiveAddressCycles<T>[M][Xn] + (std::is_same_v<T, uint32> ? 6 : 4);
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_CmpA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 An = bit::extract<9, 11>(instr);

    const uint32 op1 = static_cast<std::make_signed_t<T>>(ReadEffectiveAddress<T>(M, Xn));
    const uint32 op2 = regs.A[An];
    const uint32 result = op2 - op1;
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = IsSubOverflow(op1, op2, result);
    SR.C = IsSubCarry(op1, op2, result);

    PrefetchTransfer();
    return kEffectiveAddressCycles<T>[M][Xn] + 6;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_CmpI(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    T op1 = PrefetchNext();
    if constexpr (sizeof(T) == sizeof(uint32)) {
        op1 = (op1 << 16u) | PrefetchNext();
    }
    const T op2 = ReadEffectiveAddress<T>(M, Xn);
    const T result = op2 - op1;
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = IsSubOverflow(op1, op2, result);
    SR.C = IsSubCarry(op1, op2, result);

    PrefetchTransfer();
    if constexpr (std::is_same_v<T, uint32>) {
        return M == 0 ? 14 : kEffectiveAddressCycles<T>[M][Xn] + 12;
    } else {
        return M == 0 ? 8 : kEffectiveAddressCycles<T>[M][Xn] + 8;
    }
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_CmpM(uint16 instr) {
    const uint16 Ay = bit::extract<0, 2>(instr);
    const uint16 Ax = bit::extract<9, 11>(instr);

    const T op1 = MemRead<T, false>(regs.A[Ay]);
    AdvanceAddress<T, true>(Ay);
    const T op2 = MemRead<T, false>(regs.A[Ax]);
    AdvanceAddress<T, true>(Ax);
    const T result = op2 - op1;
    SR.N = IsNegative(result);
    SR.Z = result == 0;
    SR.V = IsSubOverflow(op1, op2, result);
    SR.C = IsSubCarry(op1, op2, result);

    PrefetchTransfer();
    return std::is_same_v<T, uint32> ? 20 : 12;
}

FORCE_INLINE uint64 MC68EC000::Instr_Scc(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint32 cond = bit::extract<8, 11>(instr);

    const uint8 value = kCondTable[(cond << 4u) | SR.flags] * 0xFF;
    ModifyEffectiveAddress<uint8>(M, Xn, [&](uint8) { return value; });
    if (value) {
        return M <= 1 ? 6 : kEffectiveAddressCycles<uint8>[M][Xn] + 8;
    } else {
        return M <= 1 ? 4 : kEffectiveAddressCycles<uint8>[M][Xn] + 8;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_TAS(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    // NOTE: this should be indivisible
    ModifyEffectiveAddress<uint8, false>(M, Xn, [&](uint8 value) {
        SR.N = IsNegative(value);
        SR.Z = value == 0;
        SR.V = SR.C = 0;
        return value | 0x80;
    });

    PrefetchTransfer();
    return M <= 1 ? 4 : kEffectiveAddressCycles<uint8>[M][Xn] + 10;
}

template <mem_primitive T>
FORCE_INLINE uint64 MC68EC000::Instr_Tst(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    const T value = ReadEffectiveAddress<T>(M, Xn);
    SR.N = IsNegative(value);
    SR.Z = value == 0;
    SR.V = SR.C = 0;

    PrefetchTransfer();
    return M <= 1 ? 4 : kEffectiveAddressCycles<T>[M][Xn] + 4;
}

FORCE_INLINE uint64 MC68EC000::Instr_LEA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 An = bit::extract<9, 11>(instr);

    regs.A[An] = CalcEffectiveAddress(M, Xn);

    PrefetchTransfer();

    static constexpr auto kCycles = [] {
        std::array<std::array<uint64, 8>, 8> arr{};
        for (uint8 M = 0; M < 8; ++M) {
            for (uint8 Xn = 0; Xn < 8; ++Xn) {
                switch (M) {
                case 0b010: arr[M][Xn] = 4; break;  // (An)
                case 0b101: arr[M][Xn] = 8; break;  // d(An)
                case 0b110: arr[M][Xn] = 12; break; // d(An,ix)
                case 0b111:
                    switch (Xn) {
                    case 0b010: arr[M][Xn] = 8; break;  // d(PC)
                    case 0b011: arr[M][Xn] = 12; break; // d(PC,ix)
                    case 0b000: arr[M][Xn] = 8; break;  // xxx.W
                    case 0b001: arr[M][Xn] = 12; break; // xxx.L
                    }
                    break;
                }
            }
        }
        return arr;
    }();

    return kCycles[M][Xn];
}

FORCE_INLINE uint64 MC68EC000::Instr_PEA(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    const uint32 address = CalcEffectiveAddress(M, Xn);
    AdvanceAddress<uint32, false>(7);
    if (M == 7 && Xn <= 1) {
        MemWriteAsc<uint32>(regs.SP, address);
        PrefetchTransfer();
    } else {
        PrefetchTransfer();
        MemWriteAsc<uint32>(regs.SP, address);
    }

    static constexpr auto kCycles = [] {
        std::array<std::array<uint64, 8>, 8> arr{};
        for (uint8 M = 0; M < 8; ++M) {
            for (uint8 Xn = 0; Xn < 8; ++Xn) {
                switch (M) {
                case 0b010: arr[M][Xn] = 12; break; // (An)
                case 0b101: arr[M][Xn] = 16; break; // d(An)
                case 0b110: arr[M][Xn] = 20; break; // d(An,ix)
                case 0b111:
                    switch (Xn) {
                    case 0b010: arr[M][Xn] = 16; break; // d(PC)
                    case 0b011: arr[M][Xn] = 20; break; // d(PC,ix)
                    case 0b000: arr[M][Xn] = 16; break; // xxx.W
                    case 0b001: arr[M][Xn] = 20; break; // xxx.L
                    }
                    break;
                }
            }
        }
        return arr;
    }();

    return kCycles[M][Xn];
}

FORCE_INLINE uint64 MC68EC000::Instr_Link(uint16 instr) {
    const uint16 An = bit::extract<0, 2>(instr);
    const sint16 disp = PrefetchNext();

    MemWriteAsc<uint32>(regs.SP - 4, regs.A[An]);
    regs.SP -= 4;
    regs.A[An] = regs.SP;
    regs.SP += disp;

    PrefetchTransfer();
    return 16;
}

FORCE_INLINE uint64 MC68EC000::Instr_Unlink(uint16 instr) {
    const uint16 An = bit::extract<0, 2>(instr);

    regs.SP = regs.A[An];
    regs.A[An] = MemRead<uint32, false>(regs.SP);
    if (An != 7) {
        regs.SP += 4;
    }

    PrefetchTransfer();
    return 12;
}

FORCE_INLINE uint64 MC68EC000::Instr_BRA(uint16 instr) {
    const uint32 currPC = PC - 2;
    sint16 disp = static_cast<sint8>(bit::extract<0, 7>(instr));
    if (disp == 0x00) {
        disp = static_cast<sint16>(PrefetchNext());
    }
    PC = currPC + disp;
    FullPrefetch();
    return 10;
}

FORCE_INLINE uint64 MC68EC000::Instr_BSR(uint16 instr) {
    const uint32 currPC = PC - 2;
    sint16 disp = static_cast<sint8>(bit::extract<0, 7>(instr));
    const bool longDisp = disp == 0;
    if (longDisp) {
        disp = static_cast<sint16>(m_prefetchQueue[0]);
        PC += 2;
    }

    regs.SP -= 4;
    MemWrite<uint16>(regs.SP + 0, (PC - 2) >> 16u);
    MemWrite<uint16>(regs.SP + 2, (PC - 2) >> 0u);
    PC = currPC + disp;
    FullPrefetch();
    return 18;
}

FORCE_INLINE uint64 MC68EC000::Instr_Bcc(uint16 instr) {
    const uint32 currPC = PC - 2;
    sint16 disp = static_cast<sint8>(bit::extract<0, 7>(instr));
    const bool longDisp = disp == 0;
    if (longDisp) {
        disp = static_cast<sint16>(m_prefetchQueue[0]);
    }
    const uint32 cond = bit::extract<8, 11>(instr);
    if (kCondTable[(cond << 4u) | SR.flags]) {
        PC = currPC + disp;
        FullPrefetch();
        return 10;
    } else if (longDisp) {
        PrefetchNext();
    }

    PrefetchTransfer();
    return longDisp ? 12 : 8;
}

FORCE_INLINE uint64 MC68EC000::Instr_DBcc(uint16 instr) {
    const uint32 currPC = PC - 2;
    const uint32 Dn = bit::extract<0, 2>(instr);
    const uint32 cond = bit::extract<8, 11>(instr);
    const sint16 disp = static_cast<sint16>(m_prefetchQueue[0]);
    uint64 cycles = 12;

    if (!kCondTable[(cond << 4u) | SR.flags]) {
        const uint16 value = regs.D[Dn] - 1;
        regs.D[Dn] = (regs.D[Dn] & 0xFFFF0000) | value;
        if (value != 0xFFFFu) {
            PC = currPC + disp;
            cycles = 10;
        } else {
            cycles = 14;
        }
    }

    FullPrefetch();
    return cycles;
}

FORCE_INLINE uint64 MC68EC000::Instr_JSR(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    const uint32 target = CalcEffectiveAddress<false>(M, Xn);
    const uint32 currPC = M == 2 ? PC - 2 : PC;

    regs.SP -= 4;
    PC = target;
    PrefetchNext();
    MemWrite<uint16>(regs.SP + 0, currPC >> 16u);
    MemWrite<uint16>(regs.SP + 2, currPC >> 0u);
    PrefetchTransfer();

    static constexpr auto kCycles = [] {
        std::array<std::array<uint64, 8>, 8> arr{};
        for (uint8 M = 0; M < 8; ++M) {
            for (uint8 Xn = 0; Xn < 8; ++Xn) {
                switch (M) {
                case 0b010: arr[M][Xn] = 16; break; // (An)
                case 0b101: arr[M][Xn] = 18; break; // d(An)
                case 0b110: arr[M][Xn] = 22; break; // d(An,ix)
                case 0b111:
                    switch (Xn) {
                    case 0b010: arr[M][Xn] = 18; break; // d(PC)
                    case 0b011: arr[M][Xn] = 22; break; // d(PC,ix)
                    case 0b000: arr[M][Xn] = 18; break; // xxx.W
                    case 0b001: arr[M][Xn] = 20; break; // xxx.L
                    }
                    break;
                }
            }
        }
        return arr;
    }();

    return kCycles[M][Xn];
}

FORCE_INLINE uint64 MC68EC000::Instr_Jmp(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);

    const uint32 target = CalcEffectiveAddress<false>(M, Xn);
    PC = target;
    FullPrefetch();

    static constexpr auto kCycles = [] {
        std::array<std::array<uint64, 8>, 8> arr{};
        for (uint8 M = 0; M < 8; ++M) {
            for (uint8 Xn = 0; Xn < 8; ++Xn) {
                switch (M) {
                case 0b010: arr[M][Xn] = 8; break;  // (An)
                case 0b101: arr[M][Xn] = 10; break; // d(An)
                case 0b110: arr[M][Xn] = 14; break; // d(An,ix)
                case 0b111:
                    switch (Xn) {
                    case 0b010: arr[M][Xn] = 10; break; // d(PC)
                    case 0b011: arr[M][Xn] = 14; break; // d(PC,ix)
                    case 0b000: arr[M][Xn] = 10; break; // xxx.W
                    case 0b001: arr[M][Xn] = 12; break; // xxx.L
                    }
                    break;
                }
            }
        }
        return arr;
    }();

    return kCycles[M][Xn];
}

FORCE_INLINE uint64 MC68EC000::Instr_RTE(uint16 instr) {
    if (CheckPrivilege()) {
        uint32 SP = regs.SP;
        SetSR(MemRead<uint16, false>(SP));
        SP += 2;
        PC = MemRead<uint32, false>(SP);
        FullPrefetch();
        SP += 4;
        SetSSP(SP);
        devlog::trace<grp::exec>("Returning from interrupt handler, PC = {:X}", PC);
        return 20;
    }
    return 34;
}

FORCE_INLINE uint64 MC68EC000::Instr_RTR(uint16 instr) {
    SR.xflags = MemRead<uint16, false>(regs.SP);
    regs.SP += 2;
    PC = MemRead<uint32, false>(regs.SP);
    FullPrefetch();
    regs.SP += 4;
    return 20;
}

FORCE_INLINE uint64 MC68EC000::Instr_RTS(uint16 instr) {
    PC = MemRead<uint32, false>(regs.SP);
    FullPrefetch();
    regs.SP += 4;
    return 16;
}

FORCE_INLINE uint64 MC68EC000::Instr_Chk(uint16 instr) {
    const uint16 Xn = bit::extract<0, 2>(instr);
    const uint16 M = bit::extract<3, 5>(instr);
    const uint16 Dn = bit::extract<9, 11>(instr);

    const sint16 upperBound = ReadEffectiveAddress<uint16>(M, Xn);
    const sint16 value = regs.D[Dn];
    SR.Z = value == 0; // undocumented
    SR.V = 0;          // undefined
    SR.C = 0;          // undefined
    if (value < 0 || value > upperBound) {
        SR.N = (value < 0);
        PC -= 2;
        EnterException(ExceptionVector::CHKInstruction);
        return 44;
    } else {
        SR.N = 0;
        PrefetchTransfer();
        return kEffectiveAddressCycles<uint16>[M][Xn] + 10;
    }
}

FORCE_INLINE uint64 MC68EC000::Instr_Reset(uint16 instr) {
    if (CheckPrivilege()) {
        // TODO: assert RESET signal, causing all external devices to be reset
        Reset(false);
        // PrefetchTransfer();
        return 132;
    }
    return 34;
}

FORCE_INLINE uint64 MC68EC000::Instr_Stop(uint16 instr) {
    if (CheckPrivilege()) {
        SetSR(m_prefetchQueue[0]);
        // TODO: stop CPU; should resume when a trace, interrupt or reset exception occurs
        return 4;
    }
    return 34;
}

FORCE_INLINE uint64 MC68EC000::Instr_Trap(uint16 instr) {
    PC -= 2;
    const uint8 vector = bit::extract<0, 3>(instr);
    EnterException(static_cast<ExceptionVector>(0x20 + vector));
    return 38;
}

FORCE_INLINE uint64 MC68EC000::Instr_TrapV(uint16 instr) {
    if (SR.V) {
        PrefetchNext();
        PC -= 4;
        EnterException(ExceptionVector::TRAPVInstruction);
        return 34;
    }

    PrefetchTransfer();
    return 4;
}

FORCE_INLINE uint64 MC68EC000::Instr_Noop(uint16 instr) {
    // TODO: synchronize integer pipeline

    PrefetchTransfer();
    return 4;
}

FORCE_INLINE uint64 MC68EC000::Instr_Illegal(uint16 instr) {
    EnterException(ExceptionVector::IllegalInstruction);
    return 34;
}

FORCE_INLINE uint64 MC68EC000::Instr_Illegal1010(uint16 instr) {
    PC -= 4;
    EnterException(ExceptionVector::Line1010Emulator);
    return 34;
}

FORCE_INLINE uint64 MC68EC000::Instr_Illegal1111(uint16 instr) {
    PC -= 4;
    EnterException(ExceptionVector::Line1111Emulator);
    return 34;
}

} // namespace ymir::m68k
