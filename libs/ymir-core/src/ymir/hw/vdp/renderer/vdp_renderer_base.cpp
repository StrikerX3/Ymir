#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

#include <ymir/util/dev_log.hpp>

namespace ymir::vdp {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   vdp2

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "Render";
    };

    struct vdp2 : public base {
        static constexpr std::string_view name = "Render-VDP2";
    };

} // namespace grp

void IVDPRenderer::Reset(bool hard) {
    for (auto &state : m_nbgLayerStates) {
        state.Reset();
    }
    for (auto &state : m_rotParamStates) {
        state.Reset();
    }
    for (auto &state : m_vramFetchers) {
        state[0].Reset();
        state[1].Reset();
    }
    m_lineBackLayerState.Reset();

    ResetImpl(hard);
}

void IVDPRenderer::VDP2CalcAccessPatterns(VDP2Regs &regs2) {
    if (!regs2.accessPatternsDirty) [[likely]] {
        return;
    }
    regs2.accessPatternsDirty = false;

    // Some games set up illegal access patterns that cause NBG2/NBG3 character pattern reads to be delayed, shifting
    // all graphics on those backgrounds one tile to the right.
    const bool hires = (regs2.TVMD.HRESOn & 6) != 0;

    // Clear bitmap delay flags
    for (uint32 bgIndex = 0; bgIndex < 4; ++bgIndex) {
        regs2.bgParams[bgIndex + 1].vramDataOffset.fill(0);
    }

    // Build access pattern masks for NBG0-3 PNs and CPs.
    // Bits 0-7 correspond to T0-T7.
    std::array<uint8, 4> pn = {0, 0, 0, 0}; // pattern name access masks
    std::array<uint8, 4> cp = {0, 0, 0, 0}; // character pattern access masks

    // First CP access timing slot per NBG. 0xFF means no accesses found.
    std::array<uint8, 4> firstCPAccessTiming = {0xFF, 0xFF, 0xFF, 0xFF};

    // First CP access VRAM chip per NBG. 0xFF means no accesses found.
    std::array<uint8, 4> firstCPAccessVRAMIndex = {0xFF, 0xFF, 0xFF, 0xFF};

    // First CP access found per NBG per bank.
    std::array<std::array<bool, 4>, 4> firstCPAccessFound = {{
        {false, false, false, false},
        {false, false, false, false},
        {false, false, false, false},
        {false, false, false, false},
    }};

    for (uint8 i = 0; i < 8; ++i) {
        for (uint8 bankIndex = 0; bankIndex < regs2.cyclePatterns.timings.size(); ++bankIndex) {
            const auto &bank = regs2.cyclePatterns.timings[bankIndex];
            if (bankIndex == 1 && !regs2.vramControl.partitionVRAMA) {
                continue;
            }
            if (bankIndex == 3 && !regs2.vramControl.partitionVRAMB) {
                continue;
            }

            const auto timing = bank[i];
            switch (timing) {
            case CyclePatterns::PatNameNBG0: [[fallthrough]];
            case CyclePatterns::PatNameNBG1: [[fallthrough]];
            case CyclePatterns::PatNameNBG2: [[fallthrough]];
            case CyclePatterns::PatNameNBG3: //
            {
                const uint8 bgIndex = static_cast<uint8>(timing) - static_cast<uint8>(CyclePatterns::PatNameNBG0);
                pn[bgIndex] |= 1u << i;
                break;
            }

            case CyclePatterns::CharPatNBG0: [[fallthrough]];
            case CyclePatterns::CharPatNBG1: [[fallthrough]];
            case CyclePatterns::CharPatNBG2: [[fallthrough]];
            case CyclePatterns::CharPatNBG3: //
            {
                const uint8 bgIndex = static_cast<uint8>(timing) - static_cast<uint8>(CyclePatterns::CharPatNBG0);
                cp[bgIndex] |= 1u << i;

                // TODO: find the correct rules for bitmap accesses
                //
                // Test cases:
                //
                // clang-format off
                //  # Res  ZM  Color  Bnk  CP mapping    Delay?  Game screen
                //  1 hi   1x  pal256  A   CP0 01..      no      Capcom Generation - Dai-5-shuu Kakutouka-tachi, art screens
                //                     B   CP0 ..23      yes     Capcom Generation - Dai-5-shuu Kakutouka-tachi, art screens
                //  2 hi   1x  pal256  B0  CP1 01..      no      3D Baseball, in-game (team nameplates during intro)
                //                     B1  CP1 ..23      no      3D Baseball, in-game (team nameplates during intro)
                //  3 hi   1x  pal256  A   CP0 01..      no      Doukyuusei - if, title screen
                //                     B   CP1 ..23      no      Doukyuusei - if, title screen
                //  4 hi   1x  pal256  A0  CP0 01..      no      Duke Nukem 3D, Netlink pages
                //                     A1  CP0 01..      no      Duke Nukem 3D, Netlink pages
                //                     B0  CP0 01..      no      Duke Nukem 3D, Netlink pages
                //                     B1  CP0 01..      no      Duke Nukem 3D, Netlink pages
                //  5 hi   1x  pal256  A   CP0 0123      no      Baroque Report, art screens
                //                     B   CP0 0123      no      Baroque Report, art screens
                //  6 hi   1x  pal256  A0  CP0 0123      no      Sonic Jam, art gallery
                //                     A1  CP0 0123      no      Sonic Jam, art gallery
                //                     B0  CP0 0123      no      Sonic Jam, art gallery
                //                     B1  CP0 0123      no      Sonic Jam, art gallery
                //  7 hi   1x  rgb555  A   CP0 0123      no      Steam Heart's, title screen
                //                     B   CP0 0123      no      Steam Heart's, title screen
                //  8 lo   1x  pal16       CP? 0123....  no      Groove on Fight, scrolling background in Options screen
                //  9 lo   1x  pal256      CP? 01......  no      Mr. Bones, in-game graphics
                // 10 lo   1x  pal256      CP? 01......  no      DoDonPachi, title screen background
                // 11 lo   1x  pal256      CP? 01......  no      Jung Rhythm, title screen
                // 12 lo   1x  pal256      CP? 01......  no      The Need for Speed, menus
                // 13 lo   1x  pal256      CP? ..23....  no      The Legend of Oasis, in-game HUD
                // 14 lo   1x  rgb555      CP? 0123....  no      Jung Rhythm, title screen
                // 15 lo   1x  rgb888      CP? 01234567  no      Street Fighter Zero 3, Capcom logo FMV
                // clang-format on
                //
                // Seems like the "delay" is caused by configuring out-of-phase reads for an NBG in different banks.
                // In case #1, CP0 is assigned to T0-T1 on bank A and T2-T3 on bank B. This is out of phase and on
                // different VRAM chips, so bank B reads are delayed.
                // In case #2, CP1 is assigned to T0-T1 on bank B0 and T2-T3 on bank B1. Despite being out of phase,
                // they're accessed on the same VRAM chip, so there is no delay.
                // In case #3 we have the same display settings but CP0 gets two cycles and CP1 gets two cycles.
                // These cause no "delay" because they're different NBGs.
                // Case #4 has no delay because all reads for the same NBG are assigned to the same cycle slot.
                // Cases #5 and #6 include more reads than necessary for the NBG, but because they all start on the same
                // slot, no delay occurs.

                // FIXME: bitmap delay seems to only apply to hi-res modes
                auto &bgParams = regs2.bgParams[bgIndex + 1];
                if (!bgParams.bitmap || hires) {
                    const uint8 vramIndex = bankIndex >> 1u;
                    if (firstCPAccessTiming[bgIndex] == 0xFF) {
                        firstCPAccessTiming[bgIndex] = i;
                        firstCPAccessVRAMIndex[bgIndex] = vramIndex;
                    } else if (!firstCPAccessFound[bgIndex][bankIndex] && i > firstCPAccessTiming[bgIndex] &&
                               vramIndex != firstCPAccessVRAMIndex[bgIndex]) {
                        bgParams.vramDataOffset[bankIndex] = 8u;
                    }
                    firstCPAccessFound[bgIndex][bankIndex] = true;
                }
                break;
            }

            default: break;
            }
        }

        // Stop at T3 if in hi-res mode
        if (hires && i == 3) {
            break;
        }
    }

    // Apply delays to the NBGs
    for (uint32 i = 0; i < 4; ++i) {
        auto &bgParams = regs2.bgParams[i + 1];
        bgParams.charPatDelay = false;
        const uint8 bgCP = cp[i];
        const uint8 bgPN = pn[i];

        // Skip bitmap NBGs as they're handled above
        if (bgParams.bitmap) {
            continue;
        }

        // Skip NBGs without any assigned accesses
        if (bgPN == 0 || bgCP == 0) {
            continue;
        }

        // Skip NBG0 and NBG1 if the pattern name access happens on T0
        if (i < 2 && bit::test<0>(bgPN)) {
            continue;
        }

        // Apply the delay
        if (bgPN == 0) {
            bgParams.charPatDelay = true;
        } else if (hires) {
            // Valid character pattern access masks per timing for high resolution modes
            static constexpr uint8 kPatterns[2][4] = {
                // 1x1 character patterns
                // T0      T1      T2      T3
                {0b0111, 0b1110, 0b1101, 0b1011},

                // 2x2 character patterns
                // T0      T1      T2      T3
                {0b0111, 0b1110, 0b1100, 0b1000},
            };

            for (uint8 pnIndex = 0; pnIndex < 4; ++pnIndex) {
                // Delay happens when either:
                // - CP access happens entirely before PN access
                // - CP access occurs in illegal time slot
                if ((bgPN & (1u << pnIndex)) != 0 &&
                    (bgCP < bgPN || (bgCP & kPatterns[bgParams.cellSizeShift][pnIndex]) != bgCP)) {
                    bgParams.charPatDelay = true;
                    break;
                }
            }
        } else {
            // Valid character pattern access masks per timing for normal resolution modes
            static constexpr uint8 kPatterns[8] = {
                //   T0          T1          T2          T3          T4          T5          T6          T7
                0b11110111, 0b11101111, 0b11001111, 0b10001111, 0b00001111, 0b00001110, 0b00001100, 0b00001000,
            };

            for (uint8 pnIndex = 0; pnIndex < 8; ++pnIndex) {
                if ((bgPN & (1u << pnIndex)) != 0) {
                    bgParams.charPatDelay = (bgCP & kPatterns[pnIndex]) == 0;
                    break;
                }
            }
        }
    }

    // Translate VRAM access cycles and rotation data bank selectors into read "permissions" for pattern name tables and
    // character pattern tables in each VRAM bank.
    const bool rbg0Enabled = regs2.bgEnabled[4];
    const bool rbg1Enabled = regs2.bgEnabled[5];

    for (uint32 bank = 0; bank < 4; ++bank) {
        const RotDataBankSel rotDataBankSel = regs2.vramControl.GetRotDataBankSel(bank);

        // RBG0
        if (rbg0Enabled && (!rbg1Enabled || bank < 2)) {
            regs2.bgParams[0].patNameAccess[bank] = rotDataBankSel == RotDataBankSel::PatternName;
            regs2.bgParams[0].charPatAccess[bank] = rotDataBankSel == RotDataBankSel::Character;
        } else {
            regs2.bgParams[0].patNameAccess[bank] = false;
            regs2.bgParams[0].charPatAccess[bank] = false;
        }

        // RBG1
        if (rbg1Enabled) {
            regs2.bgParams[1].patNameAccess[bank] = bank == 3;
            regs2.bgParams[1].charPatAccess[bank] = bank == 2;
        } else {
            regs2.bgParams[1].patNameAccess[bank] = false;
            regs2.bgParams[1].charPatAccess[bank] = false;
        }

        // NBG0-3
        for (uint32 nbg = 0; nbg < 4; ++nbg) {
            auto &bgParams = regs2.bgParams[nbg + 1];
            bgParams.patNameAccess[bank] = false;
            bgParams.charPatAccess[bank] = false;

            // Skip disabled NBGs
            if (!regs2.bgEnabled[nbg]) {
                continue;
            }
            // Skip NBGs 2 and 3 if RBG1 is enabled
            if (rbg1Enabled && bank >= 2u) {
                continue;
            }
            // Skip NBGs if RBG0 is enabled and the current bank is assigned to it
            if (rbg0Enabled && rotDataBankSel != RotDataBankSel::Unused) {
                continue;
            }

            // Determine how many character pattern accesses are needed for this NBG

            // Start with a base count of 1
            uint8 expectedCount = 1;

            // Apply ZMCTL modifiers
            // FIXME: Applying these disables background graphics in Baku Baku Animal - World Zookeeper
            /*if ((nbg == 0 && regs2.ZMCTL.N0ZMQT) || (nbg == 1 && regs2.ZMCTL.N1ZMQT)) {
                expectedCount *= 4;
            } else if ((nbg == 0 && regs2.ZMCTL.N0ZMHF) || (nbg == 1 && regs2.ZMCTL.N1ZMHF)) {
                expectedCount *= 2;
            }*/

            // Apply color format modifiers
            switch (bgParams.colorFormat) {
            case ColorFormat::Palette16: break;
            case ColorFormat::Palette256: expectedCount *= 2; break;
            case ColorFormat::Palette2048: expectedCount *= 4; break;
            case ColorFormat::RGB555: expectedCount *= 4; break;
            case ColorFormat::RGB888: expectedCount *= 8; break;
            }

            // Check for maximum 8 cycles on normal resolution, 4 cycles on high resolution/exclusive monitor modes
            const uint32 max = hires ? 4 : 8;
            if (expectedCount > max) [[unlikely]] {
                continue;
            }

            // Check that the background has the required number of accesses
            const uint8 numCPs = std::popcount(cp[nbg]);
            if (numCPs < expectedCount) {
                continue;
            }
            if constexpr (devlog::trace_enabled<grp::vdp2>) {
                if (numCPs > expectedCount) {
                    devlog::trace<grp::vdp2>("NBG{} has more CP accesses than needed ({} > {})", nbg, numCPs,
                                             expectedCount);
                }
            }

            // Enable pattern name and character pattern accesses for the bank
            for (uint32 index = 0; index < max; ++index) {
                const auto timing = regs2.cyclePatterns.timings[bank][index];
                if (timing == CyclePatterns::PatNameNBG0 + nbg) {
                    bgParams.patNameAccess[bank] = true;
                } else if (timing == CyclePatterns::CharPatNBG0 + nbg
                           // HACK: allow bitmap data access during SH-2 cycles. Probably wrong.
                           // Fixes flickering FMVs in Shin Kaitei Gunkan and Lunar - Silver Star Story
                           || (bgParams.bitmap && timing == CyclePatterns::CPU)) {
                    bgParams.charPatAccess[bank] = true;
                }
            }
        }
    }

    // Combine unpartitioned parameters
    if (!regs2.vramControl.partitionVRAMA) {
        for (uint32 i = 0; i < 5; i++) {
            regs2.bgParams[i].charPatAccess[1] = regs2.bgParams[i].charPatAccess[0];
            regs2.bgParams[i].patNameAccess[1] = regs2.bgParams[i].patNameAccess[0];
            regs2.bgParams[i].vramDataOffset[1] = regs2.bgParams[i].vramDataOffset[0];
        }
    }
    if (!regs2.vramControl.partitionVRAMB) {
        for (uint32 i = 0; i < 5; i++) {
            regs2.bgParams[i].charPatAccess[3] = regs2.bgParams[i].charPatAccess[2];
            regs2.bgParams[i].patNameAccess[3] = regs2.bgParams[i].patNameAccess[2];
            regs2.bgParams[i].vramDataOffset[3] = regs2.bgParams[i].vramDataOffset[2];
        }
    }

    // Translate VRAM access cycles for vertical cell scroll data into increment and offset for NBG0 and NBG1.
    //
    // Some games set up "illegal" access patterns which we have to honor. This is an approximation of the real
    // thing, since this VDP emulator does not actually perform the accesses described by the CYCxn registers.
    //
    // Vertical cell scroll reads are subject to a one-cycle delay if they happen on the following timing slots:
    //   NBG0: T3-T7
    //   NBG1: T4-T7

    m_vertCellScrollInc = 0;
    uint32 vcellAccessOffset = 0;

    // Update cycle accesses
    for (uint32 bank = 0; bank < 4; ++bank) {
        for (uint32 slotIndex = 0; slotIndex < 8; ++slotIndex) {
            const auto access = regs2.cyclePatterns.timings[bank][slotIndex];
            switch (access) {
            case CyclePatterns::VCellScrollNBG0:
                if (regs2.bgParams[1].verticalCellScrollEnable) {
                    m_vertCellScrollInc += sizeof(uint32);
                    m_nbgLayerStates[0].vertCellScrollOffset = vcellAccessOffset;
                    m_nbgLayerStates[0].vertCellScrollDelay = slotIndex >= 3;
                    m_nbgLayerStates[0].vertCellScrollRepeat = slotIndex >= 2;
                    vcellAccessOffset += sizeof(uint32);
                }
                break;
            case CyclePatterns::VCellScrollNBG1:
                if (regs2.bgParams[2].verticalCellScrollEnable) {
                    m_vertCellScrollInc += sizeof(uint32);
                    m_nbgLayerStates[1].vertCellScrollOffset = vcellAccessOffset;
                    m_nbgLayerStates[1].vertCellScrollDelay = slotIndex >= 3;
                    vcellAccessOffset += sizeof(uint32);
                }
                break;
            default: break;
            }
        }
    }

    // Apply access permissions for rotation coefficient data
    auto &vramCtl = regs2.vramControl;
    auto isCoeff = [](RotDataBankSel sel) { return sel == RotDataBankSel::Coefficients; };
    std::array<bool, 4> coeffAccess{};
    m_coeffAccess[0] = isCoeff(vramCtl.rotDataBankSelA0);
    m_coeffAccess[1] = isCoeff(vramCtl.partitionVRAMA ? vramCtl.rotDataBankSelA1 : vramCtl.rotDataBankSelA0);
    m_coeffAccess[2] = isCoeff(vramCtl.rotDataBankSelB0);
    m_coeffAccess[3] = isCoeff(vramCtl.partitionVRAMB ? vramCtl.rotDataBankSelB1 : vramCtl.rotDataBankSelB0);
}

