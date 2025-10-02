#include <ymir/hw/cdblock/ygr.hpp>

#include "cdblock_devlog.hpp"

#include <ymir/util/dev_assert.hpp>
#include <ymir/util/inline.hpp>

#include <cstdio>

namespace ymir::cdblock {

// -----------------------------------------------------------------------------
// Debugger

FORCE_INLINE static void TraceReceiveHostCommand(debug::IYGRTracer *tracer, uint16 cr1, uint16 cr2, uint16 cr3,
                                                 uint16 cr4) {
    if (tracer) {
        return tracer->ReceiveHostCommand(cr1, cr2, cr3, cr4);
    }
}

FORCE_INLINE static void TraceReceiveCDBlockResponse(debug::IYGRTracer *tracer, uint16 rr1, uint16 rr2, uint16 rr3,
                                                     uint16 rr4) {
    if (tracer) {
        return tracer->ReceiveCDBlockResponse(rr1, rr2, rr3, rr4);
    }
}

// -----------------------------------------------------------------------------
// Implementation

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

void YGR::MapMemory(sys::SH1Bus &cdbBus) {
    static constexpr auto cast = [](void *ctx) -> YGR & { return *static_cast<YGR *>(ctx); };

    // -------------------------------------------------------------------------
    // CD Block (SH-1) bus mappings

    cdbBus.MapNormal(
        0xA000000, 0xCFFFFFF, this, //
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).CDBReadWord<false>(address); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).CDBWriteWord<false>(address, value); });

    cdbBus.MapSideEffectFree(
        0xA000000, 0xCFFFFFF, this, //
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).CDBPeekByte(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).CDBReadWord<true>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).CDBReadWord<true>(address + 0) << 16u;
            value |= cast(ctx).CDBReadWord<true>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).CDBPokeByte(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).CDBWriteWord<true>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).CDBWriteWord<true>(address + 0, value >> 16u);
            cast(ctx).CDBWriteWord<true>(address + 2, value >> 0u);
        });
}

