#pragma once

#include "cart_base.hpp"

#include <ymir/util/data_ops.hpp>
#include <ymir/util/size_ops.hpp>

#include <algorithm>
#include <array>
#include <span>

namespace ymir::cart {

// Base class for DRAM cartridges.
template <uint8 id, size_t size, CartType type>
class BaseDRAMCartridge : public BaseCartridge {
public:
    BaseDRAMCartridge()
        : BaseCartridge(id, type) {
        Reset(true);
    }

    void Reset(bool hard) final {
        m_ram.fill(0);
    }

    void LoadRAM(std::span<const uint8, size> out) {
        std::copy(out.begin(), out.end(), m_ram.begin());
    }

    void DumpRAM(std::span<uint8, size> out) const {
        std::copy(m_ram.begin(), m_ram.end(), out.begin());
    }

protected:
    std::array<uint8, size> m_ram;
};

// ---------------------------------------------------------------------------------------------------------------------

// 8 Mbit (1 MiB) DRAM cartridge.
// Lower 512 KiB mapped to 0x240'0000..0x24F'FFFF, mirrored twice
// Upper 512 KiB mapped to 0x260'0000..0x26F'FFFF, mirrored twice
class DRAM8MbitCartridge final : public BaseDRAMCartridge<0x5A, 1_MiB, CartType::DRAM8Mbit> {
public:
    uint8 ReadByte(uint32 address) const override {
        switch (address >> 20) {
        case 0x24: return m_ram[address & 0x7FFFF];
        case 0x26: return m_ram[(address & 0x7FFFF) | 0x80000];
        default: return 0xFFu;
        }
    }

    uint16 ReadWord(uint32 address) const override {
        switch (address >> 20) {
        case 0x24: return util::ReadBE<uint16>(&m_ram[address & 0x7FFFF]);
        case 0x26: return util::ReadBE<uint16>(&m_ram[(address & 0x7FFFF) | 0x80000]);
        default: return 0xFFFFu;
        }
    }

    void WriteByte(uint32 address, uint8 value) override {
        switch (address >> 20) {
        case 0x24: m_ram[address & 0x7FFFF] = value; break;
        case 0x26: m_ram[(address & 0x7FFFF) | 0x80000] = value; break;
        }
    }

    void WriteWord(uint32 address, uint16 value) override {
        switch (address >> 20) {
        case 0x24: util::WriteBE<uint16>(&m_ram[address & 0x7FFFF], value); break;
        case 0x26: util::WriteBE<uint16>(&m_ram[(address & 0x7FFFF) | 0x80000], value); break;
        }
    }

    uint8 PeekByte(uint32 address) const override {
        return ReadByte(address);
    }
    uint16 PeekWord(uint32 address) const override {
        return ReadWord(address);
    }

    void PokeByte(uint32 address, uint8 value) override {
        WriteByte(address, value);
    }
    void PokeWord(uint32 address, uint16 value) override {
        WriteWord(address, value);
    }
};

// ---------------------------------------------------------------------------------------------------------------------

// 32 Mbit (4 MiB) DRAM cartridge.
// Mapped to 0x240'0000..0x27F'FFFF
class DRAM32MbitCartridge final : public BaseDRAMCartridge<0x5C, 4_MiB, CartType::DRAM32Mbit> {
public:
    uint8 ReadByte(uint32 address) const override {
        if (util::AddressInRange<0x240'0000, 0x27F'FFFF>(address)) {
            return m_ram[address & 0x3FFFFF];
        } else {
            return 0xFFu;
        }
    }

    uint16 ReadWord(uint32 address) const override {
        if (util::AddressInRange<0x240'0000, 0x27F'FFFF>(address)) {
            return util::ReadBE<uint16>(&m_ram[address & 0x3FFFFF]);
        } else {
            return 0xFFFFu;
        }
    }

    void WriteByte(uint32 address, uint8 value) override {
        if (util::AddressInRange<0x240'0000, 0x27F'FFFF>(address)) {
            m_ram[address & 0x3FFFFF] = value;
        }
    }

    void WriteWord(uint32 address, uint16 value) override {
        if (util::AddressInRange<0x240'0000, 0x27F'FFFF>(address)) {
            util::WriteBE<uint16>(&m_ram[address & 0x3FFFFF], value);
        }
    }

    uint8 PeekByte(uint32 address) const override {
        return ReadByte(address);
    }
    uint16 PeekWord(uint32 address) const override {
        return ReadWord(address);
    }

    void PokeByte(uint32 address, uint8 value) override {
        WriteByte(address, value);
    }
    void PokeWord(uint32 address, uint16 value) override {
        WriteWord(address, value);
    }
};

// ---------------------------------------------------------------------------------------------------------------------

// 48 Mbit (6 MiB) DRAM dev cartridge.
// Mapped to 0x400'0000..0x45F'FFFF
class DRAM48MbitCartridge final : public BaseDRAMCartridge<0x5C, 6_MiB, CartType::DRAM48Mbit> {
public:
    uint8 ReadByte(uint32 address) const override {
        if (util::AddressInRange<0x400'0000, 0x45F'FFFF>(address)) {
            return m_ram[address & 0x7FFFFF];
        } else {
            return 0xFFu;
        }
    }

    uint16 ReadWord(uint32 address) const override {
        if (util::AddressInRange<0x400'0000, 0x45F'FFFF>(address)) {
            return util::ReadBE<uint16>(&m_ram[address & 0x7FFFFF]);
        } else {
            return 0xFFFFu;
        }
    }

    void WriteByte(uint32 address, uint8 value) override {
        if (util::AddressInRange<0x400'0000, 0x45F'FFFF>(address)) {
            m_ram[address & 0x7FFFFF] = value;
        }
    }

    void WriteWord(uint32 address, uint16 value) override {
        if (util::AddressInRange<0x400'0000, 0x45F'FFFF>(address)) {
            util::WriteBE<uint16>(&m_ram[address & 0x7FFFFF], value);
        }
    }

    uint8 PeekByte(uint32 address) const override {
        return ReadByte(address);
    }
    uint16 PeekWord(uint32 address) const override {
        return ReadWord(address);
    }

    void PokeByte(uint32 address, uint8 value) override {
        WriteByte(address, value);
    }
    void PokeWord(uint32 address, uint16 value) override {
        WriteWord(address, value);
    }
};

} // namespace ymir::cart