void IVDPRenderer::VDP2UpdateEnabledBGs(const VDP2Regs &regs2, config::VDP2DebugRender &debugRenderOpts) {
    const auto &enabledLayers = debugRenderOpts.enabledLayers;

    // Sprite layer is always enabled, unless forcibly disabled
    m_layerEnabled[0] = enabledLayers[0];

    if (regs2.bgEnabled[4] && regs2.bgEnabled[5]) {
        m_layerEnabled[1] = enabledLayers[1]; // RBG0
        m_layerEnabled[2] = enabledLayers[2]; // RBG1
        m_layerEnabled[3] = false;            // EXBG
        m_layerEnabled[4] = false;            // not used
        m_layerEnabled[5] = false;            // not used
    } else {
        // Certain color format settings on NBG0 and NBG1 restrict which BG layers can be enabled
        // - NBG1 is disabled when NBG0 uses 8:8:8 RGB
        // - NBG2 is disabled when NBG0 uses 2048 color palette or any RGB format
        // - NBG3 is disabled when NBG0 uses 8:8:8 RGB or NBG1 uses 2048 color palette or 5:5:5 RGB color format
        // Additionally, NBG0 and RBG1 are mutually exclusive. If RBG1 is enabled, it takes place of NBG0.
        const ColorFormat colorFormatNBG0 = regs2.bgParams[1].colorFormat;
        const ColorFormat colorFormatNBG1 = regs2.bgParams[2].colorFormat;
        const bool disableNBG1 = colorFormatNBG0 == ColorFormat::RGB888;
        const bool disableNBG2 = colorFormatNBG0 == ColorFormat::Palette2048 ||
                                 colorFormatNBG0 == ColorFormat::RGB555 || colorFormatNBG0 == ColorFormat::RGB888;
        const bool disableNBG3 = colorFormatNBG0 == ColorFormat::RGB888 ||
                                 colorFormatNBG1 == ColorFormat::Palette2048 || colorFormatNBG1 == ColorFormat::RGB555;

        m_layerEnabled[1] = enabledLayers[1] && regs2.bgEnabled[4];                         // RBG0
        m_layerEnabled[2] = enabledLayers[2] && (regs2.bgEnabled[0] || regs2.bgEnabled[5]); // NBG0/RBG1
        m_layerEnabled[3] = enabledLayers[3] && regs2.bgEnabled[1] && !disableNBG1;         // NBG1/EXBG
        m_layerEnabled[4] = enabledLayers[4] && regs2.bgEnabled[2] && !disableNBG2;         // NBG2
        m_layerEnabled[5] = enabledLayers[5] && regs2.bgEnabled[3] && !disableNBG3;         // NBG3
    }
}

} // namespace ymir::vdp
