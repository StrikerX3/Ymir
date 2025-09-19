#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

#include <array>

namespace ymir::sh1 {

struct TimingPatternController {
    TimingPatternController() {
        Reset();
    }

    void Reset() {
        nonOverlapModes.fill(false);
        ituChanSelect.fill(3);
        ituChanGroupASame = true;
        ituChanGroupBSame = true;
        NDER.u16 = 0x0000;
        NDR.u16 = 0x0000;
    }

    // 1F0  R/W  8,16     F0        TPMR    TPC output mode register
    //
    //   bits   r/w  code       description
    //    7-4   -    -          Reserved - must be one
    //      3   R/W  G3NOV      Group 3 Non-Overlap Mode (0=overlap, 1=non-overlap)
    //      2   R/W  G2NOV      Group 2 Non-Overlap Mode (0=overlap, 1=non-overlap)
    //      1   R/W  G1NOV      Group 1 Non-Overlap Mode (0=overlap, 1=non-overlap)
    //      0   R/W  G0NOV      Group 0 Non-Overlap Mode (0=overlap, 1=non-overlap)
    //
    // Groups in non-overlap mode have the output value updated according to compare match A of the ITU channel selected
    // by TPCR.
    // Groups in overlap mode allow 1 output and 0 output to be performed independently according to compare match A and
    // B of the ITU channel selected by TPCR.

    FORCE_INLINE uint8 ReadTPMR() const {
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, 0b1111);
        bit::deposit_into<3>(value, nonOverlapModes[3]);
        bit::deposit_into<2>(value, nonOverlapModes[2]);
        bit::deposit_into<1>(value, nonOverlapModes[1]);
        bit::deposit_into<0>(value, nonOverlapModes[0]);
        return value;
    }

    FORCE_INLINE void WriteTPMR(uint8 value) {
        nonOverlapModes[3] = bit::test<3>(value);
        nonOverlapModes[2] = bit::test<2>(value);
        nonOverlapModes[1] = bit::test<1>(value);
        nonOverlapModes[0] = bit::test<0>(value);
    }

    // 1F1  R/W  8,16     FF        TPCR    TPC output control register
    //
    //   bits   r/w  code       description
    //    7-6   R/W  G3CMS1-0   Group 3 Compare Match Select
    //    5-4   R/W  G2CMS1-0   Group 2 Compare Match Select
    //    3-2   R/W  G1CMS1-0   Group 1 Compare Match Select
    //    1-0   R/W  G0CMS1-0   Group 0 Compare Match Select
    //
    // The values select the ITU channel from with the compare match will trigger a TPC output on the respective group.
    // TPC group 3 is TP15-TP12
    // TPC group 2 is TP11-TP8
    // TPC group 1 is TP7-TP4
    // TPC group 0 is TP3-TP0

    FORCE_INLINE uint8 ReadTPCR() const {
        uint8 value = 0;
        bit::deposit_into<6, 7>(value, ituChanSelect[3]);
        bit::deposit_into<4, 5>(value, ituChanSelect[2]);
        bit::deposit_into<2, 3>(value, ituChanSelect[1]);
        bit::deposit_into<0, 1>(value, ituChanSelect[0]);
        return value;
    }

    FORCE_INLINE void WriteTPCR(uint8 value) {
        ituChanSelect[3] = bit::extract<6, 7>(value);
        ituChanSelect[2] = bit::extract<4, 5>(value);
        ituChanSelect[1] = bit::extract<2, 3>(value);
        ituChanSelect[0] = bit::extract<0, 1>(value);
        ituChanGroupASame = ituChanSelect[0] == ituChanSelect[1];
        ituChanGroupBSame = ituChanSelect[2] == ituChanSelect[3];
    }

    // 1F2  R/W  8,16     00        NDERB   Next data enable register B
    //
    //   bits   r/w  code       description
    //    7-0   R/W  NDER15-8   TPC output enable for TP15-TP8 (0=disabled, 1=enabled)
    //
    // 1F3  R/W  8,16     00        NDERA   Next data enable register A
    //
    //   bits   r/w  code       description
    //    7-0   R/W  NDER7-0    TPC output enable for TP7-TP0 (0=disabled, 1=enabled)
    //
    // Each bit is handled separately.
    // When enabled, NDR15-0 is transferred to PB15-PB0.
    union RegNDER {
        uint16 u16;
        struct {
            uint8 A, B;
        };
    };

