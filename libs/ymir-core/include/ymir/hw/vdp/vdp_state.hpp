#pragma once

#include "vdp1_regs.hpp"
#include "vdp2_regs.hpp"
#include "vdp_defs.hpp"

#include <ymir/state/state_vdp.hpp>

#include <array>

namespace ymir::vdp {

/// @brief Contains the entire state of the VDP1 and VDP2.
struct VDPState {
    VDPState() {
        Reset(true);
    }

    /// @brief Performs a soft or hard reset of the state.
    /// @param hard whether to do a hard (`true`) or soft (`false`) reset
    void Reset(bool hard) {
        if (hard) {
            for (uint32 addr = 0; addr < VRAM1.size(); addr++) {
                if ((addr & 0x1F) == 0) {
                    VRAM1[addr] = 0x80;
                } else if ((addr & 0x1F) == 1) {
                    VRAM1[addr] = 0x00;
                } else if ((addr & 2) == 2) {
                    VRAM1[addr] = 0x55;
                } else {
                    VRAM1[addr] = 0xAA;
                }
            }

            VRAM2.fill(0);
            CRAM.fill(0);
            for (auto &fb : spriteFB) {
                fb.fill(0);
            }
            displayFB = 0;
        }

        regs1.Reset();
        regs2.Reset();

        HPhase = HorizontalPhase::Active;
        VPhase = VerticalPhase::Active;
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::VDPState &state) const {
        state.VRAM1 = VRAM1;
        state.VRAM2 = VRAM2;
        state.CRAM = CRAM;
        state.spriteFB = spriteFB;
        state.displayFB = displayFB;

        state.regs1.TVMR = regs1.ReadTVMR();
        state.regs1.FBCR = regs1.ReadFBCR();
        state.regs1.PTMR = regs1.ReadPTMR();
        state.regs1.EWDR = regs1.ReadEWDR();
        state.regs1.EWLR = regs1.ReadEWLR();
        state.regs1.EWRR = regs1.ReadEWRR();
        state.regs1.EDSR = regs1.ReadEDSR();
        state.regs1.LOPR = regs1.ReadLOPR();
        state.regs1.COPR = regs1.ReadCOPR();
        state.regs1.MODR = regs1.ReadMODR();

        state.VDP1FBCRChanged = regs1.fbParamsChanged;

        state.regs2.TVMD = regs2.ReadTVMD();
        state.regs2.EXTEN = regs2.ReadEXTEN();
        state.regs2.TVSTAT = regs2.ReadTVSTAT<true>();
        state.regs2.VRSIZE = regs2.ReadVRSIZE();
        state.regs2.HCNT = regs2.ReadHCNT();
        state.regs2.VCNT = regs2.ReadVCNT();
        state.regs2.RAMCTL = regs2.ReadRAMCTL();
        state.regs2.CYCA0L = regs2.ReadCYCA0L();
        state.regs2.CYCA0U = regs2.ReadCYCA0U();
        state.regs2.CYCA1L = regs2.ReadCYCA1L();
        state.regs2.CYCA1U = regs2.ReadCYCA1U();
        state.regs2.CYCB0L = regs2.ReadCYCB0L();
        state.regs2.CYCB0U = regs2.ReadCYCB0U();
        state.regs2.CYCB1L = regs2.ReadCYCB1L();
        state.regs2.CYCB1U = regs2.ReadCYCB1U();
        state.regs2.BGON = regs2.ReadBGON();
        state.regs2.MZCTL = regs2.ReadMZCTL();
        state.regs2.SFSEL = regs2.ReadSFSEL();
        state.regs2.SFCODE = regs2.ReadSFCODE();
        state.regs2.CHCTLA = regs2.ReadCHCTLA();
        state.regs2.CHCTLB = regs2.ReadCHCTLB();
        state.regs2.BMPNA = regs2.ReadBMPNA();
        state.regs2.BMPNB = regs2.ReadBMPNB();
        state.regs2.PNCNA = regs2.ReadPNCNA();
        state.regs2.PNCNB = regs2.ReadPNCNB();
        state.regs2.PNCNC = regs2.ReadPNCNC();
        state.regs2.PNCND = regs2.ReadPNCND();
        state.regs2.PNCR = regs2.ReadPNCR();
        state.regs2.PLSZ = regs2.ReadPLSZ();
        state.regs2.MPOFN = regs2.ReadMPOFN();
        state.regs2.MPOFR = regs2.ReadMPOFR();
        state.regs2.MPABN0 = regs2.ReadMPABN0();
        state.regs2.MPCDN0 = regs2.ReadMPCDN0();
        state.regs2.MPABN1 = regs2.ReadMPABN1();
        state.regs2.MPCDN1 = regs2.ReadMPCDN1();
        state.regs2.MPABN2 = regs2.ReadMPABN2();
        state.regs2.MPCDN2 = regs2.ReadMPCDN2();
        state.regs2.MPABN3 = regs2.ReadMPABN3();
        state.regs2.MPCDN3 = regs2.ReadMPCDN3();
        state.regs2.MPABRA = regs2.ReadMPABRA();
        state.regs2.MPCDRA = regs2.ReadMPCDRA();
        state.regs2.MPEFRA = regs2.ReadMPEFRA();
        state.regs2.MPGHRA = regs2.ReadMPGHRA();
        state.regs2.MPIJRA = regs2.ReadMPIJRA();
        state.regs2.MPKLRA = regs2.ReadMPKLRA();
        state.regs2.MPMNRA = regs2.ReadMPMNRA();
        state.regs2.MPOPRA = regs2.ReadMPOPRA();
        state.regs2.MPABRB = regs2.ReadMPABRB();
        state.regs2.MPCDRB = regs2.ReadMPCDRB();
        state.regs2.MPEFRB = regs2.ReadMPEFRB();
        state.regs2.MPGHRB = regs2.ReadMPGHRB();
        state.regs2.MPIJRB = regs2.ReadMPIJRB();
        state.regs2.MPKLRB = regs2.ReadMPKLRB();
        state.regs2.MPMNRB = regs2.ReadMPMNRB();
        state.regs2.MPOPRB = regs2.ReadMPOPRB();
        state.regs2.SCXIN0 = regs2.ReadSCXIN0();
        state.regs2.SCXDN0 = regs2.ReadSCXDN0();
        state.regs2.SCYIN0 = regs2.ReadSCYIN0();
        state.regs2.SCYDN0 = regs2.ReadSCYDN0();
        state.regs2.ZMXIN0 = regs2.ReadZMXIN0();
        state.regs2.ZMXDN0 = regs2.ReadZMXDN0();
        state.regs2.ZMYIN0 = regs2.ReadZMYIN0();
        state.regs2.ZMYDN0 = regs2.ReadZMYDN0();
        state.regs2.SCXIN1 = regs2.ReadSCXIN1();
        state.regs2.SCXDN1 = regs2.ReadSCXDN1();
        state.regs2.SCYIN1 = regs2.ReadSCYIN1();
        state.regs2.SCYDN1 = regs2.ReadSCYDN1();
        state.regs2.ZMXIN1 = regs2.ReadZMXIN1();
        state.regs2.ZMXDN1 = regs2.ReadZMXDN1();
        state.regs2.ZMYIN1 = regs2.ReadZMYIN1();
        state.regs2.ZMYDN1 = regs2.ReadZMYDN1();
        state.regs2.SCXIN2 = regs2.ReadSCXN2();
        state.regs2.SCYIN2 = regs2.ReadSCYN2();
        state.regs2.SCXIN3 = regs2.ReadSCXN3();
        state.regs2.SCYIN3 = regs2.ReadSCYN3();
        state.regs2.ZMCTL = regs2.ReadZMCTL();
        state.regs2.SCRCTL = regs2.ReadSCRCTL();
        state.regs2.VCSTAU = regs2.ReadVCSTAU();
        state.regs2.VCSTAL = regs2.ReadVCSTAL();
        state.regs2.LSTA0U = regs2.ReadLSTA0U();
        state.regs2.LSTA0L = regs2.ReadLSTA0L();
        state.regs2.LSTA1U = regs2.ReadLSTA1U();
        state.regs2.LSTA1L = regs2.ReadLSTA1L();
        state.regs2.LCTAU = regs2.ReadLCTAU();
        state.regs2.LCTAL = regs2.ReadLCTAL();
        state.regs2.BKTAU = regs2.ReadBKTAU();
        state.regs2.BKTAL = regs2.ReadBKTAL();
        state.regs2.RPMD = regs2.ReadRPMD();
        state.regs2.RPRCTL = regs2.ReadRPRCTL();
        state.regs2.KTCTL = regs2.ReadKTCTL();
        state.regs2.KTAOF = regs2.ReadKTAOF();
        state.regs2.OVPNRA = regs2.ReadOVPNRA();
        state.regs2.OVPNRB = regs2.ReadOVPNRB();
        state.regs2.RPTAU = regs2.ReadRPTAU();
        state.regs2.RPTAL = regs2.ReadRPTAL();
        state.regs2.WPSX0 = regs2.ReadWPSX0();
        state.regs2.WPSY0 = regs2.ReadWPSY0();
        state.regs2.WPEX0 = regs2.ReadWPEX0();
        state.regs2.WPEY0 = regs2.ReadWPEY0();
        state.regs2.WPSX1 = regs2.ReadWPSX1();
        state.regs2.WPSY1 = regs2.ReadWPSY1();
        state.regs2.WPEX1 = regs2.ReadWPEX1();
        state.regs2.WPEY1 = regs2.ReadWPEY1();
        state.regs2.WCTLA = regs2.ReadWCTLA();
        state.regs2.WCTLB = regs2.ReadWCTLB();
        state.regs2.WCTLC = regs2.ReadWCTLC();
        state.regs2.WCTLD = regs2.ReadWCTLD();
        state.regs2.LWTA0U = regs2.ReadLWTA0U();
        state.regs2.LWTA0L = regs2.ReadLWTA0L();
        state.regs2.LWTA1U = regs2.ReadLWTA1U();
        state.regs2.LWTA1L = regs2.ReadLWTA1L();
        state.regs2.SPCTL = regs2.ReadSPCTL();
        state.regs2.SDCTL = regs2.ReadSDCTL();
        state.regs2.CRAOFA = regs2.ReadCRAOFA();
        state.regs2.CRAOFB = regs2.ReadCRAOFB();
        state.regs2.LNCLEN = regs2.ReadLNCLEN();
        state.regs2.SFPRMD = regs2.ReadSFPRMD();
        state.regs2.CCCTL = regs2.ReadCCCTL();
        state.regs2.SFCCMD = regs2.ReadSFCCMD();
        state.regs2.PRISA = regs2.ReadPRISA();
        state.regs2.PRISB = regs2.ReadPRISB();
        state.regs2.PRISC = regs2.ReadPRISC();
        state.regs2.PRISD = regs2.ReadPRISD();
        state.regs2.PRINA = regs2.ReadPRINA();
        state.regs2.PRINB = regs2.ReadPRINB();
        state.regs2.PRIR = regs2.ReadPRIR();
        state.regs2.CCRSA = regs2.ReadCCRSA();
        state.regs2.CCRSB = regs2.ReadCCRSB();
        state.regs2.CCRSC = regs2.ReadCCRSC();
        state.regs2.CCRSD = regs2.ReadCCRSD();
        state.regs2.CCRNA = regs2.ReadCCRNA();
        state.regs2.CCRNB = regs2.ReadCCRNB();
        state.regs2.CCRR = regs2.ReadCCRR();
        state.regs2.CCRLB = regs2.ReadCCRLB();
        state.regs2.CLOFEN = regs2.ReadCLOFEN();
        state.regs2.CLOFSL = regs2.ReadCLOFSL();
        state.regs2.COAR = regs2.ReadCOAR();
        state.regs2.COAG = regs2.ReadCOAG();
        state.regs2.COAB = regs2.ReadCOAB();
        state.regs2.COBR = regs2.ReadCOBR();
        state.regs2.COBG = regs2.ReadCOBG();
        state.regs2.COBB = regs2.ReadCOBB();

        state.VDP2VCNTLatch = regs2.VCNTLatch;
        state.VDP2VCNTLatched = regs2.VCNTLatched;

        switch (HPhase) {
        default:
        case HorizontalPhase::Active: state.HPhase = state::VDPState::HorizontalPhase::Active; break;
        case HorizontalPhase::RightBorder: state.HPhase = state::VDPState::HorizontalPhase::RightBorder; break;
        case HorizontalPhase::Sync: state.HPhase = state::VDPState::HorizontalPhase::Sync; break;
        case HorizontalPhase::LeftBorder: state.HPhase = state::VDPState::HorizontalPhase::LeftBorder; break;
        }

        switch (VPhase) {
        default:
        case VerticalPhase::Active: state.VPhase = state::VDPState::VerticalPhase::Active; break;
        case VerticalPhase::BottomBorder: state.VPhase = state::VDPState::VerticalPhase::BottomBorder; break;
        case VerticalPhase::BlankingAndSync: state.VPhase = state::VDPState::VerticalPhase::BlankingAndSync; break;
        case VerticalPhase::VCounterSkip: state.VPhase = state::VDPState::VerticalPhase::VCounterSkip; break;
        case VerticalPhase::TopBorder: state.VPhase = state::VDPState::VerticalPhase::TopBorder; break;
        case VerticalPhase::LastLine: state.VPhase = state::VDPState::VerticalPhase::LastLine; break;
        }
    }

