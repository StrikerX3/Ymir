#include <catch2/catch_test_macros.hpp>

#include <ymir/hw/sh2/sh2.hpp>

#include <fmt/format.h>

#include <map>
#include <vector>

// -----------------------------------------------------------------------------
// Test subject class

using namespace ymir;

namespace sh2_intr {

struct TestSubject : debug::ISH2Tracer {
    sys::SystemFeatures systemFeatures{};
    mutable core::Scheduler scheduler{};
    mutable sys::SH2Bus bus{};
    mutable sh2::SH2 sh2{scheduler, bus, true, systemFeatures};
    sh2::SH2::Probe &probe{sh2.GetProbe()};

    TestSubject() {
        // Setup tracer to collect interrupts into a vector
        sh2.UseTracer(this);

        sh2.MapCallbacks(util::MakeClassMemberRequiredCallback<&TestSubject::IntrAck>(this));

        bus.MapBoth(
            0x000'0000, 0x7FF'FFFF, this,
            [](uint32 address, void *ctx) -> uint8 { return static_cast<TestSubject *>(ctx)->Read8(address); },
            [](uint32 address, void *ctx) -> uint16 { return static_cast<TestSubject *>(ctx)->Read16(address); },
            [](uint32 address, void *ctx) -> uint32 { return static_cast<TestSubject *>(ctx)->Read32(address); },
            [](uint32 address, uint8 value, void *ctx) { static_cast<TestSubject *>(ctx)->Write8(address, value); },
            [](uint32 address, uint16 value, void *ctx) { static_cast<TestSubject *>(ctx)->Write16(address, value); },
            [](uint32 address, uint32 value, void *ctx) { static_cast<TestSubject *>(ctx)->Write32(address, value); });
    }

    void ClearAll() const {
        scheduler.Reset();
        sh2.Reset(true);
        ClearCaptures();
        ClearMemoryMocks();
    }

    void ClearCaptures() const {
        interrupts.clear();
        exceptions.clear();
        memoryAccesses.clear();
        intrAcked = false;
    }

    void ClearMemoryMocks() const {
        mockedReads8.clear();
        mockedReads16.clear();
        mockedReads32.clear();
    }

    void MockMemoryRead8(uint32 address, uint8 value) const {
        mockedReads8[address] = value;
    }

    void MockMemoryRead16(uint32 address, uint16 value) const {
        mockedReads16[address] = value;
    }

    void MockMemoryRead32(uint32 address, uint32 value) const {
        mockedReads32[address] = value;
    }

    // -------------------------------------------------------------------------
    // Callbacks

    void IntrAck() {
        intrAcked = true;
    }

    // -------------------------------------------------------------------------
    // Memory accessors

    uint8 Read8(uint32 address) {
        auto it = mockedReads8.find(address);
        const uint8 value = it != mockedReads8.end() ? it->second : 0;
        memoryAccesses.push_back({address, value, false, sizeof(uint8)});
        return value;
    }

    uint16 Read16(uint32 address) {
        auto it = mockedReads16.find(address);
        const uint16 value = it != mockedReads16.end() ? it->second : 0;
        memoryAccesses.push_back({address, value, false, sizeof(uint16)});
        return value;
    }

    uint32 Read32(uint32 address) {
        auto it = mockedReads32.find(address);
        const uint32 value = it != mockedReads32.end() ? it->second : 0;
        memoryAccesses.push_back({address, value, false, sizeof(uint32)});
        return value;
    }

    void Write8(uint32 address, uint8 value) {
        MockMemoryRead8(address, value);
        memoryAccesses.push_back({address, value, true, sizeof(uint8)});
    }

    void Write16(uint32 address, uint16 value) {
        MockMemoryRead16(address, value);
        memoryAccesses.push_back({address, value, true, sizeof(uint16)});
    }

    void Write32(uint32 address, uint32 value) {
        MockMemoryRead32(address, value);
        memoryAccesses.push_back({address, value, true, sizeof(uint32)});
    }

    // -------------------------------------------------------------------------
    // ISH2Tracer implementation

    void ExecuteInstruction(uint32 pc, uint16 opcode, bool delaySlot) override {}

    void Interrupt(uint8 vecNum, uint8 level, sh2::InterruptSource source, uint32 pc) override {
        interrupts.push_back({vecNum, level, source, pc});
    }

    void Exception(uint8 vecNum, uint32 pc, uint32 sr) override {
        exceptions.push_back({vecNum, pc, sr});
    }