    FORCE_INLINE uint8 ReadNDERB() const {
        return NDER.B;
    }

    FORCE_INLINE void WriteNDERB(uint8 value) {
        NDER.B = value;
    }

    FORCE_INLINE uint8 ReadNDERA() const {
        return NDER.A;
    }

    FORCE_INLINE void WriteNDERA(uint8 value) {
        NDER.A = value;
    }

    // 1F4  R/W  8,16     00        NDRB    Next data register B (group 3)
    // 1F6  R/W  8,16     00        NDRB    Next data register B (group 2)
    //
    //   bits   r/w  code       description
    //    7-4   R/W  NDR7-4     Next output data for TPC output group 3
    //    3-0   R/W  NDR3-0     Next output data for TPC output group 2
    //
    // 1F5  R/W  8,16     00        NDRA    Next data register A (group 1)
    // 1F7  R/W  8,16     00        NDRA    Next data register A (group 0)
    //
    //   bits   r/w  code       description
    //    7-4   R/W  NDR7-4     Next output data for TPC output group 1
    //    3-0   R/W  NDR3-0     Next output data for TPC output group 0
    //
    // Address is 1F4 is used for group 3. Also used for group 2 when output groups 2 and 3 in TPCR are the same.
    // Address is 1F6 is used for group 2 when output groups 2 and 3 in TPCR are different.
    // Address is 1F5 is used for group 1. Also used for group 0 when output groups 0 and 1 in TPCR are the same.
    // Address is 1F7 is used for group 0 when output groups 0 and 1 in TPCR are different.
    // When in separate groups, the bits remain in the same position in the respective registers, and the remaining bits
    // become reserved and forced to 1.
    union RegNDR {
        uint16 u16;
        struct {
            uint8 A, B;
        };
        struct {
            uint8 ALo : 4, AHi : 4;
            uint8 BLo : 4, BHi : 4;
        };
    };

    FORCE_INLINE uint8 ReadNDRBGroup3() const {
        if (ituChanGroupBSame) {
            return NDR.B;
        } else {
            return NDR.BHi << 4u;
        }
    }

    FORCE_INLINE void WriteNDRBGroup3(uint8 value) {
        if (ituChanGroupBSame) {
            NDR.B = value;
        } else {
            NDR.BHi = bit::extract<4, 7>(value);
        }
    }

    FORCE_INLINE uint8 ReadNDRBGroup2() const {
        if (ituChanGroupBSame) {
            return 0;
        } else {
            return NDR.BLo;
        }
    }

    FORCE_INLINE void WriteNDRBGroup2(uint8 value) {
        if (ituChanGroupBSame) {
            // nothing
        } else {
            NDR.BLo = bit::extract<0, 3>(value);
        }
    }

    FORCE_INLINE uint8 ReadNDRAGroup1() const {
        if (ituChanGroupASame) {
            return NDR.A;
        } else {
            return NDR.AHi << 4u;
        }
    }

    FORCE_INLINE void WriteNDRAGroup1(uint8 value) {
        if (ituChanGroupASame) {
            NDR.A = value;
        } else {
            NDR.AHi = bit::extract<4, 7>(value);
        }
    }

    FORCE_INLINE uint8 ReadNDRAGroup0() const {
        if (ituChanGroupASame) {
            return 0;
        } else {
            return NDR.ALo;
        }
    }

    FORCE_INLINE void WriteNDRAGroup0(uint8 value) {
        if (ituChanGroupASame) {
            // nothing
        } else {
            NDR.ALo = bit::extract<0, 3>(value);
        }
    }

    // Whether each group uses non-overlapping mode.
    // Derived from TPMR.GnNOV
    std::array<bool, 4> nonOverlapModes;

    // Which ITU channel's compare match output is used for triggering TPC outputs for each group.
    // Derived from TPCR.GnCMS1-0
    std::array<uint8, 4> ituChanSelect;

    // Whether ITU channel compare match for groups 2 and 3 are the same.
    bool ituChanGroupASame;

    // Whether ITU channel compare match for groups 0 and 1 are the same.
    bool ituChanGroupBSame;

    // NDERA and NDERB combined.
    RegNDER NDER;

    // NDRA and NDRB combined.
    RegNDR NDR;
};

} // namespace ymir::sh1
