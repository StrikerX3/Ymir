#pragma once

#include <ymir/core/types.hpp>

namespace ymir::sh1 {

// MACH and MACL
union RegMAC {
    uint64 u64;
    struct {
        uint32 L;
        uint32 H;
    };
};

union RegSR {
    uint32 u32;
    struct {
        uint32 T : 1;      //   0  Test flag
        uint32 S : 1;      //   1  Saturate - Used by multiply/accumulate
        uint32 : 2;        // 2-3  (reserved, must be zero)
        uint32 ILevel : 4; // 4-7  Interrupt mask
        uint32 Q : 1;      //   8  Quotient - Used by DIV0U/S and DIV1
        uint32 M : 1;      //   9  Modulus - Used by DIV0U/S and DIV1
                           // (remaining bits are reserved and must be zero)
    };
};

} // namespace ymir::sh1