    // -------------------------------------------------------------------------
    // Traces and mocked data

    struct InterruptInfo {
        uint8 vecNum;
        uint8 level;
        sh2::InterruptSource source;
        uint32 pc;

        constexpr auto operator<=>(const InterruptInfo &) const = default;
    };

    struct ExceptionInfo {
        uint8 vecNum;
        uint32 pc;
        uint32 sr;

        constexpr auto operator<=>(const ExceptionInfo &) const = default;
    };

    struct MemoryAccessInfo {
        uint32 address;
        uint32 data;
        bool write;
        uint32 size;

        constexpr auto operator<=>(const MemoryAccessInfo &) const = default;
    };

    mutable std::vector<InterruptInfo> interrupts;
    mutable std::vector<ExceptionInfo> exceptions;
    mutable std::vector<MemoryAccessInfo> memoryAccesses;
    mutable bool intrAcked;

    mutable std::map<uint32, uint8> mockedReads8;
    mutable std::map<uint32, uint16> mockedReads16;
    mutable std::map<uint32, uint32> mockedReads32;
};

std::ostream &operator<<(std::ostream &os, TestSubject::InterruptInfo const &value) {
    os << fmt::format("INT 0x{:02X} level {} @ 0x{:X}", value.vecNum, value.level, value.pc);
    return os;
}

std::ostream &operator<<(std::ostream &os, TestSubject::ExceptionInfo const &value) {
    os << fmt::format("Exception 0x{:02X} @ 0x{:X}, SR={:X}", value.vecNum, value.pc, value.sr);
    return os;
}

std::ostream &operator<<(std::ostream &os, TestSubject::MemoryAccessInfo const &value) {
    os << fmt::format("{}-bit {} from 0x{:08X} -> 0x{:X}", value.size * 8, (value.write ? "write" : "read"),
                      value.address, value.data);
    return os;
}

// -----------------------------------------------------------------------------
// Tests

inline constexpr uint16 instrNOP = 0x0009;
inline constexpr uint16 instrRTE = 0x002B;
inline constexpr uint16 instrRTS = 0x000B;

// Test full interrupt flow:
// - Entry and exit (with RTE instruction)
// - VBR handling
// - External interrupt vector fetch and acknowledgement
TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SH2 interrupt flow works correctly", "[sh2][intc][flow]") {
    ClearAll();

    constexpr uint32 startPC = 0x1000;
    constexpr uint32 startSP = 0x2000;
    constexpr uint32 startSR = 0x00000000 | (0x0 << 4);
    constexpr uint32 intrPC1 = 0x10000;
    constexpr uint32 intrPC2 = 0x20000;
    constexpr uint32 startVBR1 = 0;
    constexpr uint32 startVBR2 = 0x100000;
    constexpr uint8 intrVec = 0x70;
    constexpr uint8 intrLevel = 5;

    // Setup interrupt handlers with NOP, RTE, NOP (delay slot)
    MockMemoryRead16(intrPC1 + 0, instrNOP);
    MockMemoryRead16(intrPC1 + 2, instrRTE);
    MockMemoryRead16(intrPC1 + 4, instrNOP);

    MockMemoryRead16(intrPC2 + 0, instrNOP);
    MockMemoryRead16(intrPC2 + 2, instrRTE);
    MockMemoryRead16(intrPC2 + 4, instrNOP);

    // Setup interrupt vectors at two different locations
    MockMemoryRead32(startVBR1 + intrVec * sizeof(uint32), intrPC1);
    MockMemoryRead32(startVBR2 + intrVec * sizeof(uint32), intrPC2);

    probe.PC() = startPC;    // point PC somewhere
    probe.R(15) = startSP;   // point stack pointer elsewhere
    probe.VBR() = startVBR1; // point VBR to the first table
    probe.SR().u32 = startSR;
    probe.INTC().ICR.VECMD = 1; // use external interrupt vector
    probe.INTC().SetVector(sh2::InterruptSource::IRL, intrVec);
    probe.INTC().SetLevel(sh2::InterruptSource::IRL, intrLevel);
    probe.RaiseInterrupt(sh2::InterruptSource::IRL);
    REQUIRE(probe.CheckInterrupts());

    // Jump to interrupt handler
    scheduler.Advance(sh2.Step<true, false>());

