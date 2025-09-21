#include <ymir/hw/cdblock/ygr.hpp>

#include "cdblock_devlog.hpp"

#include <ymir/util/inline.hpp>

#include <cstdio>

namespace ymir::cdblock {

YGR::YGR() {
    Reset();
}

void YGR::Reset() {
    m_regs.TRCTL.u16 = 0x0000;
    m_regs.CDIRQL.u16 = 0x0000;
    m_regs.CDIRQU.u16 = 0x0000;
    m_regs.CDMSKL.u16 = 0x0000;
    m_regs.CDMSKU.u16 = 0x0000;
    m_regs.REG0C.u16 = 0x0000;
    m_regs.REG0E = 0x0000;
    m_regs.CR.fill(0x0000);
    m_regs.RR.fill(0x0000);
    m_regs.REG18.u16 = 0x0000;
    m_regs.REG1A.u16 = 0x0000;
    m_regs.REG1C.u16 = 0x0000;
    m_regs.HIRQ = 0x0000;
    m_regs.HIRQMASK = 0x0000;

    m_fifo.data.fill(0);
    m_fifo.readPos = 0;
    m_fifo.writePos = 0;
    m_fifo.count = 0;
    UpdateFIFODREQ();
}

void YGR::MapMemory(sys::SH2Bus &mainBus, sys::SH1Bus &cdbBus) {
    static constexpr auto cast = [](void *ctx) -> YGR & { return *static_cast<YGR *>(ctx); };

    // -------------------------------------------------------------------------
    // Main (SH-2) bus mappings

    // CD Block registers are mirrored every 64 bytes in a 4 KiB block.
    // These 4 KiB blocks are mapped every 32 KiB.

    for (uint32 address = 0x580'0000; address <= 0x58F'FFFF; address += 0x8000) {
        mainBus.MapNormal(
            address, address + 0xFFF, this,
            [](uint32 address, void *ctx) -> uint16 { return cast(ctx).HostReadWord<false>(address); },
            [](uint32 address, void *ctx) -> uint32 {
                uint32 value = cast(ctx).HostReadWord<false>(address + 0) << 16u;
                value |= cast(ctx).HostReadWord<false>(address + 2) << 0u;
                return value;
            },
            [](uint32 address, uint16 value, void *ctx) { cast(ctx).HostWriteWord<false>(address, value); },
            [](uint32 address, uint32 value, void *ctx) {
                cast(ctx).HostWriteWord<false>(address + 0, value >> 16u);
                cast(ctx).HostWriteWord<false>(address + 2, value >> 0u);
            });

        mainBus.MapSideEffectFree(
            address, address + 0xFFF, this,
            [](uint32 address, void *ctx) -> uint8 { return cast(ctx).HostPeekByte(address); },
            [](uint32 address, void *ctx) -> uint16 { return cast(ctx).HostReadWord<true>(address); },
            [](uint32 address, void *ctx) -> uint32 {
                uint32 value = cast(ctx).HostReadWord<true>(address + 0) << 16u;
                value |= cast(ctx).HostReadWord<true>(address + 2) << 0u;
                return value;
            },
            [](uint32 address, uint8 value, void *ctx) { cast(ctx).HostPokeByte(address, value); },
            [](uint32 address, uint16 value, void *ctx) { cast(ctx).HostWriteWord<true>(address, value); },
            [](uint32 address, uint32 value, void *ctx) {
                cast(ctx).HostWriteWord<true>(address + 0, value >> 16u);
                cast(ctx).HostWriteWord<true>(address + 2, value >> 0u);
            });
    }

    // -------------------------------------------------------------------------
    // CD Block (SH-1) bus mappings

    cdbBus.MapNormal(
        0xA000000, 0xCFFFFFF, this, //
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).CDBReadWord(address); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).CDBWriteWord(address, value); });
}

FORCE_INLINE uint16 YGR::CDBReadWord(uint32 address) const {
    if (((address >> 20) & 0xF) == 0x1) {
        // TODO: read from Video CD Card registers instead
        return 0;
    }

    address &= 0xFFFF;
    switch (address) {
    case 0x00: //
    {
        const uint16 value = m_fifo.Read<false>();
        UpdateFIFODREQ();
        return value;
    }
    case 0x02: return m_regs.TRCTL.u16;
    case 0x04: return m_regs.CDIRQL.u16;
    case 0x06: return m_regs.CDIRQU.u16;
    case 0x08: return m_regs.CDMSKL.u16;
    case 0x0A: return m_regs.CDMSKU.u16;
    case 0x0C: return m_regs.REG0C.u16;
    case 0x0E: return m_regs.REG0E;
    case 0x10: return m_regs.CR[0];
    case 0x12: return m_regs.CR[1];
    case 0x14: return m_regs.CR[2];
    case 0x16: return m_regs.CR[3];
    case 0x18: return m_regs.REG18.u16;
    case 0x1A: return m_regs.REG1A.u16;
    case 0x1C: return m_regs.REG1C.u16;
    case 0x1E: return m_regs.HIRQ;
    default: devlog::trace<grp::ygr_regs>("Unhandled 16-bit CD Block YGR read from {:02X}", address); return 0u;
    }
}

