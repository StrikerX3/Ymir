#include <catch2/catch_test_macros.hpp>

#include <ymir/hw/scu/scu_dsp.hpp>

#include <fmt/format.h>

#include <map>
#include <string_view>

using namespace ymir;

namespace scu_dsp {

struct TestSubject {
    mutable sys::SH2Bus bus;
    mutable scu::SCUDSP dsp{bus};

    mutable bool dspEndTriggered = false;

    TestSubject() {
        dsp.SetTriggerDSPEndCallback(util::MakeClassMemberRequiredCallback<&TestSubject::TriggerDSPEnd>(this));

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
        dsp.Reset(true);
        ClearCaptures();
        ClearMemoryMocks();
    }

    void ClearCaptures() const {
        dspEndTriggered = false;
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
    // Callbacks

    void TriggerDSPEnd() {
        dspEndTriggered = true;
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
    // Mocked data

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

// -----------------------------------------------------------------------------

TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SCU DSP ALU operations compute correctly", "[scu][scudsp][alu]") {
    ClearAll();

    SECTION("AND") {
        dsp.carry = true;
        dsp.overflow = false;
        dsp.ALU.u16Top = 0xDEAD;

        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;

            dsp.AC.L = 0x9F00F;
            dsp.P.L = 0xCFF00;
            dsp.ALU_AND();

            CHECK(dsp.ALU.L == 0x8F000);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
        }

        SECTION("zero") {
            dsp.zero = false;
            dsp.sign = true;

            dsp.AC.L = 0x9F00F;
            dsp.P.L = 0x20FF0;
            dsp.ALU_AND();

            CHECK(dsp.ALU.L == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
        }

        SECTION("sign") {
            dsp.zero = true;
            dsp.sign = false;

            dsp.AC.L = 0x8001234F;
            dsp.P.L = 0x8005678F;
            dsp.ALU_AND();

            CHECK(dsp.ALU.L == 0x8001230F);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
        }

        // Carry should always be false
        CHECK(dsp.carry == false);

        // These should not be modified
        CHECK(dsp.overflow == false);
        CHECK(dsp.ALU.u16Top == 0xDEAD);
    }

    SECTION("OR") {
        dsp.carry = true;
        dsp.overflow = false;
        dsp.ALU.u16Top = 0xDEAD;

        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;

            dsp.AC.L = 0x9F00F;
            dsp.P.L = 0xCFF00;
            dsp.ALU_OR();

            CHECK(dsp.ALU.L == 0xDFF0F);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
        }

        SECTION("zero") {
            dsp.zero = false;
            dsp.sign = true;

            dsp.AC.L = 0;
            dsp.P.L = 0;
            dsp.ALU_OR();

            CHECK(dsp.ALU.L == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
        }

        SECTION("sign") {
            dsp.zero = true;
            dsp.sign = false;

            dsp.AC.L = 0x8001234F;
            dsp.P.L = 0x8005678F;
            dsp.ALU_OR();

            CHECK(dsp.ALU.L == 0x800567CF);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
        }

        // Carry should always be false
        CHECK(dsp.carry == false);

        // These should not be modified
        CHECK(dsp.overflow == false);
        CHECK(dsp.ALU.u16Top == 0xDEAD);
    }

    SECTION("XOR") {
        dsp.carry = true;
        dsp.overflow = false;
        dsp.ALU.u16Top = 0xDEAD;

        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;

            dsp.AC.L = 0x9F00F;
            dsp.P.L = 0xCFF00;
            dsp.ALU_XOR();

            CHECK(dsp.ALU.L == 0x50F0F);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
        }

        SECTION("zero") {
            dsp.zero = false;
            dsp.sign = true;

            dsp.AC.L = 0x1234;
            dsp.P.L = 0x1234;
            dsp.ALU_XOR();

            CHECK(dsp.ALU.L == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
        }

        SECTION("sign") {
            dsp.zero = true;
            dsp.sign = false;

            dsp.AC.L = 0x8001234F;
            dsp.P.L = 0x0005678F;
            dsp.ALU_XOR();

            CHECK(dsp.ALU.L == 0x800444C0);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
        }

        // Carry should always be false
        CHECK(dsp.carry == false);

        // These should not be modified
        CHECK(dsp.overflow == false);
        CHECK(dsp.ALU.u16Top == 0xDEAD);
    }

    SECTION("ADD") {
        dsp.ALU.u16Top = 0xDEAD;

        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.L = 123;
            dsp.P.L = 321;
            dsp.ALU_ADD();

            CHECK(dsp.ALU.L == 444);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("zero (with zeros)") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.L = 0;
            dsp.P.L = 0;
            dsp.ALU_ADD();

            CHECK(dsp.ALU.L == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("zero, carry") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.L = 0xFFFFFFFF;
            dsp.P.L = 1;
            dsp.ALU_ADD();

            CHECK(dsp.ALU.L == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == false);
        }

        SECTION("zero, carry, overflow") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.L = 0x80000000;
            dsp.P.L = 0x80000000;
            dsp.ALU_ADD();

            CHECK(dsp.ALU.L == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == true);
        }

        SECTION("sign") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.L = -123;
            dsp.P.L = 1;
            dsp.ALU_ADD();

            CHECK(dsp.ALU.L == -122);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("sign, carry") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.L = -123;
            dsp.P.L = -1;
            dsp.ALU_ADD();

            CHECK(dsp.ALU.L == -124);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == false);
        }

        SECTION("sign, overflow") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.L = 0x7FFFFFFF;
            dsp.P.L = 1;
            dsp.ALU_ADD();