    // Check results:
    // - one interrupt of the specified vector+level at the starting PC
    REQUIRE(interrupts.size() == 1);
    CHECK(interrupts[0] == InterruptInfo{intrVec, intrLevel, sh2::InterruptSource::IRL, startPC});
    // - one exception of the specified vector at the starting PC with the starting SR
    REQUIRE(exceptions.size() == 1);
    CHECK(exceptions[0] == ExceptionInfo{intrVec, startPC, startSR});
    // - external interrupt acknowledged
    CHECK(intrAcked);
    // - PC at the interrupt vector
    CHECK(probe.PC() == intrPC1);
    // - PC and SR pushed to the stack
    CHECK(probe.R(15) == startSP - 8); // should write PC and SR
    // - SR.I3-0 set to the interrupt level
    CHECK(probe.SR().ILevel == intrLevel);
    // - memory accesses:
    //   [0] push SR to stack
    //   [1] push PC to stack
    //   [2] read PC from VBR + vector*4
    REQUIRE(memoryAccesses.size() == 3);
    CHECK(memoryAccesses[0] == MemoryAccessInfo{startSP - 4, startSR, true, sizeof(uint32)});
    CHECK(memoryAccesses[1] == MemoryAccessInfo{startSP - 8, startPC, true, sizeof(uint32)});
    CHECK(memoryAccesses[2] == MemoryAccessInfo{startVBR1 + intrVec * sizeof(uint32), intrPC1, false, sizeof(uint32)});

    ClearCaptures();

    // Execute first instruction in the interrupt handler (should be a NOP)
    scheduler.Advance(sh2.Step<true, false>());

    // Check results:
    // - no interrupts
    REQUIRE(interrupts.empty());
    // - no exceptions
    REQUIRE(exceptions.empty());
    // - PC at the interrupt vector + 2
    CHECK(probe.PC() == intrPC1 + 2);
    // - no change to the stack
    CHECK(probe.R(15) == startSP - 8);
    // - no change to SR.I3-0
    CHECK(probe.SR().ILevel == intrLevel);
    // - memory accesses:
    //   [0] read NOP instruction from PC
    REQUIRE(memoryAccesses.size() == 1);
    CHECK(memoryAccesses[0] == MemoryAccessInfo{intrPC1, instrNOP, false, sizeof(uint16)});

    ClearCaptures();

    // This should be the RTE instruction
    scheduler.Advance(sh2.Step<true, false>());

    // Check results:
    // - no interrupts
    REQUIRE(interrupts.empty());
    // - no exceptions
    REQUIRE(exceptions.empty());
    // - PC at the NOP instruction in the delay slot of the RTE
    CHECK(probe.PC() == intrPC1 + 4);
    // - PC and SR popped from the stack
    CHECK(probe.R(15) == startSP);
    // - SR.I3-0 set to the previous value
    CHECK(probe.SR().u32 == startSR);
    // - memory accesses:
    //   [0] read RTE instruction from PC
    //   [1] pop PC from stack
    //   [2] pop SR from stack
    REQUIRE(memoryAccesses.size() == 3);
    CHECK(memoryAccesses[0] == MemoryAccessInfo{intrPC1 + 2, instrRTE, false, sizeof(uint16)});
    CHECK(memoryAccesses[1] == MemoryAccessInfo{startSP - 8, startPC, false, sizeof(uint32)});
    CHECK(memoryAccesses[2] == MemoryAccessInfo{startSP - 4, startSR, false, sizeof(uint32)});

    ClearCaptures();

    // This should be the NOP instruction in the delay slot
    scheduler.Advance(sh2.Step<true, false>());

    // Check results:
    // - no interrupts
    REQUIRE(interrupts.empty());
    // - no exceptions
    REQUIRE(exceptions.empty());
    // - PC back to the starting point
    CHECK(probe.PC() == startPC);
    // - no stack operations
    CHECK(probe.R(15) == startSP);
    // - no changes to SR
    CHECK(probe.SR().u32 == startSR);
    // - memory accesses:
    //   [0] read NOP instruction from PC
    REQUIRE(memoryAccesses.size() == 1);
    CHECK(memoryAccesses[0] == MemoryAccessInfo{intrPC1 + 4, instrNOP, false, sizeof(uint16)});

    ClearCaptures();

    // -----

    probe.VBR() = startVBR2; // point VBR to the second table
    probe.RaiseInterrupt(sh2::InterruptSource::IRL);
    REQUIRE(probe.CheckInterrupts());

    // Jump to interrupt handler
    scheduler.Advance(sh2.Step<true, false>());

