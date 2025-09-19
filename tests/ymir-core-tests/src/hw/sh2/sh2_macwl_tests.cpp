#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <ymir/hw/sh2/sh2.hpp>

#include <fmt/format.h>

#include <map>
#include <vector>

namespace sh2_macwl {

struct TestData {
    uint32 rn;
    uint32 rm;
    uint64 macIn;
    bool s;

    uint64 macw;
    uint64 macl;
};

constexpr auto testdata = {
    TestData{0xC6E55085, 0x58C4C6F3, 0x6E43A9068D905945, 1, 0x6E43A90780000000, 0xFFFF800000000000},
#include "sh2_macwl_testdata.inc"
};

using namespace ymir;

struct TestSubject {
    sys::SystemFeatures systemFeatures{};
    core::Scheduler scheduler{};
    mutable sys::SH2Bus bus{};
    mutable sh2::SH2 sh2{scheduler, bus, true, systemFeatures};
    sh2::SH2::Probe &probe{sh2.GetProbe()};

    TestSubject() {
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
        sh2.Reset(true);
        ClearCaptures();
        ClearMemoryMocks();
    }

    void ClearCaptures() const {
        memoryAccesses.clear();
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
    // Traces and mocked data

    struct MemoryAccessInfo {
        uint32 address;
        uint32 data;
        bool write;
        uint32 size;

        constexpr auto operator<=>(const MemoryAccessInfo &) const = default;
    };

    mutable std::vector<MemoryAccessInfo> memoryAccesses;

    mutable std::map<uint32, uint8> mockedReads8;
    mutable std::map<uint32, uint16> mockedReads16;
    mutable std::map<uint32, uint32> mockedReads32;
};

std::ostream &operator<<(std::ostream &os, TestSubject::MemoryAccessInfo const &value) {
    os << fmt::format("{}-bit {} from 0x{:08X} -> 0x{:X}", value.size * 8, (value.write ? "write" : "read"),
                      value.address, value.data);
    return os;
}

TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SH2 MACW/MACL operations are computed correctly",
                             "[sh2][instructions][macwl]") {
    ClearAll();

    const auto &testData = GENERATE(values<TestData>(testdata));

    constexpr uint16 instrMACL = 0x021F; // mac.l @r1+, @r2+
    constexpr uint16 instrMACW = 0x421F; // mac.w @r1+, @r2+
    constexpr uint16 instrNOP = 0x0009;

    probe.R(1) = 0x1000;
    probe.R(2) = 0x1100;
    probe.PC() = 0x4000;

    MockMemoryRead32(0x1000, testData.rm);
    MockMemoryRead16(0x1004, testData.rm);

    MockMemoryRead32(0x1100, testData.rn);
    MockMemoryRead16(0x1104, testData.rn);

    MockMemoryRead16(0x4000, instrMACL);
    MockMemoryRead16(0x4002, instrMACW);
    MockMemoryRead16(0x4004, instrNOP);

    // Run MAC.L
    probe.MAC().u64 = testData.macIn;
    probe.SR().S = testData.s;
    sh2.Step<false, false>();

    // Check results:
    // - increment both R1 and R2 by 4
    CHECK(probe.R(1) == 0x1004);
    CHECK(probe.R(2) == 0x1104);
    // - store result in MAC
    CHECK(probe.MAC().u64 == testData.macl);
    // - memory accesses:
    //   [0] read MAC.L instruction
    //   [1] read from @R1
    //   [2] read from @R2
    REQUIRE(memoryAccesses.size() == 3);
    CHECK(memoryAccesses[0] == MemoryAccessInfo{0x4000, instrMACL, false, sizeof(uint16)});
    CHECK(memoryAccesses[1] == MemoryAccessInfo{0x1100, testData.rn, false, sizeof(uint32)});
    CHECK(memoryAccesses[2] == MemoryAccessInfo{0x1000, testData.rm, false, sizeof(uint32)});

    ClearCaptures();

    // Run MAC.W
    probe.MAC().u64 = testData.macIn;
    probe.SR().S = testData.s;
    sh2.Step<false, false>();

    // Check results:
    // - increment both R1 and R2 by 2
    CHECK(probe.R(1) == 0x1006);
    CHECK(probe.R(2) == 0x1106);
    // - store result in MAC
    CHECK(probe.MAC().u64 == testData.macw);
    // - memory accesses:
    //   [0] read MAC.W instruction
    //   [1] read from @R1
    //   [2] read from @R2
    REQUIRE(memoryAccesses.size() == 3);
    CHECK(memoryAccesses[0] == MemoryAccessInfo{0x4002, instrMACW, false, sizeof(uint16)});
    CHECK(memoryAccesses[1] == MemoryAccessInfo{0x1104, (uint16)testData.rn, false, sizeof(uint16)});
    CHECK(memoryAccesses[2] == MemoryAccessInfo{0x1004, (uint16)testData.rm, false, sizeof(uint16)});
}

} // namespace sh2_macwl
