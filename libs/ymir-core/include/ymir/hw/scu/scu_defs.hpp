#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/data_ops.hpp>

namespace ymir::scu {

// SCU interrupts
union InterruptStatus {
    uint32 u32;
    struct {
        uint16 internal;
        uint16 external;
    };
    struct {                           //  bit  vec   lvl  source  reason
        uint32 VDP2_VBlankIN : 1;      //    0   40     F  VDP2    VBlank IN
        uint32 VDP2_VBlankOUT : 1;     //    1   41     E  VDP2    VBlank OUT
        uint32 VDP2_HBlankIN : 1;      //    2   42     D  VDP2    HBlank IN
        uint32 SCU_Timer0 : 1;         //    3   43     C  SCU     Timer 0
        uint32 SCU_Timer1 : 1;         //    4   44     B  SCU     Timer 1
        uint32 SCU_DSPEnd : 1;         //    5   45     A  SCU     DSP End
        uint32 SCSP_SoundRequest : 1;  //    6   46     9  SCSP    Sound Request
        uint32 SMPC_SystemManager : 1; //    7   47     8  SMPC    System Manager
        uint32 SMPC_Pad : 1;           //    8   48     8  SMPC    PAD Interrupt
        uint32 SCU_Level2DMAEnd : 1;   //    9   49     6  SCU     Level 2 DMA End
        uint32 SCU_Level1DMAEnd : 1;   //   10   4A     6  SCU     Level 1 DMA End
        uint32 SCU_Level0DMAEnd : 1;   //   11   4B     5  SCU     Level 0 DMA End
        uint32 SCU_DMAIllegal : 1;     //   12   4C     3  SCU     DMA-illegal
        uint32 VDP1_SpriteDrawEnd : 1; //   13   4D     2  VDP1    Sprite Draw End
        uint32 _rsvd14 : 1;            //   14   -
        uint32 _rsvd15 : 1;            //   15   -
        uint32 ABus_ExtIntr0 : 1;      //   16   50     7  A-Bus   External Interrupt 00
        uint32 ABus_ExtIntr1 : 1;      //   17   51     7  A-Bus   External Interrupt 01
        uint32 ABus_ExtIntr2 : 1;      //   18   52     7  A-Bus   External Interrupt 02
        uint32 ABus_ExtIntr3 : 1;      //   19   53     7  A-Bus   External Interrupt 03
        uint32 ABus_ExtIntr4 : 1;      //   20   54     4  A-Bus   External Interrupt 04
        uint32 ABus_ExtIntr5 : 1;      //   21   55     4  A-Bus   External Interrupt 05
        uint32 ABus_ExtIntr6 : 1;      //   22   56     4  A-Bus   External Interrupt 06
        uint32 ABus_ExtIntr7 : 1;      //   23   57     4  A-Bus   External Interrupt 07
        uint32 ABus_ExtIntr8 : 1;      //   24   58     1  A-Bus   External Interrupt 08
        uint32 ABus_ExtIntr9 : 1;      //   25   59     1  A-Bus   External Interrupt 09
        uint32 ABus_ExtIntrA : 1;      //   26   5A     1  A-Bus   External Interrupt 0A
        uint32 ABus_ExtIntrB : 1;      //   27   5B     1  A-Bus   External Interrupt 0B
        uint32 ABus_ExtIntrC : 1;      //   28   5C     1  A-Bus   External Interrupt 0C
        uint32 ABus_ExtIntrD : 1;      //   29   5D     1  A-Bus   External Interrupt 0D
        uint32 ABus_ExtIntrE : 1;      //   30   5E     1  A-Bus   External Interrupt 0E
        uint32 ABus_ExtIntrF : 1;      //   31   5F     1  A-Bus   External Interrupt 0F
    };
    struct {
        uint32 : 16;
        uint32 ABus_ExtIntrs : 16;
    };
};
static_assert(sizeof(InterruptStatus) == sizeof(uint32));

union InterruptMask {
    uint32 u32;
    uint16 internal : 15; // excludes A-Bus external interrupts bit
    struct {
        uint32 VDP2_VBlankIN : 1;
        uint32 VDP2_VBlankOUT : 1;
        uint32 VDP2_HBlankIN : 1;
        uint32 SCU_Timer0 : 1;
        uint32 SCU_Timer1 : 1;
        uint32 SCU_DSPEnd : 1;
        uint32 SCSP_SoundRequest : 1;
        uint32 SMPC_SystemManager : 1;
        uint32 SMPC_Pad : 1;
        uint32 SCU_Level2DMAEnd : 1;
        uint32 SCU_Level1DMAEnd : 1;
        uint32 SCU_Level0DMAEnd : 1;
        uint32 SCU_DMAIllegal : 1;
        uint32 VDP1_SpriteDrawEnd : 1;
        uint32 _rsvd14 : 1;
        uint32 ABus_ExtIntrs : 1;
    };
};
static_assert(sizeof(InterruptMask) == sizeof(uint32));

enum class BusID {
    ABus,
    BBus,
    WRAM,
    None,
};

inline BusID GetBusID(uint32 address) {
    address &= 0x7FF'FFFF;
    /**/ if (util::AddressInRange<0x200'0000, 0x58F'FFFF>(address)) {
        return BusID::ABus;
    } else if (util::AddressInRange<0x5A0'0000, 0x5FB'FFFF>(address)) {
        return BusID::BBus;
    } else if (address >= 0x600'0000) {
        return BusID::WRAM;
    } else {
        return BusID::None;
    }
}

// Number of cycles to delay immediate DMA transfer end interrupt signals
inline constexpr uint64 kImmediateDMAIntrDelay = 48;

} // namespace ymir::scu