FORCE_INLINE void YGR::CDBWriteWord(uint32 address, uint16 value) {
    if (((address >> 20) & 0xF) == 0x1) {
        // TODO: write to Video CD Card registers instead
        return;
    }

    address &= 0xFFFF;
    switch (address) {
    case 0x00:
        m_fifo.Write<false>(value);
        UpdateFIFODREQ();
        break;
    case 0x02:
        m_regs.TRCTL.u16 = value & 0xF;
        if (m_regs.TRCTL.RES) {
            m_fifo.Clear();
        }
        UpdateFIFODREQ();
        break;
    case 0x04: m_regs.CDIRQL.u16 = value & 0x3; break;
    case 0x06: m_regs.CDIRQU.u16 &= value; break;
    case 0x08: m_regs.CDMSKL.u16 = value & 0x3; break;
    case 0x0A: m_regs.CDMSKU.u16 = value & 0x70; break;
    case 0x0C: m_regs.REG0C.u16 = value & 0x3; break;
    case 0x0E: m_regs.REG0E = value; break;
    case 0x10: m_regs.RR[0] = value; break;
    case 0x12: m_regs.RR[1] = value; break;
    case 0x14: m_regs.RR[2] = value; break;
    case 0x16:
        m_regs.RR[3] = value;
        devlog::trace<grp::ygr_cr>("CDB  RR writes: {:04X} {:04X} {:04X} {:04X}", m_regs.RR[0], m_regs.RR[1],
                                   m_regs.RR[2], m_regs.RR[3]);
        break;
    case 0x18: m_regs.REG18.u16 = value & 0x3F; break;
    case 0x1A: m_regs.REG1A.u16 = value & 0xD7; break;
    case 0x1C: m_regs.REG1C.u16 = value & 0xFF; break;
    case 0x1E:
        m_regs.HIRQ |= value;
        UpdateInterrupts();
        break;
    default:
        devlog::trace<grp::ygr_regs>("Unhandled 16-bit CD Block YGR write to {:02X} = {:04X}", address, value);
        break;
    }
}

template <bool peek>
FORCE_INLINE uint16 YGR::HostReadWord(uint32 address) const {
    address &= 0x3C;
    switch (address) {
    case 0x00:
        if (m_regs.TRCTL.DIR && !peek) {
            return 0u;
        } else {
            const uint16 value = m_fifo.Read<peek>();
            if constexpr (!peek) {
                UpdateFIFODREQ();
            }
            return value;
        }
    case 0x08: return m_regs.HIRQ;
    case 0x0C: return m_regs.HIRQMASK;
    case 0x18: return m_regs.RR[0];
    case 0x1C: return m_regs.RR[1];
    case 0x20: return m_regs.RR[2];
    case 0x24: return m_regs.RR[3];
    case 0x28: return 0u; // TODO: read MPEGRGB
    default:
        if constexpr (!peek) {
            devlog::trace<grp::ygr_regs>("Unhandled 16-bit host YGR read from {:02X}", address);
        }
        return 0u;
    }
}

template <bool poke>
FORCE_INLINE void YGR::HostWriteWord(uint32 address, uint16 value) {
    address &= 0x3C;
    switch (address) {
    case 0x00:
        if (m_regs.TRCTL.DIR && !poke) {
            m_fifo.Write<poke>(value);
            if constexpr (!poke) {
                UpdateFIFODREQ();
            }
        }
        break;
    case 0x08:
        if constexpr (poke) {
            m_regs.HIRQ = value & 0x3FFF;
        } else {
            m_regs.HIRQ &= value;
            UpdateInterrupts();
        }
        break;
    case 0x0C:
        m_regs.HIRQMASK = value;
        if constexpr (!poke) {
            UpdateInterrupts();
        }
        break;
    case 0x18: m_regs.CR[0] = value; break;
    case 0x1C: m_regs.CR[1] = value; break;
    case 0x20: m_regs.CR[2] = value; break;
    case 0x24:
        m_regs.CR[3] = value;
        if constexpr (!poke) {
            m_cbAssertIRQ6();
            devlog::trace<grp::ygr_cr>("Host CR writes: {:04X} {:04X} {:04X} {:04X}", m_regs.CR[0], m_regs.CR[1],
                                       m_regs.CR[2], m_regs.CR[3]);
        }
        break;
    case 0x28: /* TODO: write MPEGRGB */ break;
    default:
        if constexpr (!poke) {
            devlog::trace<grp::ygr_regs>("Unhandled 16-bit host YGR write to {:02X} = {:04X}", address, value);
        }
        break;
    }
}