void YGR::MapMemory(sys::SH2Bus &mainBus) {
    static constexpr auto cast = [](void *ctx) -> YGR & { return *static_cast<YGR *>(ctx); };

    // -------------------------------------------------------------------------
    // Main (SH-2) bus mappings

    // CD Block registers are mirrored every 64 bytes in a 4 KiB block.
    // These 4 KiB blocks are mapped every 32 KiB.

    for (uint32 address = 0x580'0000; address <= 0x58F'FFFF; address += 0x8000) {
        mainBus.MapNormal(
            address, address + 0xFFF, this,
            [](uint32 address, void *ctx) -> uint16 { return cast(ctx).HostReadWord<false>(address); },
            // 32-bit reads are weird and handled separately

            [](uint32 address, uint16 value, void *ctx) { cast(ctx).HostWriteWord<false>(address, value); },
            [](uint32 address, uint32 value, void *ctx) {
                cast(ctx).HostWriteWord<false>(address + 0, value >> 16u);
                cast(ctx).HostWriteWord<false>(address + 2, value >> 0u);
            },

            // Bus wait handler
            [](uint32 address, uint32 size, bool write, void *ctx) -> bool {
                if ((address & 0x3C) != 0x00) {
                    // Only the FIFO register stalls
                    return false;
                }

                auto &ygr = cast(ctx);
                if (!ygr.m_regs.TRCTL.TE) {
                    // No need to stall if transfer is disabled
                    return false;
                }

                if (write) {
                    return ygr.m_fifo.Free() < size / sizeof(uint16);
                } else {
                    return ygr.m_fifo.Used() < size / sizeof(uint16);
                }
            });

        // 32-bit reads from FIFO at 0x25890000 pull one word.
        // 32-bit reads from FIFO at 0x25810000 pull two words.
        if (address & 0x80000) {
            mainBus.MapNormal(address, address + 0xFFF, this, [](uint32 address, void *ctx) -> uint32 {
                return cast(ctx).HostReadLong<false>(address);
            });
        } else {
            mainBus.MapNormal(address, address + 0xFFF, this, [](uint32 address, void *ctx) -> uint32 {
                return cast(ctx).HostReadLong<true>(address);
            });
        }

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
}

// -----------------------------------------------------------------------------
// CD block bus

template <bool peek>
FORCE_INLINE uint16 YGR::CDBReadWord(uint32 address) const {
    if (((address >> 20) & 0xF) == 0x1) {
        // TODO: read from Video CD Card registers instead
        return 0;
    }

    address &= 0xFFFF;
    switch (address) {
    case 0x00: //
    {
        if constexpr (!peek) {
            if (!m_regs.TRCTL.TE && m_regs.TRCTL.DIR && m_fifo.count == 1) {
                m_regs.TRCTL.TE = 1;
            }
        }
        const uint16 value = m_fifo.Read<peek>();
        if constexpr (!peek) {
            devlog::trace<grp::ygr_fifo>("CDB  FIFO read  <- rd={:X} wr={:X} cnt={:X}  {:04X}", m_fifo.readPos,
                                         m_fifo.writePos, m_fifo.count, value);
            UpdateFIFODREQ();
        }
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
    default:
        if constexpr (!peek) {
            devlog::trace<grp::ygr_regs>("Unhandled 16-bit CD Block YGR read from {:02X}", address);
        }
        return 0u;
    }
}

template <bool poke>
FORCE_INLINE void YGR::CDBWriteWord(uint32 address, uint16 value) {
    if (((address >> 20) & 0xF) == 0x1) {
        // TODO: write to Video CD Card registers instead
        return;
    }

    address &= 0xFFFF;
    switch (address) {
    case 0x00:
        m_fifo.Write<poke>(value);
        if constexpr (!poke) {
            if (m_regs.TRCTL.TE && m_regs.TRCTL.DIR) {
                m_regs.TRCTL.TE = 0;
            }
            devlog::trace<grp::ygr_fifo>("CDB  FIFO write -> rd={:X} wr={:X} cnt={:X}  {:04X}", m_fifo.readPos,
                                         m_fifo.writePos, m_fifo.count, value);
            UpdateFIFODREQ();
        }
        break;
    case 0x02:
        m_regs.TRCTL.u16 = value & 0xF;
        if constexpr (!poke) {
            if (m_regs.TRCTL.RES) {
                m_fifo.Clear();
                devlog::trace<grp::ygr_fifo>("CDB  FIFO reset -- rd={:X} wr={:X} cnt={:X}", m_fifo.readPos,
                                             m_fifo.writePos, m_fifo.count);
            }
            UpdateFIFODREQ();
        }
        break;
    case 0x04: m_regs.CDIRQL.u16 = value & 0x3; break;
    case 0x06:
        if constexpr (poke) {
            m_regs.CDIRQU.u16 = value & 0x70;
        } else {
            m_regs.CDIRQU.u16 &= value;
        }
        break;
    case 0x08: m_regs.CDMSKL.u16 = value & 0x3; break;
    case 0x0A: m_regs.CDMSKU.u16 = value & 0x70; break;
    case 0x0C: m_regs.REG0C.u16 = value & 0x3; break;
    case 0x0E: m_regs.REG0E = value; break;
    case 0x10: m_regs.RR[0] = value; break;
    case 0x12: m_regs.RR[1] = value; break;
    case 0x14: m_regs.RR[2] = value; break;
    case 0x16:
        m_regs.RR[3] = value;
        if constexpr (!poke) {
            devlog::trace<grp::ygr_cr>("CDB  RR writes: {:04X} {:04X} {:04X} {:04X}", m_regs.RR[0], m_regs.RR[1],
                                       m_regs.RR[2], m_regs.RR[3]);
            TraceReceiveCDBlockResponse(m_tracer, m_regs.RR[0], m_regs.RR[1], m_regs.RR[2], m_regs.RR[3]);
        }
        break;
    case 0x18: m_regs.REG18.u16 = value & 0x3F; break;
    case 0x1A: m_regs.REG1A.u16 = value & 0xD7; break;
    case 0x1C: m_regs.REG1C.u16 = value & 0xFF; break;
    case 0x1E:
        if constexpr (poke) {
            m_regs.HIRQ = value & 0x3FFF;
        } else {
            m_regs.HIRQ |= value & 0x3FFF;
            UpdateInterrupts();
        }
        break;
    default:
        if constexpr (!poke) {
            devlog::trace<grp::ygr_regs>("Unhandled 16-bit CD Block YGR write to {:02X} = {:04X}", address, value);
        }
        break;
    }
}

FORCE_INLINE uint8 YGR::CDBPeekByte(uint32 address) const {
    if (((address >> 20) & 0xF) == 0x1) {
        // TODO: read from Video CD Card registers instead
        return 0;
    }

    address &= 0xFFFF;
    switch (address) {
    case 0x00: return m_fifo.Read<true>() >> 8u;
    case 0x01: return m_fifo.Read<true>();
    case 0x02: return m_regs.TRCTL.u16 >> 8u;
    case 0x03: return m_regs.TRCTL.u16;
    case 0x04: return m_regs.CDIRQL.u16 >> 8u;
    case 0x05: return m_regs.CDIRQL.u16;
    case 0x06: return m_regs.CDIRQU.u16 >> 8u;
    case 0x07: return m_regs.CDIRQU.u16;
    case 0x08: return m_regs.CDMSKL.u16 >> 8u;
    case 0x09: return m_regs.CDMSKL.u16;
    case 0x0A: return m_regs.CDMSKU.u16 >> 8u;
    case 0x0B: return m_regs.CDMSKU.u16;
    case 0x0C: return m_regs.REG0C.u16 >> 8u;
    case 0x0D: return m_regs.REG0C.u16;
    case 0x0E: return m_regs.REG0E >> 8u;
    case 0x0F: return m_regs.REG0E;
    case 0x10: return m_regs.CR[0] >> 8u;
    case 0x11: return m_regs.CR[0];
    case 0x12: return m_regs.CR[1] >> 8u;
    case 0x13: return m_regs.CR[1];
    case 0x14: return m_regs.CR[2] >> 8u;
    case 0x15: return m_regs.CR[2];
    case 0x16: return m_regs.CR[3] >> 8u;
    case 0x17: return m_regs.CR[3];
    case 0x18: return m_regs.REG18.u16 >> 8u;
    case 0x19: return m_regs.REG18.u16;
    case 0x1A: return m_regs.REG1A.u16 >> 8u;
    case 0x1B: return m_regs.REG1A.u16;
    case 0x1C: return m_regs.REG1C.u16 >> 8u;
    case 0x1D: return m_regs.REG1C.u16;
    case 0x1E: return m_regs.HIRQ >> 8u;
    case 0x1F: return m_regs.HIRQ;
    default: return 0u;
    }
}

FORCE_INLINE void YGR::CDBPokeByte(uint32 address, uint8 value) {
    if (((address >> 20) & 0xF) == 0x1) {
        // TODO: write to Video CD Card registers instead
        return;
    }

    address &= 0xFFFF;
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
    case 0x02: break;
    case 0x03: m_regs.TRCTL.u16 = value & 0xF; break;
    case 0x04: break;
    case 0x05: m_regs.CDIRQL.u16 = value & 0x3; break;
    case 0x06: break;
    case 0x07: m_regs.CDIRQU.u16 = value & 0x70; break;
    case 0x08: break;
    case 0x09: m_regs.CDMSKL.u16 = value & 0x3; break;
    case 0x0A: break;
    case 0x0B: m_regs.CDMSKU.u16 = value & 0x70; break;
    case 0x0C: break;
    case 0x0D: m_regs.REG0C.u16 = value & 0x3; break;
    case 0x0E: bit::deposit_into<8, 15>(m_regs.REG0E, value); break;
    case 0x0F: bit::deposit_into<0, 7>(m_regs.REG0E, value); break;
    case 0x10: bit::deposit_into<8, 15>(m_regs.RR[0], value); break;
    case 0x11: bit::deposit_into<0, 7>(m_regs.RR[0], value); break;
    case 0x12: bit::deposit_into<8, 15>(m_regs.RR[1], value); break;
    case 0x13: bit::deposit_into<0, 7>(m_regs.RR[1], value); break;
    case 0x14: bit::deposit_into<8, 15>(m_regs.RR[2], value); break;
    case 0x15: bit::deposit_into<0, 7>(m_regs.RR[2], value); break;
    case 0x16: bit::deposit_into<8, 15>(m_regs.RR[3], value); break;
    case 0x17: bit::deposit_into<0, 7>(m_regs.RR[3], value); break;
    case 0x18: break;
    case 0x19: m_regs.REG18.u16 = value & 0x3F; break;
    case 0x1A: break;
    case 0x1B: m_regs.REG1A.u16 = value & 0xD7; break;
    case 0x1C: break;
    case 0x1D: m_regs.REG1C.u16 = value & 0xFF; break;
    case 0x1E: bit::deposit_into<8, 13>(m_regs.HIRQ, value); break;
    case 0x1F: bit::deposit_into<0, 7>(m_regs.HIRQ, value); break;
    }
}

// -----------------------------------------------------------------------------
// Host bus

template <bool peek>
FORCE_INLINE uint16 YGR::HostReadWord(uint32 address) const {
    address &= 0x3C;
    switch (address) {
    case 0x00:
        if constexpr (peek) {
            return m_fifo.Read<true>();
        }

        if (m_regs.TRCTL.DIR) {
            return 0u;
        }

        if (m_fifo.IsEmpty()) {
            // Force transfer if possible
            m_cbStepDMAC1(sizeof(uint16));
            if (m_fifo.IsEmpty() && m_regs.TRCTL.TE) {
                devlog::trace<grp::ygr_fifo>("FIFO still empty; transfer might break!");
                YMIR_DEV_CHECK();
            }
        }

        {
            const uint16 value = m_fifo.Read<peek>();
            devlog::trace<grp::ygr_fifo>("Host FIFO read  <- rd={:X} wr={:X} cnt={:X}  {:04X}", m_fifo.readPos,
                                         m_fifo.writePos, m_fifo.count, value);
            UpdateFIFODREQ();
            return value;
        }
    case 0x08: return m_regs.HIRQ;
    case 0x0C: return m_regs.HIRQMASK;
    case 0x18: return m_regs.RR[0];
    case 0x1C: return m_regs.RR[1];
    case 0x20: return m_regs.RR[2];
    case 0x24:
        if constexpr (!peek) {
            m_regs.CDIRQL.RESP = 1;
        }
        return m_regs.RR[3];
    case 0x28: return 0u; // TODO: read MPEGRGB
    default:
        if constexpr (!peek) {
            devlog::trace<grp::ygr_regs>("Unhandled 16-bit host YGR read from {:02X}", address);
        }
        return 0u;
    }
}

template <bool doubleFIFOPull>
uint32 YGR::HostReadLong(uint32 address) const {
    address &= 0x3C;
    switch (address) {
    case 0x00:
        if (m_regs.TRCTL.DIR) {
            return 0u;
        }

        if (m_fifo.IsEmpty()) {
            // Force transfer if possible
            m_cbStepDMAC1(sizeof(uint16));
            if (m_fifo.IsEmpty() && m_regs.TRCTL.TE) {
                devlog::trace<grp::ygr_fifo>("FIFO still empty; transfer might break!");
                YMIR_DEV_CHECK();
            }
        }

        if constexpr (doubleFIFOPull) {
            uint32 value = m_fifo.Read<false>() << 16u;

            if (m_fifo.IsEmpty()) {
                // Force transfer if possible
                m_cbStepDMAC1(sizeof(uint16));
                if (m_fifo.IsEmpty() && m_regs.TRCTL.TE) {
                    devlog::trace<grp::ygr_fifo>("FIFO still empty; transfer might break!");
                    YMIR_DEV_CHECK();
                }
            }

            value |= m_fifo.Read<false>();
            devlog::trace<grp::ygr_fifo>("Host FIFO read  <- rd={:X} wr={:X} cnt={:X}  {:08X}", m_fifo.readPos,
                                         m_fifo.writePos, m_fifo.count, value);
            UpdateFIFODREQ();
            return value;
        } else {
            const uint16 value = m_fifo.Read<false>();
            devlog::trace<grp::ygr_fifo>("Host FIFO read  <- rd={:X} wr={:X} cnt={:X}  {:04X}", m_fifo.readPos,
                                         m_fifo.writePos, m_fifo.count, value);
            UpdateFIFODREQ();
            return value;
        }

    case 0x08: return m_regs.HIRQ;
    case 0x0C: return m_regs.HIRQMASK;
    case 0x18: return m_regs.RR[0];
    case 0x1C: return m_regs.RR[1];
    case 0x20: return m_regs.RR[2];
    case 0x24: m_regs.CDIRQL.RESP = 1; return m_regs.RR[3];
    case 0x28: return 0u; // TODO: read MPEGRGB
    default: devlog::trace<grp::ygr_regs>("Unhandled 32-bit host YGR read from {:02X}", address); return 0u;
    }
}

template <bool poke>
FORCE_INLINE void YGR::HostWriteWord(uint32 address, uint16 value) {
    address &= 0x3C;
    switch (address) {
    case 0x00:
        if constexpr (poke) {
            m_fifo.Write<true>(value);
        } else if (m_regs.TRCTL.DIR) {
            if (m_fifo.IsFull()) {
                // Force transfer if possible
                m_cbStepDMAC1(sizeof(uint16));
                if (m_fifo.IsFull() && m_regs.TRCTL.TE) {
                    devlog::trace<grp::ygr_fifo>("FIFO still full; transfer will break!");
                    YMIR_DEV_CHECK();
                }
            }
            m_fifo.Write<poke>(value);
            devlog::trace<grp::ygr_fifo>("Host FIFO write -> rd={:X} wr={:X} cnt={:X}  {:04X}", m_fifo.readPos,
                                         m_fifo.writePos, m_fifo.count, value);
            UpdateFIFODREQ();
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
            m_regs.CDIRQL.CMD = 1;
            m_cbAssertIRQ6();
            devlog::trace<grp::ygr_cr>("Host CR writes: {:04X} {:04X} {:04X} {:04X}", m_regs.CR[0], m_regs.CR[1],
                                       m_regs.CR[2], m_regs.CR[3]);
            TraceReceiveHostCommand(m_tracer, m_regs.CR[0], m_regs.CR[1], m_regs.CR[2], m_regs.CR[3]);
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

// -----------------------------------------------------------------------------
// Save states

void YGR::SaveState(state::YGRState &state) const {
    state.fifo.data = m_fifo.data;
    state.fifo.readPos = m_fifo.readPos;
    state.fifo.writePos = m_fifo.writePos;
    state.fifo.count = m_fifo.count;

    state.regs.TRCTL = m_regs.TRCTL.u16;
    state.regs.CDIRQL = m_regs.CDIRQL.u16;
    state.regs.CDIRQU = m_regs.CDIRQU.u16;
    state.regs.CDMSKL = m_regs.CDMSKL.u16;
    state.regs.CDMSKU = m_regs.CDMSKU.u16;
    state.regs.REG0C = m_regs.REG0C.u16;
    state.regs.REG0E = m_regs.REG0E;
    state.regs.CR = m_regs.CR;
    state.regs.RR = m_regs.RR;
    state.regs.REG18 = m_regs.REG18.u16;
    state.regs.REG1A = m_regs.REG1A.u16;
    state.regs.REG1C = m_regs.REG1C.u16;
    state.regs.HIRQ = m_regs.HIRQ;
    state.regs.HIRQMASK = m_regs.HIRQMASK;
}

bool YGR::ValidateState(const state::YGRState &state) const {
    if (state.fifo.readPos >= m_fifo.data.size()) {
        return false;
    }
    if (state.fifo.writePos >= m_fifo.data.size()) {
        return false;
    }
    if (state.fifo.count > m_fifo.data.size()) {
        return false;
    }
    return true;
}

void YGR::LoadState(const state::YGRState &state) {
    m_fifo.data = state.fifo.data;
    m_fifo.readPos = state.fifo.readPos;
    m_fifo.writePos = state.fifo.writePos;
    m_fifo.count = state.fifo.count;

    m_regs.TRCTL.u16 = state.regs.TRCTL & 0xF;
    m_regs.CDIRQL.u16 = state.regs.CDIRQL & 0x3;
    m_regs.CDIRQU.u16 = state.regs.CDIRQU;
    m_regs.CDMSKL.u16 = state.regs.CDMSKL & 0x3;
    m_regs.CDMSKU.u16 = state.regs.CDMSKU & 0x70;
    m_regs.REG0C.u16 = state.regs.REG0C & 0x3;
    m_regs.REG0E = state.regs.REG0E;
    m_regs.CR = state.regs.CR;
    m_regs.RR = state.regs.RR;
    m_regs.REG18.u16 = state.regs.REG18 & 0x3F;
    m_regs.REG1A.u16 = state.regs.REG1A & 0xD7;
    m_regs.REG1C.u16 = state.regs.REG1C & 0xFF;
    m_regs.HIRQ = state.regs.HIRQ & 0x3FFF;
    m_regs.HIRQMASK = state.regs.HIRQMASK & 0x3FFF;
}

// -----------------------------------------------------------------------------
// Helpers

void YGR::UpdateInterrupts() {
    devlog::trace<grp::base>("HIRQ = {:04X}  mask = {:04X}  active = {:04X}", m_regs.HIRQ, m_regs.HIRQMASK,
                             m_regs.HIRQ & m_regs.HIRQMASK);
    if (m_regs.HIRQ & m_regs.HIRQMASK) {
        m_cbTriggerExternalInterrupt0();
    }
}

void YGR::UpdateFIFODREQ() const {
    // DREQ1# signals data transfers between the host and the CD block SH-1.
    // DREQ1# is asserted when transfers are enabled and not blocked from the SH-1 side:
    // - When doing a CDB->host transfer (SH-1 is writing to the FIFO), the FIFO should not be full
    // - When doing a host->CDB transfer (SH-1 is reading from the FIFO), the FIFO should not be empty
    // The signal is inverted, so `true` means the transfer should be blocked.
    m_cbSetDREQ1n(!m_regs.TRCTL.TE || (m_regs.TRCTL.DIR ? m_fifo.IsEmpty() : m_fifo.IsFull()));
}

void YGR::SectorTransferDone() {
    m_regs.CDIRQU.DET = 1;
    if (m_regs.CDIRQU.u16 & m_regs.CDMSKU.u16) {
        m_cbAssertIRQ7();
    }
}

} // namespace ymir::cdblock