            CHECK(dsp.ALU.L == 0x80000000);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == true);
        }

        SECTION("carry") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.L = 100;
            dsp.P.L = -1;
            dsp.ALU_ADD();

            CHECK(dsp.ALU.L == 99);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == false);
        }

        SECTION("carry, overflow") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.L = 0x80000000;
            dsp.P.L = -1;
            dsp.ALU_ADD();

            CHECK(dsp.ALU.L == 0x7FFFFFFF);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == true);
        }

        // ALU.u16Top should not be modified
        CHECK(dsp.ALU.u16Top == 0xDEAD);
    }

    SECTION("SUB") {
        dsp.ALU.u16Top = 0xDEAD;

        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.L = 321;
            dsp.P.L = 123;
            dsp.ALU_SUB();

            CHECK(dsp.ALU.L == 198);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("zero (with zeros)") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.L = 0;
            dsp.P.L = 0;
            dsp.ALU_SUB();

            CHECK(dsp.ALU.L == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("zero (with positives)") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.L = 0x7FFFFFFF;
            dsp.P.L = 0x7FFFFFFF;
            dsp.ALU_SUB();

            CHECK(dsp.ALU.L == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("zero (with negatives)") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.L = 0x80000000;
            dsp.P.L = 0x80000000;
            dsp.ALU_SUB();

            CHECK(dsp.ALU.L == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("sign") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.L = -123;
            dsp.P.L = 1;
            dsp.ALU_SUB();

            CHECK(dsp.ALU.L == -124);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("sign, carry") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.L = 1;
            dsp.P.L = 123;
            dsp.ALU_SUB();

            CHECK(dsp.ALU.L == -122);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == false);
        }

        SECTION("sign, carry, overflow") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.L = 1;
            dsp.P.L = 0x80000001;
            dsp.ALU_SUB();

            CHECK(dsp.ALU.L == 0x80000000);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == true);
        }

        SECTION("overflow") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.L = 0x80000000;
            dsp.P.L = 0x7FFFFFFF;
            dsp.ALU_SUB();

            CHECK(dsp.ALU.L == 1);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == true);
        }

        // ALU.u16Top should not be modified
        CHECK(dsp.ALU.u16Top == 0xDEAD);
    }

    SECTION("AD2") {
        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.u64 = 123;
            dsp.P.u64 = 321;
            dsp.ALU_AD2();

            CHECK(dsp.ALU.u64 == 444);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("zero") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.u64 = 0;
            dsp.P.u64 = 0;
            dsp.ALU_AD2();

            CHECK(dsp.ALU.u64 == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("zero, carry") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.u64 = -1;
            dsp.P.u64 = 1;
            dsp.ALU_AD2();

            CHECK(dsp.ALU.u64 == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == false);
        }

        SECTION("zero, carry, overflow") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.u64 = 0x800000000000;
            dsp.P.u64 = 0x800000000000;
            dsp.ALU_AD2();

            CHECK(dsp.ALU.u64 == 0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == true);
        }

        SECTION("sign") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.u64 = -123;
            dsp.P.u64 = 1;
            dsp.ALU_AD2();

            CHECK(dsp.ALU.s64 == -122);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == false);
        }

        SECTION("sign, carry") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.u64 = -123;
            dsp.P.u64 = -1;
            dsp.ALU_AD2();

            CHECK(dsp.ALU.s64 == -124);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == false);
        }

        SECTION("sign, overflow") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = true;
            dsp.overflow = false;

            dsp.AC.u64 = 0x7FFFFFFFFFFF;
            dsp.P.u64 = 1;
            dsp.ALU_AD2();

            CHECK(dsp.ALU.u64 == 0x800000000000);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == false);
            CHECK(dsp.overflow == true);
        }

        SECTION("carry") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.u64 = 100;
            dsp.P.u64 = -1;
            dsp.ALU_AD2();

            CHECK(dsp.ALU.u64 == 99);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == false);
        }

        SECTION("carry, overflow") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = false;
            dsp.overflow = false;

            dsp.AC.u64 = 0x800000000000;
            dsp.P.u64 = -1;
            dsp.ALU_AD2();

            CHECK(dsp.ALU.u64 == 0x7FFFFFFFFFFF);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
            CHECK(dsp.overflow == true);
        }
    }

    SECTION("SR") {
        dsp.overflow = false;
        dsp.ALU.u16Top = 0xDEAD;

        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = true;

            dsp.AC.L = 0x10;
            dsp.ALU_SR();

            CHECK(dsp.ALU.L == 0x8);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
        }

        SECTION("zero") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = true;

            dsp.AC.L = 0x0;
            dsp.ALU_SR();

            CHECK(dsp.ALU.L == 0x0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
        }

        SECTION("zero, carry") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = false;

            dsp.AC.L = 0x1;
            dsp.ALU_SR();

            CHECK(dsp.ALU.L == 0x0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
        }

        SECTION("carry") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = false;

            dsp.AC.L = 0x11;
            dsp.ALU_SR();

            CHECK(dsp.ALU.L == 0x8);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
        }

        // These should not be modified
        CHECK(dsp.overflow == false);
        CHECK(dsp.ALU.u16Top == 0xDEAD);
    }

    SECTION("RR") {
        dsp.overflow = false;
        dsp.ALU.u16Top = 0xDEAD;

        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = true;

            dsp.AC.L = 0x10;
            dsp.ALU_RR();

            CHECK(dsp.ALU.L == 0x8);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
        }

        SECTION("zero") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = true;

            dsp.AC.L = 0x0;
            dsp.ALU_RR();

            CHECK(dsp.ALU.L == 0x0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
        }

        SECTION("sign, carry") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = false;

            dsp.AC.L = 0x1;
            dsp.ALU_RR();

            CHECK(dsp.ALU.L == 0x80000000);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == true);
        }

        // These should not be modified
        CHECK(dsp.overflow == false);
        CHECK(dsp.ALU.u16Top == 0xDEAD);
    }

    SECTION("SL") {
        dsp.overflow = false;
        dsp.ALU.u16Top = 0xDEAD;

        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = true;

            dsp.AC.L = 0x10;
            dsp.ALU_SL();

            CHECK(dsp.ALU.L == 0x20);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
        }

        SECTION("zero") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = true;

            dsp.AC.L = 0x0;
            dsp.ALU_SL();

            CHECK(dsp.ALU.L == 0x0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
        }

        SECTION("zero, carry") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = false;

            dsp.AC.L = 0x80000000;
            dsp.ALU_SL();

            CHECK(dsp.ALU.L == 0x0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
        }

        SECTION("carry") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = false;

            dsp.AC.L = 0x80000001;
            dsp.ALU_SL();

            CHECK(dsp.ALU.L == 0x2);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
        }

        // These should not be modified
        CHECK(dsp.overflow == false);
        CHECK(dsp.ALU.u16Top == 0xDEAD);
    }

    SECTION("RL") {
        dsp.overflow = false;
        dsp.ALU.u16Top = 0xDEAD;

        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = true;

            dsp.AC.L = 0x10;
            dsp.ALU_RL();

            CHECK(dsp.ALU.L == 0x20);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
        }

        SECTION("zero") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = true;

            dsp.AC.L = 0x0;
            dsp.ALU_RL();

            CHECK(dsp.ALU.L == 0x0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
        }

        SECTION("sign") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = true;

            dsp.AC.L = 0x40000000;
            dsp.ALU_RL();

            CHECK(dsp.ALU.L == 0x80000000);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == false);
        }

        SECTION("carry") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = false;

            dsp.AC.L = 0x80000000;
            dsp.ALU_RL();

            CHECK(dsp.ALU.L == 0x1);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
        }

        SECTION("sign, carry") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = false;

            dsp.AC.L = 0xC0000000;
            dsp.ALU_RL();

            CHECK(dsp.ALU.L == 0x80000001);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == true);
        }

        // These should not be modified
        CHECK(dsp.overflow == false);
        CHECK(dsp.ALU.u16Top == 0xDEAD);
    }

    SECTION("RL8") {
        dsp.overflow = false;
        dsp.ALU.u16Top = 0xDEAD;

        SECTION("no flags") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = true;

            dsp.AC.L = 0x10;
            dsp.ALU_RL8();

            CHECK(dsp.ALU.L == 0x1000);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
        }

        SECTION("zero") {
            dsp.zero = false;
            dsp.sign = true;
            dsp.carry = true;

            dsp.AC.L = 0x0;
            dsp.ALU_RL8();

            CHECK(dsp.ALU.L == 0x0);
            CHECK(dsp.zero == true);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == false);
        }

        SECTION("sign") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = true;

            dsp.AC.L = 0x800000;
            dsp.ALU_RL8();

            CHECK(dsp.ALU.L == 0x80000000);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == false);
        }

        SECTION("sign, carry") {
            dsp.zero = true;
            dsp.sign = false;
            dsp.carry = false;

            dsp.AC.L = 0x01800000;
            dsp.ALU_RL8();

            CHECK(dsp.ALU.L == 0x80000001);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == true);
            CHECK(dsp.carry == true);
        }

        SECTION("carry") {
            dsp.zero = true;
            dsp.sign = true;
            dsp.carry = false;

            dsp.AC.L = 0x01000000;
            dsp.ALU_RL8();

            CHECK(dsp.ALU.L == 0x00000001);
            CHECK(dsp.zero == false);
            CHECK(dsp.sign == false);
            CHECK(dsp.carry == true);
        }

        // These should not be modified
        CHECK(dsp.overflow == false);
        CHECK(dsp.ALU.u16Top == 0xDEAD);
    }
}

