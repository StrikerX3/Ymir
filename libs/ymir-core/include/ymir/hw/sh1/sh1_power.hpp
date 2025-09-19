#pragma once

#include <ymir/core/types.hpp>

namespace ymir::sh1 {

// addr r/w  access   init      code    name
// 1BC  R/W  8,16,32  1F        SBYCR   Standby Control Register
//
//   bits   r/w  code   description
//      7   R/W  SBY    Standby (0=SLEEP -> sleep mode, 1=SLEEP -> standby mode)
//      6   R/W  HIZ    Port High Impedance (0=port state retained in standby mode, 1=ports go to high impedance)
//      5   R    -      Reserved - must be zero
//    4-0   R    -      Reserved - must be one
union RegSBYCR {
    uint8 u8 = 0x1F;
    struct {
        uint8 : 6;
        uint8 HIZ : 1;
        uint8 SBY : 1;
    };
};

} // namespace ymir::sh1
