#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <ymir/hw/vdp/vdp_state.hpp>

#include <array>

using namespace ymir;

struct TestSubject {
    mutable vdp::VDP2Regs regs2;
    mutable vdp::VDP2State state2;
    vdp::config::VDP2AccessPatternsConfig accPatCfg;
};

struct NBGData {
    bool check = false;

    bool bitmap;
    std::array<bool, 4> patNameAccess;
    std::array<bool, 4> charPatAccess;
    std::array<bool, 4> charPatDelay;
    std::array<uint32, 4> vramDataOffset;
    bool vcellScrollDelay;
    bool vcellScrollRepeat;
    uint8 vcellScrollOffset;
};

struct TestData {
    const char *name = "Defaults";

    uint32 CYCA0LU = 0x0000; // 010 012
    uint32 CYCA1LU = 0x0000; // 014 016
    uint32 CYCB0LU = 0x0000; // 018 01A
    uint32 CYCB1LU = 0x0000; // 01C 01E
    uint16 RAMCTL = 0x0000;  // 00E
    uint16 TVMD = 0x0000;    // 000
    uint16 BGON = 0x0000;    // 020
    uint16 CHCTLA = 0x0000;  // 028
    uint16 CHCTLB = 0x0000;  // 02A
    uint16 ZMCTL = 0x0000;   // 098
    uint16 SCRCTL = 0x0000;  // 09A

    std::array<NBGData, 4> nbgs{};
    uint8 vcellScrollInc = 0;
    std::array<bool, 4> coeffAccess{};
};

constexpr auto testdata = {
    TestData{},
#include "vdp_vram_access_patterns_testdata.inc"
};

// -----------------------------------------------------------------------------

TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "VDP2 VRAM access patterns tests", "[vdp][vram][access_patterns]") {
    regs2.Reset();
    state2.Reset();

    const auto &testData = GENERATE(values<TestData>(testdata));

    SECTION(testData.name) {
        regs2.WriteCYCA0L(testData.CYCA0LU >> 16u);
        regs2.WriteCYCA0U(testData.CYCA0LU >> 0u);
        regs2.WriteCYCA1L(testData.CYCA1LU >> 16u);
        regs2.WriteCYCA1U(testData.CYCA1LU >> 0u);
        regs2.WriteCYCB0L(testData.CYCB0LU >> 16u);
        regs2.WriteCYCB0U(testData.CYCB0LU >> 0u);
        regs2.WriteCYCB1L(testData.CYCB1LU >> 16u);
        regs2.WriteCYCB1U(testData.CYCB1LU >> 0u);
        regs2.WriteRAMCTL(testData.RAMCTL);
        regs2.WriteTVMD(testData.TVMD);
        regs2.WriteBGON(testData.BGON);
        regs2.WriteCHCTLA(testData.CHCTLA);
        regs2.WriteCHCTLB(testData.CHCTLB);
        regs2.WriteZMCTL(testData.ZMCTL);
        regs2.WriteSCRCTL(testData.SCRCTL);

        state2.CalcAccessPatterns(regs2, accPatCfg);
        state2.CalcVCellScrollDelay(regs2);

        for (uint32 i = 0; i < 4; ++i) {
            DYNAMIC_SECTION("NBG" << i) {
                auto &bgExpected = testData.nbgs[i];
                if (!bgExpected.check) {
                    continue;
                }

                auto &bgParams = regs2.bgParams[i + 1];
                auto &bgState = state2.nbgLayerStates[i];
                REQUIRE(bgParams.bitmap == bgExpected.bitmap);
                CHECK(bgParams.patNameAccess == bgExpected.patNameAccess);
                CHECK(bgParams.charPatAccess == bgExpected.charPatAccess);
                CHECK(bgParams.charPatDelay == bgExpected.charPatDelay);
                CHECK(bgParams.vramDataOffset == bgExpected.vramDataOffset);
                CHECK(bgState.vcellScrollDelay == bgExpected.vcellScrollDelay);
                CHECK(bgState.vcellScrollRepeat == bgExpected.vcellScrollRepeat);
                CHECK(bgState.vcellScrollOffset == bgExpected.vcellScrollOffset);
            }
        }
        CHECK(regs2.vcellScrollInc == testData.vcellScrollInc);
        CHECK(state2.coeffAccess == testData.coeffAccess);
    }
}
