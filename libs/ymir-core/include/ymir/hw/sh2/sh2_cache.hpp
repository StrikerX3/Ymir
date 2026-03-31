#pragma once

#include <ymir/savestate/savestate_sh2.hpp>

#include <ymir/core/types.hpp>

#include <ymir/hw/hw_defs.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/inline.hpp>

#include <array>
#include <cassert>

namespace ymir::sh2 {

// -----------------------------------------------------------------------------
// Registers

// addr r/w  access   init      code    name
// 092  R/W  8        00        CCR     Cache Control Register
//
//   bits   r/w  code   description
//      7   R/W  W1     Way Specification (MSB)
//      6   R/W  W0     Way Specification (LSB)
//      5   R    -      Reserved - must be zero
//      4   R/W  CP     Cache Purge (0=normal, 1=purge)
//      3   R/W  TW     Two-Way Mode (0=four-way, 1=two-way)
//      2   R/W  OD     Data Replacement Disable (0=disabled, 1=data cache not updated on miss)
//      1   R/W  ID     Instruction Replacement Disabled (same as above, but for code cache)
//      0   R/W  CE     Cache Enable (0=disable, 1=enable)
struct RegCCR {
    RegCCR() {
        Reset();
    }

    void Reset() {
        CE = false;
        ID = false;
        OD = false;
        TW = false;
        CP = false;
        Wn = 0;
    }

    FORCE_INLINE uint8 Read() const {
        uint8 value = 0;
        bit::deposit_into<0>(value, CE);
        bit::deposit_into<1>(value, ID);
        bit::deposit_into<2>(value, OD);
        bit::deposit_into<3>(value, TW);
        bit::deposit_into<4>(value, CP);
        bit::deposit_into<6, 7>(value, Wn);
        return value;
    }

    FORCE_INLINE void Write(uint8 value) {
        CE = bit::test<0>(value);
        ID = bit::test<1>(value);
        OD = bit::test<2>(value);
        TW = bit::test<3>(value);
        CP = bit::test<4>(value);
        Wn = bit::extract<6, 7>(value);
    }

    bool CE;
    bool ID;
    bool OD;
    bool TW;
    bool CP;
    uint8 Wn;
};

// 0E0, 0E2, 0E4 are in INTC module

// -----------------------------------------------------------------------------
// Cache implementation

inline constexpr std::size_t kCacheWays = 4;
inline constexpr std::size_t kCacheEntries = 64;
inline constexpr std::size_t kCacheLineSize = 16;

FORCE_INLINE bool IsValidCacheWay(uint8 way) {
    return way < kCacheWays;
}

struct CacheEntry {
    // Tag layout:
    //   28..10: tag
    //        2: valid bit
    // All other bits must be zero
    // This matches the address array structure
    union Tag {
        uint32 u32;
        struct {
            uint32 : 2;
            uint32 valid : 1;
            uint32 : 7;
            uint32 tagAddress : 19;
            uint32 : 3;
        };
    };
    static_assert(sizeof(Tag) == sizeof(uint32));

    alignas(16) std::array<Tag, kCacheWays> tag;
    alignas(16) std::array<std::array<uint8, kCacheLineSize>, kCacheWays> line;

    FORCE_INLINE uint8 FindWay(uint32 address) const {
        const uint32 tagAddress = (bit::extract<10, 28>(address) << 10) | (1 << 2);

        if (tag[0].u32 == tagAddress) {
            return 0;
        }
        if (tag[1].u32 == tagAddress) {
            return 1;
        }
        if (tag[2].u32 == tagAddress) {
            return 2;
        }
        if (tag[3].u32 == tagAddress) {
            return 3;
        }
        return 4;
    }
};

// Stores the cache LRU update bits
struct CacheLRUUpdateBits {
    uint8 andMask;
    uint8 orMask;
};

class Cache {
    alignas(16) static constexpr std::array<CacheLRUUpdateBits, 4> kCacheLRUUpdateBits = {{
        // AND      OR
        {0b000111u, 0b000000u}, // way 0: 000...
        {0b011001u, 0b100000u}, // way 1: 1..00.
        {0b101010u, 0b010100u}, // way 2: .1.1.0
        {0b110100u, 0b001011u}, // way 3: ..1.11
    }};