// -----------------------------------------------------------------------------

struct DSPState {
    std::array<scu::DSPInstr, 256> programRAM;
    std::array<std::array<uint32, 64>, 4> dataRAM;

    uint8 PC; // program address

    bool sign;
    bool zero;
    bool carry;
    bool overflow;

    // DSP data address
    std::array<uint8, 4> CT;

    uint64 ALU; // ALU operation output
    uint64 AC;  // ALU operation input 1
    uint64 P;   // ALU operation input 2 / Multiplication output
    sint32 RX;  // Multiplication input 1
    sint32 RY;  // Multiplication input 2

    uint16 LOP; // Loop count
    uint8 TOP;  // loop top

    uint32 RA0; // DMA read address
    uint32 WA0; // DMA write address
};

struct TestData {
    std::string_view name;
    DSPState initialState;
    DSPState finalState;
    uint32 numSteps;
    // TODO: bus accesses
    // TODO: mocked memory
};

constexpr auto testdata = {
    TestData{
        .name = "NOP",
        .initialState =
            {
                .programRAM = {{{0x00000000}}},
                .PC = 0,
            },
        .finalState =
            {
                .programRAM = {{{0x00000000}}},
                .PC = 2,
            },
        .numSteps = 1,
    },
#include "scu_dsp_testdata.inc"
};

TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SCU DSP instructions execute correctly", "[scu][scudsp][instructions]") {
    ClearAll();

    for (const auto &test : testdata) {
        DYNAMIC_SECTION(test.name) {
            // Initialize DSP state
            dsp.programRAM = test.initialState.programRAM;
            dsp.dataRAM = test.initialState.dataRAM;
            dsp.PC = test.initialState.PC;
            dsp.sign = test.initialState.sign;
            dsp.zero = test.initialState.zero;
            dsp.carry = test.initialState.carry;
            dsp.overflow = test.initialState.overflow;
            dsp.CT.array = test.initialState.CT;
            dsp.ALU.u64 = test.initialState.ALU;
            dsp.AC.u64 = test.initialState.AC;
            dsp.P.u64 = test.initialState.P;
            dsp.RX = test.initialState.RX;
            dsp.RY = test.initialState.RY;
            dsp.loopCount = test.initialState.LOP;
            dsp.loopTop = test.initialState.TOP;
            dsp.dmaReadAddr = test.initialState.RA0;
            dsp.dmaWriteAddr = test.initialState.WA0;

            // Setup execution
            dsp.PC = 0;
            dsp.programExecuting = true;
            dsp.programEnded = false;
            dsp.programPaused = false;
            dsp.programStep = false;

            // Run for the specified number of cycles
            dsp.Run<false>((test.numSteps + 1) * 2);

            // Compare DSP state against expected state
            CHECK(dsp.programRAM == test.finalState.programRAM);
            CHECK(dsp.dataRAM == test.finalState.dataRAM);
            CHECK(dsp.PC == test.finalState.PC);
            CHECK(dsp.sign == test.finalState.sign);
            CHECK(dsp.zero == test.finalState.zero);
            CHECK(dsp.carry == test.finalState.carry);
            CHECK(dsp.overflow == test.finalState.overflow);
            CHECK(dsp.CT.array == test.finalState.CT);
            CHECK(dsp.ALU.u64 == test.finalState.ALU);
            CHECK(dsp.AC.u64 == test.finalState.AC);
            CHECK(dsp.P.u64 == test.finalState.P);
            CHECK(dsp.RX == test.finalState.RX);
            CHECK(dsp.RY == test.finalState.RY);
            CHECK(dsp.loopCount == test.finalState.LOP);
            CHECK(dsp.loopTop == test.finalState.TOP);
            CHECK(dsp.dmaReadAddr == test.finalState.RA0);
            CHECK(dsp.dmaWriteAddr == test.finalState.WA0);
        }
    }
}

TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SCU DSP loop instructions execute correctly",
                             "[scu][scudsp][instructions]") {
    ClearAll();

    SECTION("LPS") {
        dsp.programRAM[0].u32 = 0xE8000000; // LPS
        dsp.programRAM[1].u32 = 0x10040000; // ADD  MOV ALU,A
        dsp.AC.u64 = 1;
        dsp.P.u64 = 1;
        dsp.loopCount = 2;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        // Run step 1 - pipelined NOP + LPS
        // LOP > 0 => set TOP = PC and LOP = LOP-1
        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 1);
        CHECK(dsp.loopTop == 0);
        CHECK(dsp.loopCount == 1);
        CHECK(dsp.ALU.u64 == 1); // copied from AC
        CHECK(dsp.AC.u64 == 1);
        CHECK(dsp.P.u64 == 1);

        // Run step 2 - ADD  MOV ALU,A; LOP > 0 => LOP = LOP-1
        dsp.Run<false>(1 * 2);

        CHECK(dsp.PC == 1);
        CHECK(dsp.loopTop == 0);
        CHECK(dsp.loopCount == 0);
        CHECK(dsp.ALU.u64 == 2);
        CHECK(dsp.AC.u64 == 2);
        CHECK(dsp.P.u64 == 1);

        // Run step 3 - ADD  MOV ALU,A; LOP == 0 => PC++, LOP = LOP-1
        dsp.Run<false>(1 * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.loopTop == 0);
        CHECK(dsp.loopCount == 0xFFF);
        CHECK(dsp.ALU.u64 == 3);
        CHECK(dsp.AC.u64 == 3);
        CHECK(dsp.P.u64 == 1);
    }

    SECTION("BTM") {
        dsp.programRAM[0].u32 = 0x00000000; // NOP
        dsp.programRAM[1].u32 = 0x10040000; // ADD  MOV ALU,A
        dsp.programRAM[2].u32 = 0xE0000000; // BTM
        dsp.programRAM[3].u32 = 0x28040000; // SL   MOV ALU,A
        dsp.AC.u64 = 1;
        dsp.P.u64 = 1;
        dsp.loopTop = 1;
        dsp.loopCount = 2;

        // Setup execution
        dsp.PC = 1;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        // Run step 1 - pipelined NOP + ADD  MOV ALU,A
        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 3);
        CHECK(dsp.loopTop == 1);
        CHECK(dsp.loopCount == 2);
        CHECK(dsp.ALU.u64 == 2);
        CHECK(dsp.AC.u64 == 2);
        CHECK(dsp.P.u64 == 1);

        // Run step 2 - BTM
        // LOP > 0 => LOP = LOP-1, PC = TOP
        dsp.Run<false>(1 * 2);

        CHECK(dsp.PC == 1);
        CHECK(dsp.loopTop == 1);
        CHECK(dsp.loopCount == 1);
        CHECK(dsp.ALU.u64 == 2);
        CHECK(dsp.AC.u64 == 2);
        CHECK(dsp.P.u64 == 1);

        // Run step 3 - SL   MOV ALU,A  (from pipeline)
        dsp.Run<false>(1 * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.loopTop == 1);
        CHECK(dsp.loopCount == 1);
        CHECK(dsp.ALU.u64 == 4);
        CHECK(dsp.AC.u64 == 4);
        CHECK(dsp.P.u64 == 1);

        // Run step 4 - ADD  MOV ALU,A
        dsp.Run<false>(1 * 2);

        CHECK(dsp.PC == 3);
        CHECK(dsp.loopTop == 1);
        CHECK(dsp.loopCount == 1);
        CHECK(dsp.ALU.u64 == 5);
        CHECK(dsp.AC.u64 == 5);
        CHECK(dsp.P.u64 == 1);

        // Run step 5 - BTM
        // LOP > 0 => LOP = LOP-1, PC = TOP
        dsp.Run<false>(1 * 2);

        CHECK(dsp.PC == 1);
        CHECK(dsp.loopTop == 1);
        CHECK(dsp.loopCount == 0);
        CHECK(dsp.ALU.u64 == 5);
        CHECK(dsp.AC.u64 == 5);
        CHECK(dsp.P.u64 == 1);

        // Run step 6 - SL   MOV ALU,A  (from pipeline)
        dsp.Run<false>(1 * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.loopTop == 1);
        CHECK(dsp.loopCount == 0);
        CHECK(dsp.ALU.u64 == 10);
        CHECK(dsp.AC.u64 == 10);
        CHECK(dsp.P.u64 == 1);

        // Run step 7 - ADD  MOV ALU,A
        dsp.Run<false>(1 * 2);

        CHECK(dsp.PC == 3);
        CHECK(dsp.loopTop == 1);
        CHECK(dsp.loopCount == 0);
        CHECK(dsp.ALU.u64 == 11);
        CHECK(dsp.AC.u64 == 11);
        CHECK(dsp.P.u64 == 1);

        // Run step 8 - BTM
        // LOP = 0 => skip instruction and set LOP = LOP-1
        dsp.Run<false>(1 * 2);

        CHECK(dsp.PC == 4);
        CHECK(dsp.loopTop == 1);
        CHECK(dsp.loopCount == 0xFFF);
        CHECK(dsp.ALU.u64 == 11);
        CHECK(dsp.AC.u64 == 11);
        CHECK(dsp.P.u64 == 1);

        // Run step 9 - SL   MOV ALU,A
        dsp.Run<false>(1 * 2);

        CHECK(dsp.PC == 5);
        CHECK(dsp.loopTop == 1);
        CHECK(dsp.loopCount == 0xFFF);
        CHECK(dsp.ALU.u64 == 22);
        CHECK(dsp.AC.u64 == 22);
        CHECK(dsp.P.u64 == 1);
    }
}

TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SCU DSP end instructions execute correctly", "[scu][scudsp][instructions]") {
    ClearAll();

    SECTION("END") {
        dsp.programRAM[0].u32 = 0xF0000000; // END

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2); // PC is incremented one extra time after END
        CHECK(dsp.programExecuting == false);
        CHECK(dsp.programEnded == false); // END does not set the end flag
        CHECK(dspEndTriggered == false);
    }

    SECTION("ENDI") {
        dsp.programRAM[0].u32 = 0xF8000000; // ENDI

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2); // PC is incremented one extra time after ENDI
        CHECK(dsp.programExecuting == false);
        CHECK(dsp.programEnded == true);
        CHECK(dspEndTriggered == true);
    }
}

TEST_CASE_PERSISTENT_FIXTURE(TestSubject, "SCU DSP DMA transfers execute correctly", "[scu][scudsp][dma]") {
    ClearAll();

    SECTION("DMA MC0,D0,#1 (invalid region)") {
        dsp.programRAM[0].u32 = 0xC0001001;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x1001000;
        dsp.dmaWriteAddr = 0x1002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x1001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x1002000); // no hold = update address
        REQUIRE(memoryAccesses.size() == 0);  // no access for invalid region
    }

    SECTION("DMA MC0,D0,#1 (A-Bus)") {
        dsp.programRAM[0].u32 = 0xC0001001;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x2001000;
        dsp.dmaWriteAddr = 0x2002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x2001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x2002004); // no hold = update address
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x2002000, 1, true, sizeof(uint32)});
    }

    SECTION("DMA MC0,D0,#1 (B-Bus)") {
        dsp.programRAM[0].u32 = 0xC0001001;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x5A01000;
        dsp.dmaWriteAddr = 0x5A02000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x5A01000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x5A02004); // no hold = update address
        REQUIRE(memoryAccesses.size() == 2);  // 32-bit access broken down into two 16-bit accesses
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x5A02000, 0, true, sizeof(uint16)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x5A02000, 1, true, sizeof(uint16)});
    }

    SECTION("DMA MC0,D0,#1 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0001001;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002004); // no hold = update address
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 1, true, sizeof(uint32)});
    }

    SECTION("DMA MC1,D0,#1 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0001101;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaSrc == 1);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002004); // no hold = update address
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 3, true, sizeof(uint32)});
    }

    SECTION("DMA MC2,D0,#1 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0001201;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaSrc == 2);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002004); // no hold = update address
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 6, true, sizeof(uint32)});
    }

    SECTION("DMA MC3,D0,#1 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0001301;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaSrc == 3);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002004); // no hold = update address
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 10, true, sizeof(uint32)});
    }

    SECTION("DMA MC3,D0,#4 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0011304;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 0;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 0);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 4);
        CHECK(dsp.dmaSrc == 3);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002010); // no hold = update address
        REQUIRE(memoryAccesses.size() == 4);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 7, true, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6002004, 8, true, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6002008, 9, true, sizeof(uint32)});
        CHECK(memoryAccesses[3] == MemoryAccessInfo{0x600200C, 10, true, sizeof(uint32)});
    }

    SECTION("DMAH MC3,D0,#4 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0015304;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 0;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 0);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == true);
        CHECK(dsp.dmaCount == 4);
        CHECK(dsp.dmaSrc == 3);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == true);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // hold = maintain address
        REQUIRE(memoryAccesses.size() == 4);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 7, true, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6002004, 8, true, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6002008, 9, true, sizeof(uint32)});
        CHECK(memoryAccesses[3] == MemoryAccessInfo{0x600200C, 10, true, sizeof(uint32)});
    }

    SECTION("DMA MC0,D0,M0 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0013000;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002004); // no hold = update address
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 1, true, sizeof(uint32)});
    }

    SECTION("DMA MC0,D0,M1 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0013001;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[0][1] = 11;
        dsp.dataRAM[0][2] = 21;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 3);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x600200C); // no hold = update address
        REQUIRE(memoryAccesses.size() == 3);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 1, true, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6002004, 11, true, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6002008, 21, true, sizeof(uint32)});
    }

    SECTION("DMA MC0,D0,M2 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0013002;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[0][1] = 11;
        dsp.dataRAM[0][2] = 21;
        dsp.dataRAM[0][3] = 31;
        dsp.dataRAM[0][4] = 41;
        dsp.dataRAM[0][5] = 51;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 6);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002018); // no hold = update address
        REQUIRE(memoryAccesses.size() == 6);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 1, true, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6002004, 11, true, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6002008, 21, true, sizeof(uint32)});
        CHECK(memoryAccesses[3] == MemoryAccessInfo{0x600200C, 31, true, sizeof(uint32)});
        CHECK(memoryAccesses[4] == MemoryAccessInfo{0x6002010, 41, true, sizeof(uint32)});
        CHECK(memoryAccesses[5] == MemoryAccessInfo{0x6002014, 51, true, sizeof(uint32)});
    }

    SECTION("DMA MC0,D0,M3 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0013003;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[0][1] = 11;
        dsp.dataRAM[0][2] = 21;
        dsp.dataRAM[0][3] = 31;
        dsp.dataRAM[0][4] = 41;
        dsp.dataRAM[0][5] = 51;
        dsp.dataRAM[0][6] = 61;
        dsp.dataRAM[0][7] = 71;
        dsp.dataRAM[0][8] = 81;
        dsp.dataRAM[0][9] = 91;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 10);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002028); // no hold = update address
        REQUIRE(memoryAccesses.size() == 10);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 1, true, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6002004, 11, true, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6002008, 21, true, sizeof(uint32)});
        CHECK(memoryAccesses[3] == MemoryAccessInfo{0x600200C, 31, true, sizeof(uint32)});
        CHECK(memoryAccesses[4] == MemoryAccessInfo{0x6002010, 41, true, sizeof(uint32)});
        CHECK(memoryAccesses[5] == MemoryAccessInfo{0x6002014, 51, true, sizeof(uint32)});
        CHECK(memoryAccesses[6] == MemoryAccessInfo{0x6002018, 61, true, sizeof(uint32)});
        CHECK(memoryAccesses[7] == MemoryAccessInfo{0x600201C, 71, true, sizeof(uint32)});
        CHECK(memoryAccesses[8] == MemoryAccessInfo{0x6002020, 81, true, sizeof(uint32)});
        CHECK(memoryAccesses[9] == MemoryAccessInfo{0x6002024, 91, true, sizeof(uint32)});
    }

    // TODO: check this on hardware; is it supposed to preincrement MC0 and read from data RAM[0][1]?
    SECTION("DMA MC0,D0,MC0 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0013004;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[0][1] = 11;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 1);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002004); // no hold = update address
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 11, true, sizeof(uint32)});
    }

    SECTION("DMA MC0,D0,MC1 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0013005;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[0][1] = 11;
        dsp.dataRAM[0][2] = 21;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 2);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 3);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x600200C); // no hold = update address
        REQUIRE(memoryAccesses.size() == 3);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 1, true, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6002004, 11, true, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6002008, 21, true, sizeof(uint32)});
    }

    SECTION("DMA MC0,D0,MC2 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0013006;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[0][1] = 11;
        dsp.dataRAM[0][2] = 21;
        dsp.dataRAM[0][3] = 31;
        dsp.dataRAM[0][4] = 41;
        dsp.dataRAM[0][5] = 51;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 3);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 6);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002018); // no hold = update address
        REQUIRE(memoryAccesses.size() == 6);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 1, true, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6002004, 11, true, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6002008, 21, true, sizeof(uint32)});
        CHECK(memoryAccesses[3] == MemoryAccessInfo{0x600200C, 31, true, sizeof(uint32)});
        CHECK(memoryAccesses[4] == MemoryAccessInfo{0x6002010, 41, true, sizeof(uint32)});
        CHECK(memoryAccesses[5] == MemoryAccessInfo{0x6002014, 51, true, sizeof(uint32)});
    }

    SECTION("DMA MC0,D0,MC3 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0013007;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[0][1] = 11;
        dsp.dataRAM[0][2] = 21;
        dsp.dataRAM[0][3] = 31;
        dsp.dataRAM[0][4] = 41;
        dsp.dataRAM[0][5] = 51;
        dsp.dataRAM[0][6] = 61;
        dsp.dataRAM[0][7] = 71;
        dsp.dataRAM[0][8] = 81;
        dsp.dataRAM[0][9] = 91;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 4);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 10);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002028); // no hold = update address
        REQUIRE(memoryAccesses.size() == 10);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 1, true, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6002004, 11, true, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6002008, 21, true, sizeof(uint32)});
        CHECK(memoryAccesses[3] == MemoryAccessInfo{0x600200C, 31, true, sizeof(uint32)});
        CHECK(memoryAccesses[4] == MemoryAccessInfo{0x6002010, 41, true, sizeof(uint32)});
        CHECK(memoryAccesses[5] == MemoryAccessInfo{0x6002014, 51, true, sizeof(uint32)});
        CHECK(memoryAccesses[6] == MemoryAccessInfo{0x6002018, 61, true, sizeof(uint32)});
        CHECK(memoryAccesses[7] == MemoryAccessInfo{0x600201C, 71, true, sizeof(uint32)});
        CHECK(memoryAccesses[8] == MemoryAccessInfo{0x6002020, 81, true, sizeof(uint32)});
        CHECK(memoryAccesses[9] == MemoryAccessInfo{0x6002024, 91, true, sizeof(uint32)});
    }

    SECTION("DMAH MC0,D0,MC3 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0017007;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[0][1] = 11;
        dsp.dataRAM[0][2] = 21;
        dsp.dataRAM[0][3] = 31;
        dsp.dataRAM[0][4] = 41;
        dsp.dataRAM[0][5] = 51;
        dsp.dataRAM[0][6] = 61;
        dsp.dataRAM[0][7] = 71;
        dsp.dataRAM[0][8] = 81;
        dsp.dataRAM[0][9] = 91;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 4);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == true);
        CHECK(dsp.dmaCount == 10);
        CHECK(dsp.dmaSrc == 0);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == true);
        CHECK(dsp.dmaHold == true);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // not used = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // hold = maintain address
        REQUIRE(memoryAccesses.size() == 10);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6002000, 1, true, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6002004, 11, true, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6002008, 21, true, sizeof(uint32)});
        CHECK(memoryAccesses[3] == MemoryAccessInfo{0x600200C, 31, true, sizeof(uint32)});
        CHECK(memoryAccesses[4] == MemoryAccessInfo{0x6002010, 41, true, sizeof(uint32)});
        CHECK(memoryAccesses[5] == MemoryAccessInfo{0x6002014, 51, true, sizeof(uint32)});
        CHECK(memoryAccesses[6] == MemoryAccessInfo{0x6002018, 61, true, sizeof(uint32)});
        CHECK(memoryAccesses[7] == MemoryAccessInfo{0x600201C, 71, true, sizeof(uint32)});
        CHECK(memoryAccesses[8] == MemoryAccessInfo{0x6002020, 81, true, sizeof(uint32)});
        CHECK(memoryAccesses[9] == MemoryAccessInfo{0x6002024, 91, true, sizeof(uint32)});
    }

    SECTION("DMA D0,MC0,#1 (invalid region)") {
        dsp.programRAM[0].u32 = 0xC0000001;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x1001000;
        dsp.dmaWriteAddr = 0x1002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaDst == 0);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x1001000);  // no hold = update address
        CHECK(dsp.dmaWriteAddr == 0x1002000); // not used = maintain address
        CHECK(dsp.dataRAM[0][0] == 1);
        REQUIRE(memoryAccesses.size() == 0); // no access for invalid region
    }

    SECTION("DMA D0,MC0,#1 (A-Bus)") {
        dsp.programRAM[0].u32 = 0xC0000001;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        MockMemoryRead32(0x2001000, 0x12345678);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x2001000;
        dsp.dmaWriteAddr = 0x2002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaDst == 0);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x2001004);  // no hold = update address
        CHECK(dsp.dmaWriteAddr == 0x2002000); // not used = maintain address
        CHECK(dsp.dataRAM[0][0] == 0x12345678);
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x2001000, 0x12345678, false, sizeof(uint32)});
    }

    SECTION("DMA D0,MC0,#1 (B-Bus)") {
        dsp.programRAM[0].u32 = 0xC0000001;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        MockMemoryRead16(0x5A01000, 0x1234);
        MockMemoryRead16(0x5A01002, 0x5678);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x5A01000;
        dsp.dmaWriteAddr = 0x5A02000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaDst == 0);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x5A01004);  // no hold = update address; B-Bus write always increments by 4
        CHECK(dsp.dmaWriteAddr == 0x5A02000); // not used = maintain address
        CHECK(dsp.dataRAM[0][0] == 0x12345678);
        REQUIRE(memoryAccesses.size() == 2); // 32-bit access broken down into two 16-bit accesses
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x5A01000, 0x1234, false, sizeof(uint16)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x5A01002, 0x5678, false, sizeof(uint16)});
    }

    SECTION("DMA D0,MC0,#1 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0000001;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        MockMemoryRead32(0x6001000, 0x12345678);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaDst == 0);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001004);  // no hold = update address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // not used = maintain address
        CHECK(dsp.dataRAM[0][0] == 0x12345678);
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6001000, 0x12345678, false, sizeof(uint32)});
    }

    SECTION("DMA D0,MC1,#1 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0000101;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        MockMemoryRead32(0x6001000, 0x12345678);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaDst == 1);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001004);  // no hold = update address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // not used = maintain address
        CHECK(dsp.dataRAM[1][1] == 0x12345678);
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6001000, 0x12345678, false, sizeof(uint32)});
    }

    SECTION("DMA D0,MC2,#1 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0000201;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        MockMemoryRead32(0x6001000, 0x12345678);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaDst == 2);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001004);  // no hold = update address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // not used = maintain address
        CHECK(dsp.dataRAM[2][2] == 0x12345678);
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6001000, 0x12345678, false, sizeof(uint32)});
    }

    SECTION("DMA D0,MC3,#1 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0000301;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        MockMemoryRead32(0x6001000, 0x12345678);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaDst == 3);
        CHECK(dsp.dmaAddrInc == 0);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001004);  // no hold = update address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // not used = maintain address
        CHECK(dsp.dataRAM[3][3] == 0x12345678);
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6001000, 0x12345678, false, sizeof(uint32)});
    }

    SECTION("DMA D0,MC3,#4 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0010304;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 0;

        MockMemoryRead32(0x6001000, 0x12345678);
        MockMemoryRead32(0x6001004, 0xDEADBEEF);
        MockMemoryRead32(0x6001008, 0xCAFEBABE);
        MockMemoryRead32(0x600100C, 0xABADF00D);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 0);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 4);
        CHECK(dsp.dmaDst == 3);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001010);  // no hold = update address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // not used = maintain address
        CHECK(dsp.dataRAM[3][0] == 0x12345678);
        CHECK(dsp.dataRAM[3][1] == 0xDEADBEEF);
        CHECK(dsp.dataRAM[3][2] == 0xCAFEBABE);
        CHECK(dsp.dataRAM[3][3] == 0xABADF00D);
        REQUIRE(memoryAccesses.size() == 4);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6001000, 0x12345678, false, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6001004, 0xDEADBEEF, false, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6001008, 0xCAFEBABE, false, sizeof(uint32)});
        CHECK(memoryAccesses[3] == MemoryAccessInfo{0x600100C, 0xABADF00D, false, sizeof(uint32)});
    }

    SECTION("DMAH D0,MC3,#4 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0014304;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 0;

        MockMemoryRead32(0x6001000, 0x12345678);
        MockMemoryRead32(0x6001004, 0xDEADBEEF);
        MockMemoryRead32(0x6001008, 0xCAFEBABE);
        MockMemoryRead32(0x600100C, 0xABADF00D);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 0);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == true);
        CHECK(dsp.dmaCount == 4);
        CHECK(dsp.dmaDst == 3);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == true);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // hold = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // not used = maintain address
        CHECK(dsp.dataRAM[3][0] == 0x12345678);
        CHECK(dsp.dataRAM[3][1] == 0xDEADBEEF);
        CHECK(dsp.dataRAM[3][2] == 0xCAFEBABE);
        CHECK(dsp.dataRAM[3][3] == 0xABADF00D);
        REQUIRE(memoryAccesses.size() == 4);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6001000, 0x12345678, false, sizeof(uint32)});
        CHECK(memoryAccesses[1] == MemoryAccessInfo{0x6001004, 0xDEADBEEF, false, sizeof(uint32)});
        CHECK(memoryAccesses[2] == MemoryAccessInfo{0x6001008, 0xCAFEBABE, false, sizeof(uint32)});
        CHECK(memoryAccesses[3] == MemoryAccessInfo{0x600100C, 0xABADF00D, false, sizeof(uint32)});
    }

    SECTION("DMA D0,MC0,M0 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0012000;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        MockMemoryRead32(0x6001000, 0x12345678);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaDst == 0);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001004);  // no hold = update address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // not used = maintain address
        CHECK(dsp.dataRAM[0][0] == 0x12345678);
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6001000, 0x12345678, false, sizeof(uint32)});
    }

    SECTION("DMA D0,PRG,M0 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0012400;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        MockMemoryRead32(0x6001000, 0x12345678);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaDst == 4);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == false);
        CHECK(dsp.dmaReadAddr == 0x6001004);  // no hold = update address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // not used = maintain address
        CHECK(dsp.dataRAM[0][0] == 1);
        CHECK(dsp.programRAM[0].u32 == 0x12345678);
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6001000, 0x12345678, false, sizeof(uint32)});
    }

    SECTION("DMAH D0,PRG,M0 (WRAM High)") {
        dsp.programRAM[0].u32 = 0xC0016400;
        dsp.dataRAM[0][0] = 1;
        dsp.dataRAM[1][0] = 2;
        dsp.dataRAM[1][1] = 3;
        dsp.dataRAM[2][0] = 4;
        dsp.dataRAM[2][1] = 5;
        dsp.dataRAM[2][2] = 6;
        dsp.dataRAM[3][0] = 7;
        dsp.dataRAM[3][1] = 8;
        dsp.dataRAM[3][2] = 9;
        dsp.dataRAM[3][3] = 10;
        dsp.CT.array[0] = 0;
        dsp.CT.array[1] = 1;
        dsp.CT.array[2] = 2;
        dsp.CT.array[3] = 3;

        MockMemoryRead32(0x6001000, 0x12345678);

        // Setup execution
        dsp.PC = 0;
        dsp.programExecuting = true;
        dsp.programEnded = false;
        dsp.programPaused = false;
        dsp.programStep = false;

        dsp.dmaReadAddr = 0x6001000;
        dsp.dmaWriteAddr = 0x6002000;

        dsp.Run<false>((1 + 1) * 2);

        CHECK(dsp.PC == 2);
        CHECK(dsp.CT.array[0] == 0);
        CHECK(dsp.CT.array[1] == 1);
        CHECK(dsp.CT.array[2] == 2);
        CHECK(dsp.CT.array[3] == 3);
        CHECK(dsp.dmaRun == true);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == true);
        CHECK(dsp.dmaCount == 1);
        CHECK(dsp.dmaDst == 4);
        CHECK(dsp.dmaAddrInc == 4);

        // TODO: should cycle count properly
        dsp.RunDMA<false>(dsp.dmaCount);

        CHECK(dsp.dmaRun == false);
        CHECK(dsp.dmaToD0 == false);
        CHECK(dsp.dmaHold == true);
        CHECK(dsp.dmaReadAddr == 0x6001000);  // hold = maintain address
        CHECK(dsp.dmaWriteAddr == 0x6002000); // not used = maintain address
        CHECK(dsp.dataRAM[0][0] == 1);
        CHECK(dsp.programRAM[0].u32 == 0x12345678);
        REQUIRE(memoryAccesses.size() == 1);
        CHECK(memoryAccesses[0] == MemoryAccessInfo{0x6001000, 0x12345678, false, sizeof(uint32)});
    }

    // TODO: test NT0 and T0 conditions with:
    // "MVI SImm,[d],<cond>"
    // "JMP <cond>,Imm"
    //
    // Needs proper cycle counting for it to work
    //
    // Test DMA region boundaries
    // - mednafen notes that GunBlaze-S does an overly large DMA that exceeds the end of VDP1 VRAM which would write
    //   garbage to VDP1 registers
}

// TODO: test complete programs

// TODO: test DSP control (start, stop, pause, step, etc.)

// TODO: test additional behavior
// - program and data RAM cannot be accessed externally while DSP is running

} // namespace scu_dsp
