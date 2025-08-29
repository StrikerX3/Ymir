#pragma once

#include <ymir/state/state.hpp>

#include <ymir/util/size_ops.hpp>

#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>

#include <algorithm>

namespace ymir::state {

// Current save state format version.
// Increment once per release if there are any changes to the serializers.
// Remember to document every change!
// Versions:
//   1 = 0.1.0
//   2 = 0.1.1
//   3 = 0.1.2
//   4 = 0.1.3
//   5 = 0.1.4
//   6 = 0.1.5
//   7 = 0.1.6
//   8 = 0.1.7
//   9 = 0.1.8
inline constexpr uint32 kVersion = 9;

} // namespace ymir::state

CEREAL_CLASS_VERSION(ymir::state::State, ymir::state::kVersion);

namespace ymir::state {

template <class Archive>
void serialize(Archive &ar, SchedulerState &s) {
    ar(s.currCount, s.events);
}

template <class Archive>
void serialize(Archive &ar, SchedulerState::EventState &s) {
    ar(s.target, s.countNumerator, s.countDenominator, s.id);
}

template <class Archive>
void serialize(Archive &ar, SystemState &s) {
    ar(s.videoStandard, s.clockSpeed);
    ar(s.slaveSH2Enabled);
    ar(s.iplRomHash);
    ar(s.WRAMLow, s.WRAMHigh);
}

template <class Archive>
void serialize(Archive &ar, SH2State &s, const uint32 version) {
    // v6:
    // - New fields
    //   - bool sleep = false

    ar(s.R, s.PC, s.PR, s.MACL, s.MACH, s.SR, s.GBR, s.VBR);
    ar(s.delaySlot, s.delaySlotTarget);
    ar(s.bsc, s.dmac);
    serialize(ar, s.wdt, version);
    serialize(ar, s.divu, version);
    serialize(ar, s.frt, version);
    ar(s.intc, s.cache, s.SBYCR);
    if (version >= 8) {
        ar(s.sleep);
    } else {
        s.sleep = false;
    }
    if (version < 5) {
        s.divu.VCRDIV = s.intc.vectors[12]; // 12 == static_cast<size_t>(sh2::InterruptSource::DIVU_OVFI)
    }
}

template <class Archive>
void serialize(Archive &ar, SH2State::BSC &s) {
    ar(s.BCR1, s.BCR2, s.WCR, s.MCR, s.RTCSR, s.RTCNT, s.RTCOR);
}

template <class Archive>
void serialize(Archive &ar, SH2State::DMAC &s) {
    ar(s.DMAOR, s.channels);
}

template <class Archive>
void serialize(Archive &ar, SH2State::DMAC::Channel &s) {
    ar(s.SAR, s.DAR, s.TCR, s.CHCR, s.DRCR);
}

template <class Archive>
void serialize(Archive &ar, SH2State::WDT &s, const uint32 version) {
    // v6:
    // - New fields
    //   - uint8 busValue = 0
    // v5:
    // - New fields
    //   - WTCSR_mask = false
    // - Changed fields
    //   - cycleCount is now an absolute counter based on the scheduler counter

    ar(s.WTCSR, s.WTCNT, s.RSTCSR, s.cycleCount);
    if (version >= 5) {
        ar(s.WTCSR_mask);
    } else {
        s.WTCSR_mask = false;
    }
    if (version >= 6) {
        ar(s.busValue);
    } else {
        s.busValue = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, SH2State::DIVU &s, const uint32 version) {
    // v5:
    // - New fields
    //   - VCRDIV = INTC.vectors[static_cast<size_t>(InterruptSource::DIVU_OVFI)]

    ar(s.DVSR, s.DVDNT, s.DVCR, s.DVDNTH, s.DVDNTL, s.DVDNTUH, s.DVDNTUL);
    if (version >= 5) {
        ar(s.VCRDIV);
        // VCRDIV is filled in with INTC.vectors[DIVU_OVFI] for version prior to 5 in the SH2State serializer above
    }
}

template <class Archive>
void serialize(Archive &ar, SH2State::FRT &s, const uint32 version) {
    // v5:
    // - New fields
    //   - FTCSR_mask = 0x00
    // - Changed fields
    //   - cycleCount is now an absolute counter based on the scheduler counter

    ar(s.TIER, s.FTCSR, s.FRC, s.OCRA, s.OCRB, s.TCR, s.TOCR, s.ICR, s.TEMP, s.cycleCount);
    if (version >= 5) {
        ar(s.FTCSR_mask);
    } else {
        s.FTCSR_mask = 0x00;
    }
}

template <class Archive>
void serialize(Archive &ar, SH2State::INTC &s) {
    ar(s.ICR, s.levels, s.vectors, s.pendingSource, s.pendingLevel, s.NMI, s.extVec);
}

template <class Archive>
void serialize(Archive &ar, SH2State::Cache &s) {
    ar(s.CCR, s.entries, s.lru);
}

template <class Archive>
void serialize(Archive &ar, SH2State::Cache::Entry &s) {
    ar(s.tags, s.lines);
}

template <class Archive>
void serialize(Archive &ar, SCUState &s, const uint32 version) {
    // v8:
    // - New fields
    //   - abusIntrsPendingAck = intrStatus >> 16  (or 0 if abusIntrAck == true in versions 7 and below)
    //   - timer1Triggered = true
    // - Removed fields
    //   - bool abusIntrAck
    // v5:
    // - New fields
    //   - pendingIntrLevel = 0
    //   - pendingIntrIndex = 0
    // - Changed fields
    //   - timer1Enable renamed to timerEnable; no changes to value
    // v4:
    // - New fields
    //   - enum SCUState::CartType: added ROM

    ar(s.dma);
    serialize(ar, s.dsp, version);
    ar(s.cartType);

    if (version >= 4) {
        // From version 4 onwards, carts have a fixed size.
        switch (s.cartType) {
        case SCUState::CartType::DRAM8Mbit: s.cartData.resize(1_MiB); break;
        case SCUState::CartType::DRAM32Mbit: s.cartData.resize(4_MiB); break;
        case SCUState::CartType::ROM: s.cartData.resize(2_MiB);
        default: s.cartData.clear(); break;
        }
    } else {
        // Up to version 3, DRAM cartridge states could store an arbitrary amount of data.
        // Besides the DRAM cartridge, only the Backup RAM cartridge was also available.
        //
        // Reject save states with unexpected sizes to prevent potential memory allocation attacks.
        cereal::size_type size;
        ar(size);
        switch (s.cartType) {
        case SCUState::CartType::DRAM8Mbit:
            if (size != 1_MiB) {
                throw cereal::Exception("Unexpected 8 Mbit DRAM cart data array size");
            }
            break;
        case SCUState::CartType::DRAM32Mbit:
            if (size != 4_MiB) {
                throw cereal::Exception("Unexpected 32 Mbit DRAM cart data array size");
            }
            break;
        default:
            if (size != 0) {
                throw cereal::Exception("Unexpected cart data array size");
            }
            break;
        }
        s.cartData.resize(size);
    }
    if (!s.cartData.empty()) {
        ar(cereal::binary_data(s.cartData.data(), s.cartData.size()));
    }
    ar(s.intrMask, s.intrStatus);
    if (version >= 8) {
        ar(s.abusIntrsPendingAck);
    } else {
        bool abusIntrAck;
        ar(abusIntrAck);
        s.abusIntrsPendingAck = abusIntrAck ? 0x0000 : (s.intrStatus >> 16u);
    }
    if (version >= 5) {
        ar(s.pendingIntrLevel, s.pendingIntrIndex);
    } else {
        s.pendingIntrLevel = 0;
        s.pendingIntrIndex = 0;
    }
    ar(s.timer0Counter, s.timer0Compare);
    ar(s.timer1Reload);
    if (version >= 8) {
        ar(s.timer1Triggered);
    } else {
        s.timer1Triggered = true;
    }
    ar(s.timerEnable, s.timer1Mode);
    ar(s.wramSizeSelect);
}

template <class Archive>
void serialize(Archive &ar, SCUDMAState &s) {
    ar(s.srcAddr, s.dstAddr, s.xferCount);
    ar(s.srcAddrInc, s.dstAddrInc, s.updateSrcAddr, s.updateDstAddr);
    ar(s.enabled, s.active, s.indirect, s.trigger, s.start);
    ar(s.currSrcAddr, s.currDstAddr, s.currXferCount);
    ar(s.currSrcAddrInc, s.currDstAddrInc);
    ar(s.currIndirectSrc, s.endIndirect);
}

template <class Archive>
void serialize(Archive &ar, SCUDSPState &s, const uint32 version) {
    // v7:
    // - New fields
    //   - dmaAddrD0 = 0
    //   - dmaPC = PC
    // v6:
    // - New fields
    //   - nextInstr = programRAM[PC]
    //   - looping = false
    //   - cyclesSpillover = 0
    // - Removed fields
    //   - uint32 nextPC
    //   - uint8 jmpCounter

    ar(s.programRAM, s.dataRAM);
    ar(s.programExecuting, s.programPaused, s.programEnded, s.programStep);
    ar(s.PC, s.dataAddress);
    if (version >= 6) {
        ar(s.nextInstr);
    } else {
        s.nextInstr = s.programRAM[s.PC];
    }
    if (version < 6) {
        uint32 nextPC;
        uint8 jmpCounter;
        ar(nextPC, jmpCounter);
    }
    ar(s.sign, s.zero, s.carry, s.overflow);
    ar(s.CT, s.ALU, s.AC, s.P, s.RX, s.RY, s.LOP, s.TOP);
    if (version >= 6) {
        ar(s.looping);
    } else {
        s.looping = false;
    }
    ar(s.dmaRun, s.dmaToD0, s.dmaHold, s.dmaCount, s.dmaSrc, s.dmaDst);
    ar(s.dmaReadAddr, s.dmaWriteAddr, s.dmaAddrInc);
    if (version >= 7) {
        ar(s.dmaAddrD0, s.dmaPC);
    } else {
        s.dmaAddrD0 = 0;
        s.dmaPC = s.PC;
    }
    if (version >= 6) {
        ar(s.cyclesSpillover);
    } else {
        s.cyclesSpillover = 0u;
    }
}

template <class Archive>
void serialize(Archive &ar, SMPCState &s, const uint32 version) {
    // v9:
    // - New fields
    //   - commandEventState = 0

    ar(s.IREG, s.OREG, s.COMREG, s.SR, s.SF);
    ar(s.PDR1, s.PDR2, s.DDR1, s.DDR2, s.IOSEL, s.EXLE);
    ar(s.intback);
    ar(s.busValue, s.resetDisable);
    ar(s.rtcTimestamp, s.rtcSysClockCount);
    if (version >= 9) {
        ar(s.commandEventState);
    } else {
        s.commandEventState = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, SMPCState::INTBACK &s) {
    ar(s.getPeripheralData, s.optimize, s.port1mode, s.port2mode);
    ar(s.report, s.reportOffset, s.inProgress);
}

template <class Archive>
void serialize(Archive &ar, VDPState &s, const uint32 version) {
    // v9:
    // - New fields
    //   - enum VDPState::VerticalPhase: added VCounterSkip (= 5)
    // v7:
    // - New fields
    //   - VDP1TimingPenalty = 0
    // v6:
    // - Removed fields
    //   - uint16 VCounter -> moved to regs2.VCNT

    ar(s.VRAM1, s.VRAM2, s.CRAM, s.spriteFB, s.displayFB);
    if (version >= 7) {
        ar(s.VDP1TimingPenalty);
    } else {
        s.VDP1TimingPenalty = 0;
    }
    ar(s.regs1, s.regs2);
    ar(s.HPhase, s.VPhase);
    if (version < 6) {
        uint16 VCounter;
        ar(VCounter);
        s.regs2.VCNT = VCounter;
    }
    if (version < 9) {
        // The VCNT skip phase timing was slightly shifted. Adjust the phase here.
        uint16 lowerBound, upperBound;
        if (s.regs2.TVSTAT & 1) {
            // PAL
            switch (s.regs2.TVMD & 3) {
            case 0:
                lowerBound = 259;
                upperBound = 281;
                break;
            case 1:
                lowerBound = 267;
                upperBound = 289;
                break;
            case 2: [[fallthrough]];
            case 3:
                lowerBound = 275;
                upperBound = 297;
                break;
            }
        } else {
            // NTSC
            lowerBound = (s.regs2.TVMD & 1) ? 245 : 237;
            upperBound = 255;
        }
        if (s.regs2.VCNT >= lowerBound && s.regs2.VCNT < upperBound) {
            s.VPhase = VDPState::VerticalPhase::VCounterSkip;
        }

        // Replace obsolete horizontal phases
        switch (static_cast<uint8>(s.HPhase)) {
        case 3 /*VBlankOut*/: s.HPhase = VDPState::HorizontalPhase::Sync; break;
        case 5 /*LastDot*/: s.HPhase = VDPState::HorizontalPhase::LeftBorder; break;
        default: break;
        }
    }
    serialize(ar, s.renderer, version);

    if (version < 4) {
        // Compensate for the removal of SCXIN/SCYIN from fracScrollX/Y
        s.renderer.normBGLayerStates[0].fracScrollX -= (s.regs2.SCXIN0 << 8u) | (s.regs2.SCXDN0 >> 8u);
        s.renderer.normBGLayerStates[1].fracScrollX -= (s.regs2.SCXIN1 << 8u) | (s.regs2.SCXDN1 >> 8u);
        s.renderer.normBGLayerStates[2].fracScrollX -= (s.regs2.SCXIN2 << 8u);
        s.renderer.normBGLayerStates[3].fracScrollX -= (s.regs2.SCXIN3 << 8u);

        s.renderer.normBGLayerStates[0].fracScrollY -= (s.regs2.SCYIN0 << 8u) | (s.regs2.SCYDN0 >> 8u);
        s.renderer.normBGLayerStates[1].fracScrollY -= (s.regs2.SCYIN1 << 8u) | (s.regs2.SCYDN1 >> 8u);
        s.renderer.normBGLayerStates[2].fracScrollY -= (s.regs2.SCYIN2 << 8u);
        s.renderer.normBGLayerStates[3].fracScrollY -= (s.regs2.SCYIN3 << 8u);
    }
    if (version < 7) {
        // Compensate for the lack of scrollAmountV in earlier versions
        s.renderer.normBGLayerStates[0].scrollAmountV = (s.regs2.SCYIN0 << 8u) | (s.regs2.SCYDN0 >> 8u);
        s.renderer.normBGLayerStates[1].scrollAmountV = (s.regs2.SCYIN1 << 8u) | (s.regs2.SCYDN1 >> 8u);
        s.renderer.normBGLayerStates[2].scrollAmountV = (s.regs2.SCYIN2 << 8u);
        s.renderer.normBGLayerStates[3].scrollAmountV = (s.regs2.SCYIN3 << 8u);
    }
}

template <class Archive>
void serialize(Archive &ar, VDPState::VDPRendererState &s, const uint32 version) {
    // v7:
    // - New fields
    //   - vramFetchers = (default values)
    // v5:
    // - New fields
    //   - erase = false
    // v4:
    // - New fields
    //   - vertCellScrollInc = sizeof(uint32)

    serialize(ar, s.vdp1State, version);
    for (auto &state : s.normBGLayerStates) {
        serialize(ar, state, version);
    }
    for (auto &state : s.rotParamStates) {
        serialize(ar, state, version);
    }
    ar(s.lineBackLayerState);
    if (version >= 7) {
        for (auto &fieldFetchers : s.vramFetchers) {
            for (auto &fetcher : fieldFetchers) {
                serialize(ar, fetcher, version);
            }
        }
    } else {
        for (auto &fieldFetchers : s.vramFetchers) {
            for (auto &fetcher : fieldFetchers) {
                fetcher = {};
            }
        }
    }
    if (version >= 4) {
        ar(s.vertCellScrollInc);
    } else {
        s.vertCellScrollInc = sizeof(uint32);
    }
    ar(s.displayFB);
    ar(s.vdp1Done);
}

template <class Archive>
void serialize(Archive &ar, VDPState::VDP1RegsState &s) {
    ar(s.TVMR, s.FBCR, s.PTMR);
    ar(s.EWDR, s.EWLR, s.EWRR, s.EDSR);
    ar(s.LOPR, s.COPR);
    ar(s.MODR);
    ar(s.manualSwap, s.manualErase);
}

template <class Archive>
void serialize(Archive &ar, VDPState::VDP2RegsState &s) {
    ar(s.TVMD, s.EXTEN, s.TVSTAT, s.VRSIZE, s.HCNT, s.VCNT, s.RAMCTL);
    ar(s.CYCA0L, s.CYCA0U, s.CYCA1L, s.CYCA1U, s.CYCB0L, s.CYCB0U, s.CYCB1L, s.CYCB1U);
    ar(s.BGON);
    ar(s.MZCTL);
    ar(s.SFSEL, s.SFCODE);
    ar(s.CHCTLA, s.CHCTLB);
    ar(s.BMPNA, s.BMPNB);
    ar(s.PNCNA, s.PNCNB, s.PNCNC, s.PNCND, s.PNCR);
    ar(s.PLSZ);
    ar(s.MPOFN, s.MPOFR);
    ar(s.MPABN0, s.MPCDN0, s.MPABN1, s.MPCDN1, s.MPABN2, s.MPCDN2, s.MPABN3, s.MPCDN3);
    ar(s.MPABRA, s.MPCDRA, s.MPEFRA, s.MPGHRA, s.MPIJRA, s.MPKLRA, s.MPMNRA, s.MPOPRA);
    ar(s.MPABRB, s.MPCDRB, s.MPEFRB, s.MPGHRB, s.MPIJRB, s.MPKLRB, s.MPMNRB, s.MPOPRB);
    ar(s.SCXIN0, s.SCXDN0, s.SCYIN0, s.SCYDN0, s.ZMXIN0, s.ZMXDN0, s.ZMYIN0, s.ZMYDN0);
    ar(s.SCXIN1, s.SCXDN1, s.SCYIN1, s.SCYDN1, s.ZMXIN1, s.ZMXDN1, s.ZMYIN1, s.ZMYDN1);
    ar(s.SCXIN2, s.SCYIN2);
    ar(s.SCXIN3, s.SCYIN3);
    ar(s.ZMCTL, s.SCRCTL);
    ar(s.VCSTAU, s.VCSTAL);
    ar(s.LSTA0U, s.LSTA0L, s.LSTA1U, s.LSTA1L);
    ar(s.LCTAU, s.LCTAL);
    ar(s.BKTAU, s.BKTAL);
    ar(s.RPMD, s.RPRCTL, s.KTCTL, s.KTAOF);
    ar(s.OVPNRA, s.OVPNRB);
    ar(s.RPTAU, s.RPTAL);
    ar(s.WPSX0, s.WPSY0, s.WPEX0, s.WPEY0);
    ar(s.WPSX1, s.WPSY1, s.WPEX1, s.WPEY1);
    ar(s.WCTLA, s.WCTLB, s.WCTLC, s.WCTLD);
    ar(s.LWTA0U, s.LWTA0L, s.LWTA1U, s.LWTA1L);
    ar(s.SPCTL, s.SDCTL);
    ar(s.CRAOFA, s.CRAOFB);
    ar(s.LNCLEN);
    ar(s.SFPRMD);
    ar(s.CCCTL, s.SFCCMD);
    ar(s.PRISA, s.PRISB, s.PRISC, s.PRISD, s.PRINA, s.PRINB, s.PRIR);
    ar(s.CCRSA, s.CCRSB, s.CCRSC, s.CCRSD, s.CCRNA, s.CCRNB, s.CCRR);
    ar(s.CCRLB);
    ar(s.CLOFEN, s.CLOFSL);
    ar(s.COAR, s.COAG, s.COAB);
    ar(s.COBR, s.COBG, s.COBB);
}

template <class Archive>
void serialize(Archive &ar, VDPState::VDPRendererState::VDP1RenderState &s, const uint32 version) {
    ar(s.sysClipH, s.sysClipV);
    ar(s.userClipX0, s.userClipY0, s.userClipX1, s.userClipY1);
    ar(s.localCoordX, s.localCoordY);
    ar(s.rendering);
    if (version >= 5) {
        ar(s.erase);
    } else {
        s.erase = false;
    }
    ar(s.cycleCount);
}

template <class Archive>
void serialize(Archive &ar, VDPState::VDPRendererState::NormBGLayerState &s, const uint32 version) {
    // v7:
    // - New fields
    //   - normBGLayerStates[0].scrollAmountV = (regs2.SCYIN0 << 8u) | (regs2.SCYDN0 >> 8u);
    //   - normBGLayerStates[1].scrollAmountV = (regs2.SCYIN1 << 8u) | (regs2.SCYDN1 >> 8u);
    //   - normBGLayerStates[2].scrollAmountV = (regs2.SCYIN2 << 8u);
    //   - normBGLayerStates[3].scrollAmountV = (regs2.SCYIN3 << 8u);
    //   - vertCellScrollDelay = false
    //   - vertCellScrollRepeat = false
    // v4:
    // - New fields
    //   - vertCellScrollOffset = 0
    // - Changed fields
    //   - fracScrollX and fracScrollY no longer include the values of SC[XY][ID]N#. Therefore, they need to be
    //     compensated for as follows:
    //       normBGLayerStates[0].fracScrollX -= (regs2.SCXIN0 << 8u) | (regs2.SCXDN0 >> 8u);
    //       normBGLayerStates[1].fracScrollX -= (regs2.SCXIN1 << 8u) | (regs2.SCXDN1 >> 8u);
    //       normBGLayerStates[2].fracScrollX -= (regs2.SCXIN2 << 8u);
    //       normBGLayerStates[3].fracScrollX -= (regs2.SCXIN3 << 8u);
    //
    //       normBGLayerStates[0].fracScrollY -= (regs2.SCYIN0 << 8u) | (regs2.SCYDN0 >> 8u);
    //       normBGLayerStates[1].fracScrollY -= (regs2.SCYIN1 << 8u) | (regs2.SCYDN1 >> 8u);
    //       normBGLayerStates[2].fracScrollY -= (regs2.SCYIN2 << 8u);
    //       normBGLayerStates[3].fracScrollY -= (regs2.SCYIN3 << 8u);

    // NOTE: fracScrollX/Y and scrollAmountV compensation happens in the VDPState serializer
    ar(s.fracScrollX, s.fracScrollY, s.scrollIncH);
    if (version >= 7) {
        ar(s.scrollAmountV);
    }
    ar(s.lineScrollTableAddress);
    if (version >= 4) {
        ar(s.vertCellScrollOffset);
    } else {
        s.vertCellScrollOffset = 0;
    }
    if (version >= 7) {
        ar(s.vertCellScrollDelay, s.vertCellScrollRepeat);
    } else {
        s.vertCellScrollDelay = false;
        s.vertCellScrollRepeat = false;
    }
    ar(s.mosaicCounterY);
}

template <class Archive>
void serialize(Archive &ar, VDPState::VDPRendererState::RotationParamState &s, const uint32 version) {
    // v9:
    // - Changed fields
    //   - pageBaseAddresses changed from std::array<uint32, 16> to std::array<std::array<uint32, 16>, 2>
    // v8:
    // - New fields
    //   - Xst = 0
    //   - Yst = 0
    // - Removed fields
    //   - sint32 scrX
    //   - sint32 scrY
    if (version >= 9) {
        ar(s.pageBaseAddresses);
    } else {
        ar(s.pageBaseAddresses[0]);
        s.pageBaseAddresses[1] = s.pageBaseAddresses[0];
    }
    if (version >= 8) {
        ar(s.Xst, s.Yst);
    } else {
        sint32 scrX, scrY;
        ar(scrX, scrY);
        s.Xst = 0;
        s.Yst = 0;
    }
    ar(s.KA);
}

template <class Archive>
void serialize(Archive &ar, VDPState::VDPRendererState::LineBackLayerState &s) {
    ar(s.lineColor);
    ar(s.backColor);
}

template <class Archive>
void serialize(Archive &ar, VDPState::VDPRendererState::Character &s) {
    // v7:
    // - Struct created
    ar(s.charNum, s.palNum);
    ar(s.specColorCalc, s.specPriority);
    ar(s.flipH, s.flipV);
}

template <class Archive>
void serialize(Archive &ar, VDPState::VDPRendererState::VRAMFetcherState &s, const uint32 version) {
    // v7:
    // - Struct created

    if (version >= 7) {
        ar(s.currChar, s.nextChar, s.lastCharIndex);
        ar(s.bitmapData, s.bitmapDataAddress);
        ar(s.lastVCellScroll);
    } else {
        s.currChar = {};
        s.nextChar = {};
        s.lastCharIndex = 0xFFFFFFFF;
        s.bitmapData.fill(0);
        s.bitmapDataAddress = 0xFFFFFFFF;
        s.lastVCellScroll = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, M68KState &s) {
    ar(s.DA, s.SP_swap, s.PC, s.SR);
    ar(s.prefetchQueue, s.extIntrLevel);
}

template <class Archive>
void serialize(Archive &ar, SCSPState &s, const uint32 version) {
    // v6:
    // - New fields
    //   - SCILV = {0,0,0}
    //     (unfortunately the data is missing, so old save states will never restore properly)
    //   - reuseSCILV = true if version < 6, false otherwise; not stored in save state binary
    //   - KYONEXExec = false
    //   - currSlot = 0
    //   - out = {0,0}
    //   - midiInputBuffer = {0,0,0,...}
    //   - midiInputReadPos = 0
    //   - midiInputWritePos = 0
    //   - midiInputOverflow = false
    //   - midiOutputBuffer = {0,0,0,...}
    //   - midiOutputSize = 0
    //   - expectedOutputPacketSize = 0
    // - Removed fields
    //   - uint64 sampleCycles
    // - Misc changes
    //   - DAC18B and MEM4MB were swapped
    // v5:
    // - Changed fields
    //   - cddaBuffer array size reduced from 2048 * 75 to 2352 * 25; note that this is a circular buffer indexed by
    //     cddaReadPos and cddaWritePos
    // v4:
    // - Removed fields
    //   - uint16 egCycle
    //   - bool egStep
    // v3:
    // - New fields
    //   - KYONEX = false

    ar(s.WRAM);
    if (version >= 5) {
        ar(s.cddaBuffer, s.cddaReadPos, s.cddaWritePos, s.cddaReady);
    } else {
        // Reconstruct circular buffer
        auto cddaBuffer = std::make_unique<std::array<uint8, 2048 * 75>>();
        uint32 cddaReadPos, cddaWritePos;
        ar(*cddaBuffer, cddaReadPos, cddaWritePos, s.cddaReady);

        // Use the most recent samples if there is too much data in the old buffer since the new buffer is smaller
        uint32 count = cddaWritePos - cddaReadPos;
        if (cddaWritePos < cddaReadPos) {
            count += 2048 * 75;
        } else if (cddaWritePos == cddaReadPos && s.cddaReady) {
            count = 2048 * 75;
        }
        if (count > s.cddaBuffer.size()) {
            cddaReadPos += count - s.cddaBuffer.size();
            if (cddaReadPos >= 2048 * 75) {
                cddaReadPos -= 2048 * 75;
            }
            count = s.cddaBuffer.size();
        }

        if (cddaWritePos < cddaReadPos) {
            // ======W-----R======
            s.cddaReadPos = 0;
            s.cddaWritePos = cddaWritePos - cddaReadPos + 2048 * 75;
            if (s.cddaWritePos >= s.cddaBuffer.size()) {
                s.cddaWritePos -= s.cddaBuffer.size();
            }
            std::copy(cddaBuffer->begin() + cddaReadPos, cddaBuffer->end(), s.cddaBuffer.begin());
            std::copy_n(cddaBuffer->begin(), cddaWritePos, s.cddaBuffer.begin() + (2048 * 75 - cddaReadPos));
        } else if (cddaWritePos > cddaReadPos) {
            // ------R=====W------
            s.cddaReadPos = 0;
            s.cddaWritePos = cddaWritePos - cddaReadPos;
            if (s.cddaWritePos >= s.cddaBuffer.size()) {
                s.cddaWritePos -= s.cddaBuffer.size();
            }
            std::copy(cddaBuffer->begin() + cddaReadPos, cddaBuffer->begin() + cddaWritePos, s.cddaBuffer.begin());
        } else if (s.cddaReady) {
            // Buffer is full
            // NOTE: this case should never happen since the target buffer is smaller than the source buffer and the
            // copy length is clamped before reaching this if-else chain
            std::copy(cddaBuffer->begin() + cddaReadPos, cddaBuffer->end(), s.cddaBuffer.begin());
            std::copy(cddaBuffer->begin(), cddaBuffer->begin() + cddaWritePos, s.cddaBuffer.begin() + cddaReadPos);
            s.cddaReadPos = 0;
            s.cddaWritePos = 0;
        } else {
            // Buffer is empty
            s.cddaBuffer.fill(0);
            s.cddaReadPos = 0;
            s.cddaWritePos = 0;
        }
    }
    ar(s.m68k, s.m68kSpilloverCycles, s.m68kEnabled);
    for (auto &slot : s.slots) {
        serialize(ar, slot, version);
    }
    if (version >= 3) {
        ar(s.KYONEX);
    } else {
        s.KYONEX = false;
    }
    if (version >= 6) {
        ar(s.KYONEXExec);
    } else {
        s.KYONEXExec = false;
    }
    ar(s.MVOL);
    if (version >= 6) {
        ar(s.DAC18B, s.MEM4MB);
    } else {
        ar(s.MEM4MB, s.DAC18B);
    }
    ar(s.MSLC);
    ar(s.timers);
    ar(s.MCIEB, s.MCIPD);
    ar(s.SCIEB, s.SCIPD);
    if (version >= 6) {
        ar(s.SCILV);
        s.reuseSCILV = false;
    } else {
        s.SCILV.fill(0);
        s.reuseSCILV = true;
    }
    ar(s.DEXE, s.DDIR, s.DGATE, s.DMEA, s.DRGA, s.DTLG);
    ar(s.SOUS, s.soundStackIndex);
    serialize(ar, s.dsp, version);
    ar(s.m68kCycles);
    if (version < 6) {
        uint64 sampleCycles;
        ar(sampleCycles);
    }
    ar(s.sampleCounter);
    if (version < 4) {
        uint16 egCycle;
        bool egStep;
        ar(egCycle, egStep);
    }
    ar(s.lfsr);
    if (version >= 6) {
        ar(s.currSlot);
        ar(s.out);
    } else {
        s.currSlot = 0;
        s.out.fill(0);
    }
    if (version >= 6) {
        ar(s.midiInputBuffer);
        ar(s.midiInputReadPos);
        ar(s.midiInputWritePos);
        ar(s.midiInputOverflow);

        ar(s.midiOutputBuffer);
        ar(s.midiOutputSize);
        ar(s.expectedOutputPacketSize);
    } else {
        s.midiInputBuffer.fill(0);
        s.midiInputReadPos = 0;
        s.midiInputWritePos = 0;
        s.midiInputOverflow = false;

        s.midiOutputBuffer.fill(0);
        s.midiOutputSize = 0;
        s.expectedOutputPacketSize = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, SCSPSlotState &s, const uint32 version) {
    // v6:
    // - New fields
    //   - currEGLevel = egLevel
    // - Removed fields
    //   - uint32 sampleCount
    // v4:
    // - New fields
    //   - MM = bit 15 of extra10 if available, otherwise false
    //   - modulation = 0
    //   - egAttackBug = false
    //   - finalLevel = 0
    // - Removed fields
    //   - uint16 extra10
    //   - uint32 currAddress
    // - Changed fields
    //   - LSA and LEA changed from uint32 to uint16
    // v3:
    // - New fields
    //   - SBCTL = 0
    //   - EGBYPASS = false
    //   - extra0C = 0
    //   - extra10 = 0
    //   - extra14 = 0
    //   - nextPhase = currPhase
    //   - alfoOutput = 0
    // - Changed fields
    //   - currPhase >>= 4u

    ar(s.SA);
    if (version >= 4) {
        ar(s.LSA, s.LEA);
    } else {
        uint32 tmp;
        ar(tmp);
        s.LSA = tmp;
        ar(tmp);
        s.LEA = tmp;
    }
    ar(s.PCM8B, s.KYONB);
    if (version >= 3) {
        ar(s.SBCTL);
    } else {
        s.SBCTL = 0;
    }
    ar(s.LPCTL);
    ar(s.SSCTL);
    ar(s.AR, s.D1R, s.D2R, s.RR, s.DL);
    ar(s.KRS, s.EGHOLD, s.LPSLNK);
    if (version >= 3) {
        ar(s.EGBYPASS);
    } else {
        s.EGBYPASS = false;
    }
    ar(s.MDL, s.MDXSL, s.MDYSL, s.STWINH);
    ar(s.TL, s.SDIR);
    ar(s.OCT, s.FNS);
    if (version >= 4) {
        ar(s.MM);
    } else {
        s.MM = false;
    }
    ar(s.LFORE, s.LFOF, s.ALFOS, s.PLFOS, s.ALFOWS, s.PLFOWS);
    ar(s.IMXL, s.ISEL, s.DISDL, s.DIPAN);
    ar(s.EFSDL, s.EFPAN);
    if (version >= 3) {
        ar(s.extra0C);
        if (version == 3) {
            uint16 extra10;
            ar(extra10);
            s.MM = bit::test<15>(extra10);
        }
        ar(s.extra14);
    } else {
        s.extra0C = 0;
        s.extra14 = 0;
    }
    ar(s.active);
    ar(s.egState);
    ar(s.egLevel);
    if (version >= 6) {
        ar(s.currEGLevel);
    } else {
        s.currEGLevel = s.egLevel;
    }
    if (version >= 4) {
        ar(s.egAttackBug);
    } else {
        s.egAttackBug = false;
    }
    if (version < 6) {
        uint32 sampleCount;
        ar(sampleCount);
    }
    if (version < 4) {
        uint32 currAddress;
        ar(currAddress);
    }
    ar(s.currSample, s.currPhase);
    if (version < 3) {
        s.currPhase >>= 4u;
    }
    if (version >= 3) {
        ar(s.nextPhase);
    }
    if (version >= 4) {
        ar(s.modulation);
    } else {
        s.modulation = 0;
    }
    ar(s.reverse, s.crossedLoopStart);
    ar(s.lfoCycles, s.lfoStep);
    if (version >= 3) {
        ar(s.alfoOutput);
    } else {
        s.alfoOutput = 0;
    }
    ar(s.sample1, s.sample2, s.output);
    if (version >= 4) {
        ar(s.finalLevel);
    } else {
        s.finalLevel = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, SCSPDSP &s, const uint32 version) {
    // v6:
    // - New fields
    //   - PC = 0x68
    //   - MIXSGen = 0
    //   - MIXStackNull = 0xFFFF
    // - Changed fields
    //   - TEMP entries changed from uint32 to sint32
    //   - MEMS entries changed from uint32 to sint32
    //   - MIXS increased from 16 to 16*2 entries
    //   - INPUTS changed from uint32 to sint32

    ar(s.MPRO, s.TEMP, s.MEMS, s.COEF, s.MADRS);
    if (version >= 6) {
        ar(s.MIXS, s.MIXSGen, s.MIXSNull);
    } else {
        std::array<sint32, 16> MIXS;
        ar(MIXS);
        std::copy_n(MIXS.begin(), MIXS.size(), s.MIXS.begin());
        std::fill(s.MIXS.begin() + MIXS.size(), s.MIXS.end(), 0);

        s.MIXSGen = 0;
        s.MIXSNull = 0xFFFF;
    }
    ar(s.EFREG, s.EXTS);
    ar(s.RBP, s.RBL);
    if (version >= 6) {
        ar(s.PC);
    } else {
        s.PC = 0x68;
    }
    ar(s.INPUTS);
    ar(s.SFT_REG, s.FRC_REG, s.Y_REG, s.ADRS_REG);
    ar(s.MDEC_CT);
    ar(s.readPending, s.readNOFL, s.readValue);
    ar(s.writePending, s.writeValue);
    ar(s.readWriteAddr);
}

template <class Archive>
void serialize(Archive &ar, SCSPTimer &s) {
    ar(s.incrementInterval);
    ar(s.reload);
    ar(s.doReload);
    ar(s.counter);
}

template <class Archive>
void serialize(Archive &ar, CDBlockState &s, const uint32 version) {
    // v9:
    // - New fields
    //   - seekTicks = 0
    //   - playEndPending = false
    //   - xferGetLength = getSectorLength
    //   - xferDelCount = (xferLength + getSectorLength - 1) / getSectorLength if xferType == GetThenDeleteSector,
    //     otherwise 0
    //   - reservedBuffers = 0
    // v8:
    // - New fields
    //   - fs
    // v5:
    // - New fields
    //   - enum CDBlockState::TransferType: added PutSector (= 6)
    //   - scratchBufferPutIndex = 0
    // - Removed fields
    //   - scratchBuffer moved into the buffers array

    ar(s.discHash);
    ar(s.CR, s.HIRQ, s.HIRQMASK);
    serialize(ar, s.status, version);
    ar(s.readyForPeriodicReports);
    ar(s.currDriveCycles, s.targetDriveCycles);
    if (version >= 9) {
        ar(s.seekTicks);
    } else {
        s.seekTicks = 0;
    }
    ar(s.playStartParam, s.playEndParam, s.playRepeatParam, s.scanDirection, s.scanCounter);
    ar(s.playStartPos, s.playEndPos, s.playMaxRepeat, s.playFile, s.bufferFullPause);
    if (version >= 9) {
        ar(s.playEndPending);
    } else {
        s.playEndPending = false;
    }
    ar(s.readSpeed);
    ar(s.discAuthStatus, s.mpegAuthStatus);
    ar(s.xferType, s.xferPos, s.xferLength, s.xferCount, s.xferBuffer, s.xferBufferPos);
    ar(s.xferSectorPos, s.xferSectorEnd, s.xferPartition);
    if (version >= 9) {
        ar(s.xferGetLength, s.xferDelCount);
        // Default value handled below, after reading getSectorLength
    }
    ar(s.xferSubcodeFrameAddress, s.xferSubcodeGroup);
    ar(s.xferExtraCount);
    if (version >= 5) {
        for (auto &buffer : s.buffers) {
            serialize(ar, buffer, version);
        }
        ar(s.scratchBufferPutIndex);
    } else {
        // scratchBuffer was moved into the buffers array immediately after the partition buffers
        auto buffers = std::make_unique<std::array<CDBlockState::BufferState, cdblock::kNumBuffers>>();
        auto scratchBuffer = std::make_unique<CDBlockState::BufferState>();
        for (auto &buffer : *buffers) {
            serialize(ar, buffer, version);
        }
        ar(*scratchBuffer);

        // Copy entire buffers array
        std::copy_n(buffers->begin(), cdblock::kNumBuffers, s.buffers.begin());

        // Find a place for the scratch buffer immediately after the partition buffers
        for (uint32 i = 0; i < s.buffers.size(); ++i) {
            if (s.buffers[i].partitionIndex == 0xFF) {
                s.buffers[i] = *scratchBuffer;
                break;
            }
        }

        s.scratchBufferPutIndex = 0;
    }
    if (version >= 9) {
        ar(s.reservedBuffers);
    } else {
        s.reservedBuffers = 0;
    }
    ar(s.filters);
    ar(s.cdDeviceConnection, s.lastCDWritePartition);
    ar(s.calculatedPartitionSize);
    ar(s.getSectorLength, s.putSectorLength);
    ar(s.processingCommand);
    serialize(ar, s.fs, version);

    if (version < 9) {
        s.xferGetLength = s.getSectorLength;
        if (s.xferType == CDBlockState::TransferType::GetThenDeleteSector) {
            s.xferDelCount = (s.xferLength + s.getSectorLength - 1) / s.getSectorLength;
        } else {
            s.xferDelCount = 0;
        }
    }
}

template <class Archive>
void serialize(Archive &ar, CDBlockState::StatusState &s, const uint32 version) {
    // v9:
    // - Changed fields
    //   - statusCode no longer includes flags (&= 0xF)

    ar(s.statusCode);
    ar(s.frameAddress);
    ar(s.flags);
    ar(s.repeatCount);
    ar(s.controlADR);
    ar(s.track);
    ar(s.index);

    if (version < 9) {
        s.statusCode &= 0xF;
    }
}

template <class Archive>
void serialize(Archive &ar, CDBlockState::BufferState &s, const uint32 version) {
    ar(s.data, s.size);
    ar(s.frameAddress);
    ar(s.fileNum, s.chanNum, s.submode, s.codingInfo);
    ar(s.partitionIndex);
}

template <class Archive>
void serialize(Archive &ar, CDBlockState::FilterState &s) {
    // v5:
    // - Changed fields:
    //   - trueOutput renamed to passOutput; no changes to value
    //   - falseOutput renamed to failOutput; no changes to value

    ar(s.startFrameAddress, s.frameAddressCount);
    ar(s.mode);
    ar(s.fileNum, s.chanNum);
    ar(s.submodeMask, s.submodeValue);
    ar(s.codingInfoMask, s.codingInfoValue);
    ar(s.passOutput, s.failOutput);
}

template <class Archive>
void serialize(Archive &ar, CDBlockState::FilesystemState &s, const uint32 version) {
    // v8:
    // - Struct newly introduced
    // - New fields:
    //   - currDirectory = 0; not much can be done since the information is completely missing from earlier versions
    //   - currFileOffset = 0

    if (version >= 8) {
        ar(s.currDirectory, s.currFileOffset);
    } else {
        s.currDirectory = 0;
        s.currFileOffset = 0;
    }
}

template <class Archive>
void serialize(Archive &ar, State &s, const uint32 version) {
    // v8:
    // - New fields:
    //   - uint64 msh2SpilloverCycles = 0
    // v5:
    // - New fields:
    //   - uint64 ssh2SpilloverCycles = 0

    // Reject version 0 and future versions
    if (version == 0 || version > kVersion) {
        return;
    }

    // NOTE: serialize is invoked manually here to handle versioned and non-versioned (pre-v4) variants
    serialize(ar, s.scheduler);
    serialize(ar, s.system);
    serialize(ar, s.msh2, version);
    serialize(ar, s.ssh2, version);
    serialize(ar, s.scu, version);
    serialize(ar, s.smpc, version);
    serialize(ar, s.vdp, version);
    serialize(ar, s.scsp, version);
    serialize(ar, s.cdblock, version);

    if (version >= 8) {
        ar(s.msh2SpilloverCycles);
    } else {
        s.msh2SpilloverCycles = 0;
    }
    if (version >= 5) {
        ar(s.ssh2SpilloverCycles);
    } else {
        s.ssh2SpilloverCycles = 0;
    }

    if (version < 5) {
        // Fixup FRT and WDT cycle counters which changed from local to global
        s.msh2.frt.cycleCount = s.scheduler.currCount - s.msh2.frt.cycleCount;
        s.msh2.wdt.cycleCount = s.scheduler.currCount - s.msh2.wdt.cycleCount;
        s.ssh2.frt.cycleCount = s.scheduler.currCount - s.ssh2.frt.cycleCount;
        s.ssh2.wdt.cycleCount = s.scheduler.currCount - s.ssh2.wdt.cycleCount;
    }
}

} // namespace ymir::state