    // Check results:
    // - one interrupt of the specified vector+level at the starting PC
    REQUIRE(interrupts.size() == 1);
    CHECK(interrupts[0] == InterruptInfo{intrVec, intrLevel, sh2::InterruptSource::IRL, startPC});
    // - one exception of the specified vector at the starting PC with the starting SR
    REQUIRE(exceptions.size() == 1);
    CHECK(exceptions[0] == ExceptionInfo{intrVec, startPC, startSR});
    // - external interrupt acknowledged
    CHECK(intrAcked);
    // - PC at the interrupt vector
    CHECK(probe.PC() == intrPC2);
    // - PC and SR pushed to the stack
    CHECK(probe.R(15) == startSP - 8); // should write PC and SR
    // - SR.I3-0 set to the interrupt level
    CHECK(probe.SR().ILevel == intrLevel);
    // - memory accesses:
    //   [0] push SR to stack
    //   [1] push PC to stack
    //   [2] read PC from VBR + vector*4
    REQUIRE(memoryAccesses.size() == 3);
    CHECK(memoryAccesses[0] == MemoryAccessInfo{startSP - 4, startSR, true, sizeof(uint32)});
    CHECK(memoryAccesses[1] == MemoryAccessInfo{startSP - 8, startPC, true, sizeof(uint32)});
    CHECK(memoryAccesses[2] == MemoryAccessInfo{startVBR2 + intrVec * sizeof(uint32), intrPC2, false, sizeof(uint32)});

    ClearCaptures();

    // Execute first instruction in the interrupt handler (should be a NOP)
    scheduler.Advance(sh2.Step<true, false>());

    // Check results:
    // - no interrupts
    REQUIRE(interrupts.empty());
    // - no exceptions
    REQUIRE(exceptions.empty());
    // - PC at the interrupt vector + 2
    CHECK(probe.PC() == intrPC2 + 2);
    // - no change to the stack
    CHECK(probe.R(15) == startSP - 8);
    // - no change to SR.I3-0
    CHECK(probe.SR().ILevel == intrLevel);
    // - memory accesses:
    //   [0] read NOP instruction from PC
    REQUIRE(memoryAccesses.size() == 1);
    CHECK(memoryAccesses[0] == MemoryAccessInfo{intrPC2, instrNOP, false, sizeof(uint16)});

    ClearCaptures();

    // This should be the RTE instruction
    scheduler.Advance(sh2.Step<true, false>());

    // Check results:
    // - no interrupts
    REQUIRE(interrupts.empty());
    // - no exceptions
    REQUIRE(exceptions.empty());
    // - PC at the NOP instruction in the delay slot of the RTE
    CHECK(probe.PC() == intrPC2 + 4);
    // - PC and SR popped from the stack
    CHECK(probe.R(15) == startSP);
    // - SR.I3-0 set to the previous value
    CHECK(probe.SR().u32 == startSR);
    // - memory accesses:
    //   [0] read RTE instruction from PC
    //   [1] pop PC from stack
    //   [2] pop SR from stack
    REQUIRE(memoryAccesses.size() == 3);
    CHECK(memoryAccesses[0] == MemoryAccessInfo{intrPC2 + 2, instrRTE, false, sizeof(uint16)});
    CHECK(memoryAccesses[1] == MemoryAccessInfo{startSP - 8, startPC, false, sizeof(uint32)});
    CHECK(memoryAccesses[2] == MemoryAccessInfo{startSP - 4, startSR, false, sizeof(uint32)});

    ClearCaptures();

    // This should be the NOP instruction in the delay slot
    scheduler.Advance(sh2.Step<true, false>());

    // Check results:
    // - no interrupts
    REQUIRE(interrupts.empty());
    // - no exceptions
    REQUIRE(exceptions.empty());
    // - PC back to the starting point
    CHECK(probe.PC() == startPC);
    // - no stack operations
    CHECK(probe.R(15) == startSP);
    // - no changes to SR
    CHECK(probe.SR().u32 == startSR);
    // - memory accesses:
    //   [0] read NOP instruction from PC
    REQUIRE(memoryAccesses.size() == 1);
    CHECK(memoryAccesses[0] == MemoryAccessInfo{intrPC2 + 4, instrNOP, false, sizeof(uint16)});
}