    alignas(16) static constexpr auto kCacheLRUWaySelect = [] {
        std::array<uint8, 64> arr{};
        arr.fill(kCacheWays);
        for (uint8 i = 0; i < 8; i++) {
            arr[0b111000 | bit::scatter<0b000111>(i)] = 0; // way 0: 111...
            arr[0b000110 | bit::scatter<0b011001>(i)] = 1; // way 1: 0..11.
            arr[0b000001 | bit::scatter<0b101010>(i)] = 2; // way 2: .0.0.1
            arr[0b000000 | bit::scatter<0b110100>(i)] = 3; // way 3: ..0.00
        }
        return arr;
    }();

public:
    Cache() {
        Reset();
    }

    void Reset() {
        CCR.Reset();
        m_entries.fill({});
        m_lru.fill(0);
        m_replaceANDMask = 0x3Fu;
        m_replaceORMask[false] = 0u;
        m_replaceORMask[true] = 0u;
    }

    FORCE_INLINE CacheEntry &GetEntry(uint32 address) {
        const uint32 index = bit::extract<4, 9>(address);
        return m_entries[index];
    }

    FORCE_INLINE const CacheEntry &GetEntry(uint32 address) const {
        const uint32 index = bit::extract<4, 9>(address);
        return m_entries[index];
    }

    template <bool instrFetch>
    FORCE_INLINE uint8 GetWayFromLRU(uint8 lru) {
        assert(lru <= 63);
        return kCacheLRUWaySelect[lru & m_replaceANDMask] | m_replaceORMask[instrFetch];
    }

    template <bool instrFetch>
    FORCE_INLINE uint8 SelectWay(uint32 address) {
        const uint32 index = bit::extract<4, 9>(address);
        const uint8 lru = m_lru[index];
        const uint8 way = GetWayFromLRU<instrFetch>(lru);
        if (IsValidCacheWay(way)) {
            const uint32 tagAddress = bit::extract<10, 28>(address);
            m_entries[index].tag[way].tagAddress = tagAddress;
            m_entries[index].tag[way].valid = 1;
        }
        return way;
    }

    FORCE_INLINE uint8 GetLRU(uint8 index) {
        assert(index < kCacheEntries);
        return m_lru[index];
    }

    FORCE_INLINE void SetLRU(uint8 index, uint8 lru) {
        assert(index < kCacheEntries);
        m_lru[index] = lru;
    }

    FORCE_INLINE void UpdateLRU(uint32 address, uint8 way) {
        const uint32 index = bit::extract<4, 9>(address);
        m_lru[index] &= kCacheLRUUpdateBits[way].andMask;
        m_lru[index] |= kCacheLRUUpdateBits[way].orMask;
    }

    FORCE_INLINE void AssociativePurge(uint32 address) {
        const uint32 index = bit::extract<4, 9>(address);
        const uint32 tagAddress = bit::extract<10, 28>(address);
        for (auto &tag : m_entries[index].tag) {
            tag.valid &= tag.tagAddress != tagAddress;
        }
    }

    template <bool peek>
    FORCE_INLINE uint32 ReadAddressArray(uint32 address) const {
        const uint32 index = bit::extract<4, 9>(address);
        const uint8 lru = m_lru[index];
        const uint8 way = peek ? bit::extract<2, 3>(address) : CCR.Wn;
        return m_entries[index].tag[way].u32 | (lru << 4u);
    }