    [[nodiscard]] bool ValidateState(const state::VDPState &state) const {
        switch (state.HPhase) {
        case state::VDPState::HorizontalPhase::Active: break;
        case state::VDPState::HorizontalPhase::RightBorder: break;
        case state::VDPState::HorizontalPhase::Sync: break;
        case state::VDPState::HorizontalPhase::LeftBorder: break;
        default: return false;
        }

        switch (state.VPhase) {
        case state::VDPState::VerticalPhase::Active: break;
        case state::VDPState::VerticalPhase::BottomBorder: break;
        case state::VDPState::VerticalPhase::BlankingAndSync: break;
        case state::VDPState::VerticalPhase::VCounterSkip: break;
        case state::VDPState::VerticalPhase::TopBorder: break;
        case state::VDPState::VerticalPhase::LastLine: break;
        default: return false;
        }

        return true;
    }

    void LoadState(const state::VDPState &state) {
        VRAM1 = state.VRAM1;
        VRAM2 = state.VRAM2;
        CRAM = state.CRAM;
        spriteFB = state.spriteFB;
        displayFB = state.displayFB;

        regs1.WriteTVMR(state.regs1.TVMR);
        regs1.WriteFBCR(state.regs1.FBCR);
        regs1.WritePTMR(state.regs1.PTMR);
        regs1.WriteEWDR(state.regs1.EWDR);
        regs1.WriteEWLR(state.regs1.EWLR);
        regs1.WriteEWRR(state.regs1.EWRR);
        regs1.WriteEDSR(state.regs1.EDSR);
        regs1.WriteLOPR(state.regs1.LOPR);
        regs1.WriteCOPR(state.regs1.COPR);
        regs1.WriteMODR(state.regs1.MODR);

        regs1.fbParamsChanged = state.VDP1FBCRChanged;

        regs2.WriteTVMD(state.regs2.TVMD);
        regs2.WriteEXTEN(state.regs2.EXTEN);
        regs2.WriteTVSTAT(state.regs2.TVSTAT);
        regs2.WriteVRSIZE(state.regs2.VRSIZE);
        regs2.WriteHCNT(state.regs2.HCNT);
        regs2.WriteVCNT(state.regs2.VCNT);
        regs2.WriteRAMCTL(state.regs2.RAMCTL);
        regs2.WriteCYCA0L(state.regs2.CYCA0L);
        regs2.WriteCYCA0U(state.regs2.CYCA0U);
        regs2.WriteCYCA1L(state.regs2.CYCA1L);
        regs2.WriteCYCA1U(state.regs2.CYCA1U);
        regs2.WriteCYCB0L(state.regs2.CYCB0L);
        regs2.WriteCYCB0U(state.regs2.CYCB0U);
        regs2.WriteCYCB1L(state.regs2.CYCB1L);
        regs2.WriteCYCB1U(state.regs2.CYCB1U);
        regs2.WriteBGON(state.regs2.BGON);
        regs2.WriteMZCTL(state.regs2.MZCTL);
        regs2.WriteSFSEL(state.regs2.SFSEL);
        regs2.WriteSFCODE(state.regs2.SFCODE);
        regs2.WriteCHCTLA(state.regs2.CHCTLA);
        regs2.WriteCHCTLB(state.regs2.CHCTLB);
        regs2.WriteBMPNA(state.regs2.BMPNA);
        regs2.WriteBMPNB(state.regs2.BMPNB);
        regs2.WritePNCNA(state.regs2.PNCNA);
        regs2.WritePNCNB(state.regs2.PNCNB);
        regs2.WritePNCNC(state.regs2.PNCNC);
        regs2.WritePNCND(state.regs2.PNCND);
        regs2.WritePNCR(state.regs2.PNCR);
        regs2.WritePLSZ(state.regs2.PLSZ);
        regs2.WriteMPOFN(state.regs2.MPOFN);
        regs2.WriteMPOFR(state.regs2.MPOFR);
        regs2.WriteMPABN0(state.regs2.MPABN0);
        regs2.WriteMPCDN0(state.regs2.MPCDN0);
        regs2.WriteMPABN1(state.regs2.MPABN1);
        regs2.WriteMPCDN1(state.regs2.MPCDN1);
        regs2.WriteMPABN2(state.regs2.MPABN2);
        regs2.WriteMPCDN2(state.regs2.MPCDN2);
        regs2.WriteMPABN3(state.regs2.MPABN3);
        regs2.WriteMPCDN3(state.regs2.MPCDN3);
        regs2.WriteMPABRA(state.regs2.MPABRA);
        regs2.WriteMPCDRA(state.regs2.MPCDRA);
        regs2.WriteMPEFRA(state.regs2.MPEFRA);
        regs2.WriteMPGHRA(state.regs2.MPGHRA);
        regs2.WriteMPIJRA(state.regs2.MPIJRA);
        regs2.WriteMPKLRA(state.regs2.MPKLRA);
        regs2.WriteMPMNRA(state.regs2.MPMNRA);
        regs2.WriteMPOPRA(state.regs2.MPOPRA);
        regs2.WriteMPABRB(state.regs2.MPABRB);
        regs2.WriteMPCDRB(state.regs2.MPCDRB);
        regs2.WriteMPEFRB(state.regs2.MPEFRB);
        regs2.WriteMPGHRB(state.regs2.MPGHRB);
        regs2.WriteMPIJRB(state.regs2.MPIJRB);
        regs2.WriteMPKLRB(state.regs2.MPKLRB);
        regs2.WriteMPMNRB(state.regs2.MPMNRB);
        regs2.WriteMPOPRB(state.regs2.MPOPRB);
        regs2.WriteSCXIN0(state.regs2.SCXIN0);
        regs2.WriteSCXDN0(state.regs2.SCXDN0);
        regs2.WriteSCYIN0(state.regs2.SCYIN0);
        regs2.WriteSCYDN0(state.regs2.SCYDN0);
        regs2.WriteZMXIN0(state.regs2.ZMXIN0);
        regs2.WriteZMXDN0(state.regs2.ZMXDN0);
        regs2.WriteZMYIN0(state.regs2.ZMYIN0);
        regs2.WriteZMYDN0(state.regs2.ZMYDN0);
        regs2.WriteSCXIN1(state.regs2.SCXIN1);
        regs2.WriteSCXDN1(state.regs2.SCXDN1);
        regs2.WriteSCYIN1(state.regs2.SCYIN1);
        regs2.WriteSCYDN1(state.regs2.SCYDN1);
        regs2.WriteZMXIN1(state.regs2.ZMXIN1);
        regs2.WriteZMXDN1(state.regs2.ZMXDN1);
        regs2.WriteZMYIN1(state.regs2.ZMYIN1);
        regs2.WriteZMYDN1(state.regs2.ZMYDN1);
        regs2.WriteSCXN2(state.regs2.SCXIN2);
        regs2.WriteSCYN2(state.regs2.SCYIN2);
        regs2.WriteSCXN3(state.regs2.SCXIN3);
        regs2.WriteSCYN3(state.regs2.SCYIN3);
        regs2.WriteZMCTL(state.regs2.ZMCTL);
        regs2.WriteSCRCTL(state.regs2.SCRCTL);
        regs2.WriteVCSTAU(state.regs2.VCSTAU);
        regs2.WriteVCSTAL(state.regs2.VCSTAL);
        regs2.WriteLSTA0U(state.regs2.LSTA0U);
        regs2.WriteLSTA0L(state.regs2.LSTA0L);
        regs2.WriteLSTA1U(state.regs2.LSTA1U);
        regs2.WriteLSTA1L(state.regs2.LSTA1L);
        regs2.WriteLCTAU(state.regs2.LCTAU);
        regs2.WriteLCTAL(state.regs2.LCTAL);
        regs2.WriteBKTAU(state.regs2.BKTAU);
        regs2.WriteBKTAL(state.regs2.BKTAL);
        regs2.WriteRPMD(state.regs2.RPMD);
        regs2.WriteRPRCTL(state.regs2.RPRCTL);
        regs2.WriteKTCTL(state.regs2.KTCTL);
        regs2.WriteKTAOF(state.regs2.KTAOF);
        regs2.WriteOVPNRA(state.regs2.OVPNRA);
        regs2.WriteOVPNRB(state.regs2.OVPNRB);
        regs2.WriteRPTAU(state.regs2.RPTAU);
        regs2.WriteRPTAL(state.regs2.RPTAL);
        regs2.WriteWPSX0(state.regs2.WPSX0);
        regs2.WriteWPSY0(state.regs2.WPSY0);
        regs2.WriteWPEX0(state.regs2.WPEX0);
        regs2.WriteWPEY0(state.regs2.WPEY0);
        regs2.WriteWPSX1(state.regs2.WPSX1);
        regs2.WriteWPSY1(state.regs2.WPSY1);
        regs2.WriteWPEX1(state.regs2.WPEX1);
        regs2.WriteWPEY1(state.regs2.WPEY1);
        regs2.WriteWCTLA(state.regs2.WCTLA);
        regs2.WriteWCTLB(state.regs2.WCTLB);
        regs2.WriteWCTLC(state.regs2.WCTLC);
        regs2.WriteWCTLD(state.regs2.WCTLD);
        regs2.WriteLWTA0U(state.regs2.LWTA0U);
        regs2.WriteLWTA0L(state.regs2.LWTA0L);
        regs2.WriteLWTA1U(state.regs2.LWTA1U);
        regs2.WriteLWTA1L(state.regs2.LWTA1L);
        regs2.WriteSPCTL(state.regs2.SPCTL);
        regs2.WriteSDCTL(state.regs2.SDCTL);
        regs2.WriteCRAOFA(state.regs2.CRAOFA);
        regs2.WriteCRAOFB(state.regs2.CRAOFB);
        regs2.WriteLNCLEN(state.regs2.LNCLEN);
        regs2.WriteSFPRMD(state.regs2.SFPRMD);
        regs2.WriteCCCTL(state.regs2.CCCTL);
        regs2.WriteSFCCMD(state.regs2.SFCCMD);
        regs2.WritePRISA(state.regs2.PRISA);
        regs2.WritePRISB(state.regs2.PRISB);
        regs2.WritePRISC(state.regs2.PRISC);
        regs2.WritePRISD(state.regs2.PRISD);
        regs2.WritePRINA(state.regs2.PRINA);
        regs2.WritePRINB(state.regs2.PRINB);
        regs2.WritePRIR(state.regs2.PRIR);
        regs2.WriteCCRSA(state.regs2.CCRSA);
        regs2.WriteCCRSB(state.regs2.CCRSB);
        regs2.WriteCCRSC(state.regs2.CCRSC);
        regs2.WriteCCRSD(state.regs2.CCRSD);
        regs2.WriteCCRNA(state.regs2.CCRNA);
        regs2.WriteCCRNB(state.regs2.CCRNB);
        regs2.WriteCCRR(state.regs2.CCRR);
        regs2.WriteCCRLB(state.regs2.CCRLB);
        regs2.WriteCLOFEN(state.regs2.CLOFEN);
        regs2.WriteCLOFSL(state.regs2.CLOFSL);
        regs2.WriteCOAR(state.regs2.COAR);
        regs2.WriteCOAG(state.regs2.COAG);
        regs2.WriteCOAB(state.regs2.COAB);
        regs2.WriteCOBR(state.regs2.COBR);
        regs2.WriteCOBG(state.regs2.COBG);
        regs2.WriteCOBB(state.regs2.COBB);

        regs2.VCNTLatch = state.VDP2VCNTLatch;
        regs2.VCNTLatched = state.VDP2VCNTLatched;

        regs2.accessPatternsDirty = true;

        switch (state.HPhase) {
        default:
        case state::VDPState::HorizontalPhase::Active: HPhase = HorizontalPhase::Active; break;
        case state::VDPState::HorizontalPhase::RightBorder: HPhase = HorizontalPhase::RightBorder; break;
        case state::VDPState::HorizontalPhase::Sync: HPhase = HorizontalPhase::Sync; break;
        case state::VDPState::HorizontalPhase::LeftBorder: HPhase = HorizontalPhase::LeftBorder; break;
        }

        switch (state.VPhase) {
        default:
        case state::VDPState::VerticalPhase::Active: VPhase = VerticalPhase::Active; break;
        case state::VDPState::VerticalPhase::BottomBorder: VPhase = VerticalPhase::BottomBorder; break;
        case state::VDPState::VerticalPhase::BlankingAndSync: VPhase = VerticalPhase::BlankingAndSync; break;
        case state::VDPState::VerticalPhase::VCounterSkip: VPhase = VerticalPhase::VCounterSkip; break;
        case state::VDPState::VerticalPhase::TopBorder: VPhase = VerticalPhase::TopBorder; break;
        case state::VDPState::VerticalPhase::LastLine: VPhase = VerticalPhase::LastLine; break;
        }
    }

