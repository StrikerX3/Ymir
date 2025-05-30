#pragma once

#include "cart_base.hpp"

namespace ymir::cart {

class NoCartridge final : public BaseCartridge {
public:
    NoCartridge()
        : BaseCartridge(0xFFu, CartType::None) {}

    uint8 ReadByte(uint32 address) const final {
        return 0xFFu;
    }
    uint16 ReadWord(uint32 address) const final {
        return 0xFFFFu;
    }

    void WriteByte(uint32 address, uint8 value) final {}
    void WriteWord(uint32 address, uint16 value) final {}

    uint8 PeekByte(uint32 address) const final {
        return 0xFFu;
    }
    uint16 PeekWord(uint32 address) const final {
        return 0xFFFFu;
    }

    void PokeByte(uint32 address, uint8 value) final {}
    void PokeWord(uint32 address, uint16 value) final {}
};

} // namespace ymir::cart