    template <mem_primitive T, bool poke>
    FORCE_INLINE void WriteAddressArray(uint32 address, T value) {
        const uint32 index = bit::extract<4, 9>(address);
        if constexpr (poke) {
            uint32 currValue;
            const uint8 way = bit::extract<2, 3>(address);
            if constexpr (std::is_same_v<T, uint8>) {
                currValue = m_entries[index].tag[way].u32 | (m_lru[index] << 4u);
                switch (address & 3) {
                case 0: bit::deposit_into<24, 31>(currValue, value); break;
                case 1: bit::deposit_into<16, 23>(currValue, value); break;
                case 2: bit::deposit_into<8, 15>(currValue, value); break;
                case 3: bit::deposit_into<0, 7>(currValue, value); break;
                }
            } else if constexpr (std::is_same_v<T, uint16>) {
                currValue = m_entries[index].tag[way].u32 | (m_lru[index] << 4u);
                switch (address & 2) {
                case 0: bit::deposit_into<16, 31>(currValue, value); break;
                case 2: bit::deposit_into<0, 15>(currValue, value); break;
                }
            } else {
                currValue = value;
            }
            m_entries[index].tag[way].u32 = currValue & 0x1FFFFC04;
            m_lru[index] = bit::extract<4, 9>(currValue);
        } else {
            m_entries[index].tag[CCR.Wn].u32 = address & 0x1FFFFC04;
            m_lru[index] = bit::extract<4, 9>(value);
        }
    }

    template <mem_primitive T>
    FORCE_INLINE T ReadDataArray(uint32 address) const {
        const uint32 index = bit::extract<4, 9>(address);
        const uint32 way = bit::extract<10, 11>(address);
        const uint32 byte = bit::extract<0, 3>(address) ^ (4 - sizeof(T));
        const auto &line = m_entries[index].line[way];
        return util::ReadNE<T>(&line[byte]);
    }

    template <mem_primitive T>
    FORCE_INLINE void WriteDataArray(uint32 address, T value) {
        const uint32 index = bit::extract<4, 9>(address);
        const uint32 way = bit::extract<10, 11>(address);
        const uint32 byte = bit::extract<0, 3>(address) ^ (4 - sizeof(T));
        auto &line = m_entries[index].line[way];
        util::WriteNE<T>(&line[byte], value);
    }

    FORCE_INLINE void Purge() {
        for (uint32 index = 0; index < 64; index++) {
            for (auto &tag : m_entries[index].tag) {
                tag.valid = 0;
            }
            m_lru[index] = 0;
        }
    }

    // -------------------------------------------------------------------------
    // Registers

    RegCCR CCR; // 092  R/W  8        00        CCR     Cache Control Register

    FORCE_INLINE uint8 ReadCCR() const {
        return CCR.Read();
    }

    template <bool poke>
    FORCE_INLINE void WriteCCR(uint8 value) {
        CCR.Write(value);
        m_replaceANDMask = CCR.TW ? 0x1u : 0x3Fu;
        m_replaceORMask[false] = CCR.OD ? -1 : 0;
        m_replaceORMask[true] = CCR.ID ? -1 : 0;
        if (CCR.CP) {
            if constexpr (!poke) {
                Purge();
            }
            CCR.CP = false;
        }
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(savestate::SH2SaveState::Cache &state) const {
        state.CCR = ReadCCR();
        for (size_t i = 0; i < 64; i++) {
            for (size_t j = 0; j < 4; j++) {
                state.entries[i].tags[j] = m_entries[i].tag[j].u32;
            }
            state.entries[i].lines = m_entries[i].line;
        }
        state.lru = m_lru;
    }

    void LoadState(const savestate::SH2SaveState::Cache &state) {
        WriteCCR<true>(state.CCR);
        for (size_t i = 0; i < 64; i++) {
            for (size_t j = 0; j < 4; j++) {
                m_entries[i].tag[j].u32 = state.entries[i].tags[j];
            }
            m_entries[i].line = state.entries[i].lines;
        }
        m_lru = state.lru;
    }

    // -------------------------------------------------------------------------
    // Debugging

    FORCE_INLINE CacheEntry &GetEntryByIndex(uint8 index) {
        assert(index < kCacheEntries);
        return m_entries[index];
    }

    FORCE_INLINE const CacheEntry &GetEntryByIndex(uint8 index) const {
        assert(index < kCacheEntries);
        return m_entries[index];
    }

private:
    alignas(16) std::array<CacheEntry, kCacheEntries> m_entries;
    alignas(16) std::array<uint8, kCacheEntries> m_lru;
    uint8 m_replaceANDMask;
    std::array<sint8, 2> m_replaceORMask; // [0]=data, [1]=code
};

} // namespace ymir::sh2