// Test that interrupts raised from each source map to the corresponding vector and level.
// Also test IRLs using autovector and external vector fetch/acknowledge.
TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SH2 interrupts are handled correctly", "[sh2][intc][single]") {
    ClearAll();

    constexpr uint32 startPC = 0x1000;
    constexpr uint32 startSP = 0x2000;
    constexpr uint32 startSR = 0x0;
    constexpr uint32 startVBR = 0x0;
    constexpr uint32 baseIntrPC = 0x10000;
    constexpr uint32 irlIntrPC = 0x20000;
    constexpr uint32 irlExIntrPC = 0x28000;
    constexpr uint32 ubcIntrPC = 0x30000;
    constexpr uint32 nmiIntrPC = 0x40000;
    constexpr uint8 irlExIntrVec = 0x60;
    constexpr uint8 irlExIntrLevel = 6;

    probe.PC() = startPC;
    probe.R(15) = startSP;
    probe.SR().u32 = startSR;
    probe.VBR() = startVBR;

    // Set up different vectors and levels for every interrupt source (although this is impossible on real hardware)
    // IRLs have fixed levels and need special testing for autovector and external vector
    // User break and NMI have fixed levels and vectors
    using enum sh2::InterruptSource;
    static constexpr sh2::InterruptSource kSources[] = {FRT_OVI, FRT_OCI,       FRT_ICI,       SCI_TEI,
                                                        SCI_TXI, SCI_RXI,       SCI_ERI,       BSC_REF_CMI,
                                                        WDT_ITI, DMAC1_XferEnd, DMAC0_XferEnd, DIVU_OVFI};
    auto &intc = probe.INTC();
    for (auto source : kSources) {
        const uint8 i = static_cast<uint8>(source);
        const uint8 vecNum = 0x70 + i;
        const uint8 level = i;
        const uint32 routineAddr = baseIntrPC + i * sizeof(uint16) * 2;
        intc.SetVector(source, vecNum);
        intc.SetLevel(source, level);
        MockMemoryRead32(startVBR + vecNum * sizeof(uint32), routineAddr);
        MockMemoryRead16(routineAddr + 0, instrRTE);
        MockMemoryRead16(routineAddr + 2, instrNOP);
    }

    // IRL autovector
    MockMemoryRead32(startVBR + 0x40 * sizeof(uint32), irlIntrPC);
    MockMemoryRead16(irlIntrPC + 0, instrRTE);
    MockMemoryRead16(irlIntrPC + 2, instrNOP);

    // IRL external vector
    MockMemoryRead32(startVBR + irlExIntrVec * sizeof(uint32), irlExIntrPC);
    MockMemoryRead16(irlExIntrPC + 0, instrRTE);
    MockMemoryRead16(irlExIntrPC + 2, instrNOP);

    // User break
    MockMemoryRead32(startVBR + 0x0C * sizeof(uint32), ubcIntrPC);
    MockMemoryRead16(ubcIntrPC + 0, instrRTE);
    MockMemoryRead16(ubcIntrPC + 2, instrNOP);

    // NMI
    MockMemoryRead32(startVBR + 0x0B * sizeof(uint32), nmiIntrPC);
    MockMemoryRead16(nmiIntrPC + 0, instrRTE);
    MockMemoryRead16(nmiIntrPC + 2, instrNOP);

    auto testIntr = [&](sh2::InterruptSource source, uint8 vecNum, uint8 level, uint32 intrHandlerAddr) {
        probe.RaiseInterrupt(source);
        REQUIRE(probe.CheckInterrupts());

        // Enter interrupt handler
        scheduler.Advance(sh2.Step<true, false>());

        // Check results:
        // - FRT OVI interrupt at starting PC
        REQUIRE(interrupts.size() == 1);
        CHECK(interrupts[0] == InterruptInfo{vecNum, level, source, startPC});
        // - exception at FRT OVI vector at starting PC with starting SR
        REQUIRE(exceptions.size() == 1);
        CHECK(exceptions[0] == ExceptionInfo{vecNum, startPC, startSR});
        // - PC at the RTE instruction
        CHECK(probe.PC() == intrHandlerAddr);
        // - SR.I3-0 set to the interrupt level (NMI sets level to 15)
        if (source == sh2::InterruptSource::NMI) {
            CHECK(probe.SR().ILevel == 15);
        } else {
            CHECK(probe.SR().ILevel == level);
        }
        // - memory accesses
        //   [0] push SR to stack
        //   [1] push PC to stack
        //   [2] read PC from VBR + vecNum*4
        const uint32 vecAddr = startVBR + vecNum * sizeof(uint32);
        REQUIRE(memoryAccesses.size() == 3);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{startSP - 4, startSR, true, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{startSP - 8, startPC, true, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{vecAddr, intrHandlerAddr, false, sizeof(uint32)});
        // - IRL interrupt acknowledged if INTC.ICR.VECMD=1; no other interrupt should be acknowledged
        if (source == sh2::InterruptSource::IRL) {
            CHECK(intrAcked == intc.ICR.VECMD);
        } else {
            CHECK(intrAcked == false);
        }

        ClearCaptures();

        // Step through RTE instruction
        scheduler.Advance(sh2.Step<true, false>());

        // Check results:
        // - no interrupts
        REQUIRE(interrupts.empty());
        // - no exceptions
        REQUIRE(exceptions.empty());
        // - PC at the NOP instruction in the delay slot of the RTE
        CHECK(probe.PC() == intrHandlerAddr + sizeof(uint16));
        // - SR restored to starting value by RTE
        CHECK(probe.SR().u32 == startSR);
        // - memory accesses
        //   [0] read instruction from PC (RTE)
        //   [1] pop PC from stack
        //   [2] pop SR from stack
        REQUIRE(memoryAccesses.size() == 3);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{intrHandlerAddr, instrRTE, false, sizeof(uint16)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{startSP - 8, startPC, false, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{startSP - 4, startSR, false, sizeof(uint32)});
    };

    auto testIndexedIntr = [&](sh2::InterruptSource source) {
        const uint8 index = static_cast<uint8>(source);
        const uint32 intrHandlerAddr = baseIntrPC + index * sizeof(uint16) * 2;
        testIntr(source, 0x70 + index, index, intrHandlerAddr);
    };

    SECTION("FRT OVI interrupt") {
        testIndexedIntr(FRT_OVI);
    }

    SECTION("FRT OCI interrupt") {
        testIndexedIntr(FRT_OCI);
    }

    SECTION("FRT ICI interrupt") {
        testIndexedIntr(FRT_ICI);
    }

    SECTION("SCI TEI interrupt") {
        testIndexedIntr(SCI_TEI);
    }

    SECTION("SCI TXI interrupt") {
        testIndexedIntr(SCI_TXI);
    }

    SECTION("SCI RXI interrupt") {
        testIndexedIntr(SCI_RXI);
    }

    SECTION("SCI ERI interrupt") {
        testIndexedIntr(SCI_ERI);
    }

    SECTION("BSC REF CMI interrupt") {
        testIndexedIntr(BSC_REF_CMI);
    }

    SECTION("WDT ITI interrupt") {
        testIndexedIntr(WDT_ITI);
    }

    SECTION("DMAC channel 1 transfer end interrupt") {
        testIndexedIntr(DMAC1_XferEnd);
    }

    SECTION("DMAC channel 0 transfer end interrupt") {
        testIndexedIntr(DMAC0_XferEnd);
    }

    SECTION("DIVU OVFI transfer end interrupt") {
        testIndexedIntr(DIVU_OVFI);
    }

    SECTION("UBC user break interrupt") {
        testIntr(UserBreak, 0x0C, 15, ubcIntrPC);
    }

    SECTION("NMI") {
        testIntr(NMI, 0x0B, 16, nmiIntrPC);
    }

    SECTION("IRL autovector interrupt") {
        probe.INTC().ICR.VECMD = 0; // use autovector
        testIntr(IRL, 0x40, 1, irlIntrPC);
    }

    SECTION("IRL external vector interrupt") {
        probe.INTC().ICR.VECMD = 1; // use external interrupt vector
        probe.INTC().SetVector(sh2::InterruptSource::IRL, irlExIntrVec);
        probe.INTC().SetLevel(sh2::InterruptSource::IRL, irlExIntrLevel);
        testIntr(IRL, irlExIntrVec, irlExIntrLevel, irlExIntrPC);
    }
}

// Test interrupt prioritization, including tiebreakers
TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SH2 interrupts are prioritized correctly", "[sh2][intc][priorities]") {
    ClearAll();

    SECTION("Basic priority handling - low priority before high priority") {
        auto &intc = probe.INTC();

        // Set up interrupts such that WDT ITI has higher priority than DIVU OVFI
        intc.SetVector(sh2::InterruptSource::DIVU_OVFI, 0x60);
        intc.SetLevel(sh2::InterruptSource::DIVU_OVFI, 6);
        intc.SetVector(sh2::InterruptSource::WDT_ITI, 0x70);
        intc.SetLevel(sh2::InterruptSource::WDT_ITI, 7);

        probe.SR().ILevel = 0;
        probe.RaiseInterrupt(sh2::InterruptSource::DIVU_OVFI);
        probe.RaiseInterrupt(sh2::InterruptSource::WDT_ITI);

        CHECK(probe.CheckInterrupts());
        CHECK(intc.pending.source == sh2::InterruptSource::WDT_ITI);
        CHECK(intc.pending.level == 7);
    }

    SECTION("Basic priority handling - high priority before low priority") {
        auto &intc = probe.INTC();

        // Set up interrupts such that WDT ITI has higher priority than DIVU OVFI
        intc.SetVector(sh2::InterruptSource::DIVU_OVFI, 0x60);
        intc.SetLevel(sh2::InterruptSource::DIVU_OVFI, 6);
        intc.SetVector(sh2::InterruptSource::WDT_ITI, 0x70);
        intc.SetLevel(sh2::InterruptSource::WDT_ITI, 7);

        probe.SR().ILevel = 0;
        probe.RaiseInterrupt(sh2::InterruptSource::WDT_ITI);
        probe.RaiseInterrupt(sh2::InterruptSource::DIVU_OVFI);

        CHECK(probe.CheckInterrupts());
        CHECK(intc.pending.source == sh2::InterruptSource::WDT_ITI);
        CHECK(intc.pending.level == 7);
    }

    SECTION("Basic priority handling - raise high priority before low priority, then lower high priority") {
        auto &intc = probe.INTC();

        // Set up interrupts such that WDT ITI has higher priority than DIVU OVFI
        intc.SetVector(sh2::InterruptSource::DIVU_OVFI, 0x60);
        intc.SetLevel(sh2::InterruptSource::DIVU_OVFI, 6);
        intc.SetVector(sh2::InterruptSource::WDT_ITI, 0x70);
        intc.SetLevel(sh2::InterruptSource::WDT_ITI, 7);

        // We need to force the actual OVFI flag to be set here

        probe.SR().ILevel = 0;
        probe.RaiseInterrupt(sh2::InterruptSource::WDT_ITI);
        probe.RaiseInterrupt(sh2::InterruptSource::DIVU_OVFI);
        probe.LowerInterrupt(sh2::InterruptSource::WDT_ITI);

        CHECK(probe.CheckInterrupts());
        CHECK(intc.pending.source == sh2::InterruptSource::DIVU_OVFI);
        CHECK(intc.pending.level == 6);
    }

    SECTION("Tiebreaker - low priority before high priority") {
        auto &intc = probe.INTC();

        // Set up interrupts such that WDT ITI has the same priority as DIVU OVFI.
        intc.SetVector(sh2::InterruptSource::DIVU_OVFI, 0x60);
        intc.SetLevel(sh2::InterruptSource::DIVU_OVFI, 6);
        intc.SetVector(sh2::InterruptSource::WDT_ITI, 0x61);
        intc.SetLevel(sh2::InterruptSource::WDT_ITI, 6);

        probe.SR().ILevel = 0;
        probe.RaiseInterrupt(sh2::InterruptSource::WDT_ITI);
        probe.RaiseInterrupt(sh2::InterruptSource::DIVU_OVFI);

        // DIVU OVFI should be prioritized
        CHECK(probe.CheckInterrupts());
        CHECK(intc.pending.source == sh2::InterruptSource::DIVU_OVFI);
        CHECK(intc.pending.level == 6);
    }

    SECTION("Tiebreaker - high priority before low priority") {
        auto &intc = probe.INTC();

        // Set up interrupts such that WDT ITI has the same priority as DIVU OVFI.
        intc.SetVector(sh2::InterruptSource::DIVU_OVFI, 0x60);
        intc.SetLevel(sh2::InterruptSource::DIVU_OVFI, 6);
        intc.SetVector(sh2::InterruptSource::WDT_ITI, 0x61);
        intc.SetLevel(sh2::InterruptSource::WDT_ITI, 6);

        probe.SR().ILevel = 0;
        probe.RaiseInterrupt(sh2::InterruptSource::DIVU_OVFI);
        probe.RaiseInterrupt(sh2::InterruptSource::WDT_ITI);

        // DIVU OVFI should be prioritized
        CHECK(probe.CheckInterrupts());
        CHECK(intc.pending.source == sh2::InterruptSource::DIVU_OVFI);
        CHECK(intc.pending.level == 6);
    }
}