uint8 YGR::HostPeekByte(uint32 address) const {
    address &= 0x3D;
    switch (address) {
    case 0x00: return m_fifo.Read<true>() >> 8u;
    case 0x01: return m_fifo.Read<true>();
    case 0x08: return m_regs.HIRQ >> 8u;
    case 0x09: return m_regs.HIRQ;
    case 0x0C: return m_regs.HIRQMASK >> 8u;
    case 0x0D: return m_regs.HIRQMASK;
    case 0x18: return m_regs.RR[0] >> 8u;
    case 0x19: return m_regs.RR[0];
    case 0x1C: return m_regs.RR[1] >> 8u;
    case 0x1D: return m_regs.RR[1];
    case 0x20: return m_regs.RR[2] >> 8u;
    case 0x21: return m_regs.RR[2];
    case 0x24: return m_regs.RR[3] >> 8u;
    case 0x25: return m_regs.RR[3];
    case 0x28: return 0u >> 8u; // TODO: read MPEGRGB
    case 0x29: return 0u;       // TODO: read MPEGRGB
    default: return 0u;
    }
}

void YGR::HostPokeByte(uint32 address, uint8 value) {
    address &= 0x3C;
    switch (address) {
    case 0x00: //
    {
        uint16 fifoVal = m_fifo.Read<true>();
        bit::deposit_into<8, 15>(fifoVal, value);
        m_fifo.Write<true>(fifoVal);
        break;
    }
    case 0x01: //
    {
        uint16 fifoVal = m_fifo.Read<true>();
        bit::deposit_into<0, 7>(fifoVal, value);
        m_fifo.Write<true>(fifoVal);
        break;
    }
    case 0x08: bit::deposit_into<8, 13>(m_regs.HIRQ, value); break;
    case 0x09: bit::deposit_into<0, 7>(m_regs.HIRQ, value); break;
    case 0x0C: bit::deposit_into<8, 15>(m_regs.HIRQMASK, value); break;
    case 0x0D: bit::deposit_into<0, 7>(m_regs.HIRQMASK, value); break;
    case 0x18: bit::deposit_into<8, 15>(m_regs.CR[0], value); break;
    case 0x19: bit::deposit_into<0, 7>(m_regs.CR[0], value); break;
    case 0x1C: bit::deposit_into<8, 15>(m_regs.CR[1], value); break;
    case 0x1D: bit::deposit_into<0, 7>(m_regs.CR[1], value); break;
    case 0x20: bit::deposit_into<8, 15>(m_regs.CR[2], value); break;
    case 0x21: bit::deposit_into<0, 7>(m_regs.CR[2], value); break;
    case 0x24: bit::deposit_into<8, 15>(m_regs.CR[3], value); break;
    case 0x25: bit::deposit_into<0, 7>(m_regs.CR[3], value); break;
    case 0x28: /* TODO: write MPEGRGB */ break;
    case 0x29: /* TODO: write MPEGRGB */ break;
    }
}

void YGR::UpdateInterrupts() {
    devlog::debug<grp::base>("HIRQ = {:04X}  mask = {:04X}  active = {:04X}", m_regs.HIRQ, m_regs.HIRQMASK,
                             m_regs.HIRQ & m_regs.HIRQMASK);
    if (m_regs.HIRQ & m_regs.HIRQMASK) {
        m_cbTriggerExternalInterrupt0();
    }
}

void YGR::UpdateFIFODREQ() const {
    // DREQ is asserted when doing a read transfer and there is room in the FIFO.
    // DREQ is deasserted if:
    // - transfers are disabled (TRCTL.TE=0)
    // - the FIFO is full
    // - the FIFO is empty when doing a write (put) transfer
    m_cbSetDREQ1n(!m_regs.TRCTL.TE || m_fifo.IsFull() || (m_regs.TRCTL.DIR && m_fifo.IsEmpty()));
}

void YGR::DiscChanged() {
    m_regs.HIRQ |= kHIRQ_DCHG | kHIRQ_EFLS;
    UpdateInterrupts();
}

} // namespace ymir::cdblock
