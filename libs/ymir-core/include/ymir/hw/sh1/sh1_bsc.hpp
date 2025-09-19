#pragma once

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::sh1 {

struct BusStateController {
    BusStateController() {
        Reset();
    }

    void Reset() {
        BCR.u16 = 0x0000;
        WCR1.u16 = 0xFFFF;
        WCR2.u16 = 0xFFFF;
        WCR3.u16 = 0xF800;
        DCR.u16 = 0x0000;
        PCR.u16 = 0x0000;
        RCR.u16 = 0x0000;
        RTCSR.u16 = 0x0000;
        RTCNT = 0x00;
        RTCOR = 0xFF;
    }

    // 1A0  R/W  8,16,32  0000      BCR     Bus control register
    //
    //   bits   r/w  code       description
    //     15   R/W  DRAME      DRAM Enable Bit (area 1: 0=external, 1=DRAM)
    //     14   R/W  IOE        Multiplexed I/O Enable Bit (area 6: 0=external, 1=muxed I/O)
    //     13   R/W  WARP       Warp Mode Bit
    // 	                          0 = normal: internal and external accesses not simultaneous
    // 	                          1 = warp: simultaneous internal and external accesses
    //     12   R/W  RDDTY      RD Duty (RD signal high level duty: 0=50% of T1, 1=35% of T1)
    //     11   R/W  BAS        Byte Access Select (0=nWRL,nWRL,A0; 1=nLBS,nWR,nHBS)
    //   10-0   R    -          Reserved - must be zero
    union RegBCR {
        uint16 u16;
        struct {
            uint16 _rsvd0_10 : 11;
            uint16 BAS : 1;
            uint16 RDDTY : 1;
            uint16 WARP : 1;
            uint16 IOE : 1;
            uint16 DRAME : 1;
        };
    };

    // 1A2  R/W  8,16,32  FFFF      WCR1    Wait state control register 1
    //
    //   bits   r/w  code       description
    //   15-8   R/W  RW7-0      Wait State Control During Read
    // 	                          Each bit corresponds to an area
    // 	                          0 = no nWAIT sampling
    // 	                          1 = sample nWAIT
    //    7-2   R    -          Reserved - must be one
    //      1   R/W  WW1        Wait State Control During Write
    //                            Specifically for area 1.
    // 	                          Area type              WW1=0    WW1=1
    // 	                          DRAM space (DRAME=1)   1 state  (invalid)
    // 	                          External (DRAM=0)      2+nWAIT  2+nWAIT
    //      0   R    -          Reserved - must be one
    union RegWCR1 {
        uint16 u16;
        struct {
            uint16 _rsvd0 : 1;
            uint16 WW1 : 1;
            uint16 _rsvd2_7 : 6;
            uint16 RWn : 8;
        };
    };

    // 1A4  R/W  8,16,32  FFFF      WCR2    Wait state control register 2
    //
    //   bits   r/w  code       description
    //   15-8   R/W  DRW7-0     Wait State Control During Single-Mode DMA Transfer
    // 	                          Each bit corresponds to an area
    // 	                          0 = no nWAIT sampling
    // 	                          1 = sample nWAIT
    //    7-0   R/W  DWW7-0     Single-Mode DMA Memory Write Wait State Control
    // 	                          Each bit corresponds to an area
    // 	                          0 = no nWAIT sampling
    // 	                          1 = sample nWAIT
    union RegWCR2 {
        uint16 u16;
        struct {
            uint16 DWWn : 8;
            uint16 DRWn : 8;
        };
    };

    // 1A6  R/W  8,16,32  F800      WCR3    Wait state control register 3
    //
    //   bits   r/w  code       description
    //     15   R/W  WPU        Wait Pin Pull-Up Control (0=nWAIT not pulled up; 1=nWAIT pulled up)
    //  14-13   R/W  A02LW1-0   Long Wait Insertion in Areas 0 and 2
    // 	                          00 (0) = 1 state inserted
    // 	                          01 (1) = 2 state inserted
    // 	                          10 (2) = 3 state inserted
    // 	                          11 (3) = 4 state inserted
    //  12-11   R/W  A6LW1-0    Long Wait Insertion in Area 6
    // 	                          00 (0) = 1 state inserted
    // 	                          01 (1) = 2 state inserted
    // 	                          10 (2) = 3 state inserted
    // 	                          11 (3) = 4 state inserted
    //   10-0   R    -          Reserved - must be zero
    union RegWCR3 {
        uint16 u16;
        struct {
            uint16 _rsvd0_10 : 11;
            uint16 A6LWn : 2;
            uint16 A02LWn : 2;
            uint16 WPU : 1;
        };
    };

    // 1A8  R/W  8,16,32  0000      DCR     DRAM area control register
    //
    //   bits   r/w  code       description
    //     15   R/W  CW2        Dual-CAS or Dual-WE Select Bit (0=nCASH,nCASL,nWRL; 1=nCASL,nWRH,nWRL)
    //     14   R/W  RASD       RAS Down (0=up; 1=down)
    //     13   R/W  TPC        RAS Precharge Cycle Count (0=1-state precharge; 1=2-state precharge)
    //     12   R/W  BE         Burst Operation Enable (0=normal; 1=burst)
    //     11   R/W  CDTY       CAS Duty (nCAS high level duty: 0=50% of Tc; 1=30% of Tc)
    //     10   R/W  MXE        Multiplex Enable Bit (0=disable; 1=enable)
    //    9-8   R/W  MXC1-0     Multiplex Shift Count
    // 	                          00 (0) = 8 bits, A8-A27
    // 	                          01 (1) = 9 bits, A9-A27
    // 	                          10 (2) = 10 bits, A10-A27
    // 	                          11 (3) = reserved
    //    7-0   R    -          Reserved - must be zero
    union RegDCR {
        uint16 u16;
        struct {
            uint16 _rsvd0_7 : 8;
            uint16 MXCn : 2;
            uint16 MXE : 1;
            uint16 CDTY : 1;
            uint16 BE : 1;
            uint16 TPC : 1;
            uint16 RASD : 1;
            uint16 CW2 : 1;
        };
    };

    // 1AA  R/W  8,16,32  0000      PCR     Parity control register
    //
    //   bits   r/w  code       description
    //     15   R/W  PEF        Parity Error Flag (0=no error; 1=error occurred)
    //     14   R/W  PFRC       Parity Output Force (0=not forced; 1=force high output)
    //     13   R/W  PEO        Parity Polarity (0=even; 1=odd)
    //  12-11   R/W  PCHK1-0    Parity Check Enable
    // 	                          00 (0) = Parity not checked and not generated
    // 	                          01 (1) = Parity checked and generated only in DRAM area
    // 	                          10 (2) = Parity checked and generated in DRAM area and area 2
    // 	                          11 (3) = Reserved
    //   10-0   R    -          Reserved - must be zero
    union RegPCR {
        uint16 u16;
        struct {
            uint16 _rsvd0_10 : 11;
            uint16 PCHKn : 2;
            uint16 PEO : 1;
            uint16 PFRC : 1;
            uint16 PEF : 1;
        };
    };

    // 1AC  R/W  8,16,32  0000      RCR     Refresh control register
    //
    //   bits   r/w  code       description
    //   15-8   R    -          Reserved - must be zero
    //      7   R/W  RFSHE      Refresh Control (0=disabled, RTCNT usable as timer; 1=enabled)
    //      6   R/W  RMODE      Refresh Mode (0=CAS-before-RAS refresh; 1=Self-refresh)
    //    5-4   R/W  RLW1-0     CBR Refresh Wait State Insertion
    // 	                          00 (0) = 1 state inserted
    // 	                          01 (1) = 2 state inserted
    // 	                          10 (2) = 3 state inserted
    // 	                          11 (3) = 4 state inserted
    //    3-0   R    -          Reserved - must be zero
    //
    //   Requires 16-bit writes in the format 0x5A__
    union RegRCR {
        uint16 u16;
        struct {
            uint16 _rsvd0_3 : 4;
            uint16 RLWn : 2;
            uint16 RMODE : 1;
            uint16 RFSHE : 1;
            uint16 _rsvd8_15 : 8;
        };
    };

    // 1AE  R/W  8,16,32  0000      RTCSR   Refresh timer control/status register
    //
    //   bits   r/w  code       description
    //   15-8   R    -          Reserved - must be zero
    //      7   R/W  CMF        Compare Match Flag (0=no match; 1=match -- RTCNT==RTCOR)
    //      6   R/W  CMIE       Compare Match Interrupt Enable (0=disable; 1=enable)
    //    5-3   R/W  CKS2-0     Clock Select
    // 	                          000 (0) = disabled
    // 	                          001 (1) = phi/2
    // 	                          010 (2) = phi/8
    // 	                          011 (3) = phi/32
    // 	                          100 (4) = phi/128
    // 	                          101 (5) = phi/512
    // 	                          110 (6) = phi/2048
    // 	                          111 (7) = phi/4096
    //    2-0   R    -          Reserved - must be zero
    //
    //   Requires 16-bit writes in the format 0xA5__
    union RegRTCSR {
        uint16 u16;
        struct {
            uint16 _rsvd0_2 : 3;
            uint16 CKSn : 3;
            uint16 CMIE : 1;
            uint16 CMF : 1;
            uint16 _rsvd8_15 : 8;
        };
    };

    // 1B0  R/W  8,16,32  0000      RTCNT   Refresh timer counter
    //
    //   bits   r/w  code       description
    //   15-8   R    -          Reserved - must be zero
    //    7-0   R    RTCNT      Refresh Timer Counter
    //
    //   Requires 16-bit writes in the format 0x69__
    using RegRTCNT = uint8;

    // 1B2  R/W  8,16,32  00FF      RTCOR   Refresh timer constant register
    //
    //   bits   r/w  code       description
    //   15-8   R    -          Reserved - must be zero
    //    7-0   R    RTCOR      Refresh Timer Constant
    //
    //   Requires 16-bit writes in the format 0x96__
    using RegRTCOR = uint8;

    RegBCR BCR;
    RegWCR1 WCR1;
    RegWCR2 WCR2;
    RegWCR3 WCR3;
    RegDCR DCR;
    RegPCR PCR;
    RegRCR RCR;
    RegRTCSR RTCSR;
    RegRTCNT RTCNT;
    RegRTCOR RTCOR;
};

} // namespace ymir::sh1