// Test that interrupts are masked by the SR.I3-0 setting.
// Test that NMI is never masked.
TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SH2 interrupts are masked correctly", "[sh2][intc][level-mask]") {
    ClearAll();

    SECTION("Interrupt is not masked when priority is greater than SR.I3-0") {
        auto &intc = probe.INTC();

        // Set up interrupt with higher priority than SR.I3-0
        probe.SR().ILevel = 4;
        intc.SetVector(sh2::InterruptSource::WDT_ITI, 0x60);
        intc.SetLevel(sh2::InterruptSource::WDT_ITI, 5);

        probe.RaiseInterrupt(sh2::InterruptSource::WDT_ITI);

        // The interrupt is pending and about to be serviced
        CHECK(probe.CheckInterrupts() == true);
        CHECK(intc.pending.source == sh2::InterruptSource::WDT_ITI);
        CHECK(intc.pending.level == 5);
    }

    SECTION("Interrupt is masked when priority is equal to SR.I3-0") {
        auto &intc = probe.INTC();

        // Set up interrupt with same priority as SR.I3-0
        probe.SR().ILevel = 4;
        intc.SetVector(sh2::InterruptSource::WDT_ITI, 0x60);
        intc.SetLevel(sh2::InterruptSource::WDT_ITI, 4);

        probe.RaiseInterrupt(sh2::InterruptSource::WDT_ITI);

        // The interrupt is left pending, but not serviced
        CHECK(probe.CheckInterrupts() == false);
        CHECK(intc.pending.source == sh2::InterruptSource::WDT_ITI);
        CHECK(intc.pending.level == 4);
    }

    SECTION("Interrupt is masked when priority is less than SR.I3-0") {
        auto &intc = probe.INTC();

        // Set up interrupt with lower priority than SR.I3-0
        probe.SR().ILevel = 4;
        intc.SetVector(sh2::InterruptSource::WDT_ITI, 0x60);
        intc.SetLevel(sh2::InterruptSource::WDT_ITI, 3);

        probe.RaiseInterrupt(sh2::InterruptSource::WDT_ITI);

        // The interrupt is left pending, but not serviced
        CHECK(probe.CheckInterrupts() == false);
        CHECK(intc.pending.source == sh2::InterruptSource::WDT_ITI);
        CHECK(intc.pending.level == 3);
    }

    SECTION("NMI is never masked") {
        auto &intc = probe.INTC();

        probe.SR().ILevel = 0xF;

        probe.RaiseInterrupt(sh2::InterruptSource::NMI);

        // NMI is always serviced even with maximum SR.ILevel
        CHECK(probe.CheckInterrupts() == true);
        CHECK(intc.pending.source == sh2::InterruptSource::NMI);
        CHECK(intc.pending.level == 16);
    }
}