    // -------------------------------------------------------------------------
    // Memory

    alignas(16) std::array<uint8, kVDP1VRAMSize> VRAM1;
    alignas(16) std::array<uint8, kVDP2VRAMSize> VRAM2; // 4x 128 KiB banks: A0, A1, B0, B1
    alignas(16) std::array<uint8, kVDP2CRAMSize> CRAM;
    alignas(16) std::array<SpriteFB, 2> spriteFB;
    uint8 displayFB; // index of current sprite display buffer, CPU-accessible; opposite buffer is drawn into

    // -------------------------------------------------------------------------
    // Registers

    VDP1Regs regs1;
    VDP2Regs regs2;

    // -------------------------------------------------------------------------
    // Timings and signals

    // Based on https://github.com/srg320/Saturn_hw/blob/main/VDP2/VDP2.xlsx

    // Horizontal display phases:
    // NOTE: each dot takes 4 system (SH-2) cycles on standard resolutions, 2 system cycles on hi-res modes
    // NOTE: hi-res modes doubles all HCNTs
    //
    //   320 352  dots
    // --------------------------------
    //     0   0  Active display area
    //   320 352  Right border
    //   347 375  Horizontal sync
    //   374 403  VBlank OUT
    //   400 432  Left border
    //   426 454  Last dot
    //   427 455  Total HCNT
    //
    // Vertical display phases:
    // NOTE: bottom blanking, vertical sync and top blanking are consolidated into a single phase since no important
    // events happen other than not drawing the border
    //
    //    NTSC    --  PAL  --
    //   224 240  224 240 256  lines
    // ---------------------------------------------
    //     0   0    0   0   0  Active display area
    //   224 240  224 240 256  Bottom border
    //   232 240  256 264 272  Bottom blanking | these are
    //   237 245  259 267 275  Vertical sync   | merged into
    //   240 248  262 270 278  Top blanking    | one phase
    //   255 255  281 289 297  Top border
    //   262 262  312 312 312  Last line
    //   263 263  313 313 313  Total VCNT
    //
    // Events:
    //   VBLANK signal is raised when entering bottom border V phase
    //   VBLANK signal is lowered when entering VBlank clear H phase during last line V phase
    //
    //   HBLANK signal is raised when entering right border H phase (closest match, 4 cycles early)
    //   HBLANK signal is lowered when entering left border H phase (closest match, 10 cycles early)
    //
    //   Even/odd field flag is flipped when entering last dot H phase during first line of bottom border V phase
    //
    //   VBlank IN/OUT interrupts are raised when the VBLANK signal is raised/lowered
    //   HBlank IN interrupt is raised when the HBLANK signal is raised
    //
    //   Drawing happens when in both active display area phases
    //   Border drawing happens when in any of the border phases

    HorizontalPhase HPhase;
    VerticalPhase VPhase;

    // TODO: store latched H/V counters
};

} // namespace ymir::vdp