// Test that the CPU cannot handle interrupts in delay slots.
TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SH2 interrupts are not serviced in delay slots", "[sh2][intc][delay-slot]") {
    ClearAll();

    auto &intc = probe.INTC();

    constexpr uint32 startPC = 0x1000;

    // Setup simple program that returns to itself
    MockMemoryRead16(startPC + 0, instrRTS);
    MockMemoryRead16(startPC + 2, instrNOP);
    probe.PC() = startPC; // point PC to start of program
    probe.PR() = startPC; // point PR to start of program

    // Step once; should be in delay slot
    scheduler.Advance(sh2.Step<false, false>());

    REQUIRE(probe.IsInDelaySlot() == true);

    // Raise an interrupt
    probe.SR().ILevel = 0x0;
    intc.SetVector(sh2::InterruptSource::WDT_ITI, 0x60);
    intc.SetLevel(sh2::InterruptSource::WDT_ITI, 5);
    probe.RaiseInterrupt(sh2::InterruptSource::WDT_ITI);

    // Interrupt should not be serviced now, even though it is pending
    CHECK(probe.CheckInterrupts() == false);
    CHECK(intc.pending.source == sh2::InterruptSource::WDT_ITI);
    CHECK(intc.pending.level == 5);

    // Step once more; should be back to start of program
    scheduler.Advance(sh2.Step<false, false>());

    REQUIRE(probe.IsInDelaySlot() == false);

    // Interrupt should be serviceable now
    CHECK(probe.CheckInterrupts() == true);
    CHECK(intc.pending.source == sh2::InterruptSource::WDT_ITI);
    CHECK(intc.pending.level == 5);
}

} // namespace sh2_intr
