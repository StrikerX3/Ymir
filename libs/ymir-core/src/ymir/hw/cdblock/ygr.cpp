#include <ymir/hw/cdblock/ygr.hpp>

#include "cdblock_devlog.hpp"

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
}

uint16 YGR::CDBReadWord(uint32 address) const {
    if (((address >> 20) & 0xF) == 0x1) {
        // TODO: read from Video CD Card registers instead
        return 0;
    }

    address &= 0xFFFF;
    switch (address) {
    case 0x00: return m_fifo.Read();
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
    default: devlog::trace<grp::ygr_regs>("Unhandled 16-bit CD Block YGR read from {:02X}\n", address); return 0u;
    }
}

void YGR::CDBWriteWord(uint32 address, uint16 value) {
    if (((address >> 20) & 0xF) == 0x1) {
        // TODO: write to Video CD Card registers instead
        return;
    }

    address &= 0xFFFF;
    switch (address) {
    case 0x00: m_fifo.Write(value); break;
    case 0x02:
        m_regs.TRCTL.u16 = value & 0xF;
        if (m_regs.TRCTL.RES) {
            m_fifo.Clear();
        }
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
    case 0x16: m_regs.RR[3] = value; break;
    case 0x18: m_regs.REG18.u16 = value & 0x3F; break;
    case 0x1A: m_regs.REG1A.u16 = value & 0xD7; break;
    case 0x1C: m_regs.REG1C.u16 = value & 0xFF; break;
    case 0x1E: m_regs.HIRQ |= value; break;
    default:
        devlog::trace<grp::ygr_regs>("Unhandled 16-bit CD Block YGR write from {:02X} = {:04X}\n", address, value);
        break;
    }
}

uint16 YGR::HostReadWord(uint32 address) const {
    if ((address & 0x7FFF) >= 0x1000) {
        // Out of range
        return 0u;
    }

    address &= 0x3C;
    switch (address) {
    case 0x00: return m_regs.TRCTL.DIR ? 0u : m_fifo.Read();
    case 0x08: return m_regs.HIRQ;
    case 0x0C: return m_regs.HIRQMASK;
    case 0x18: return m_regs.RR[0];
    case 0x1C: return m_regs.RR[1];
    case 0x20: return m_regs.RR[2];
    case 0x24: return m_regs.RR[3];
    case 0x28: return 0u; // TODO: read MPEGRGB
    default: devlog::trace<grp::ygr_regs>("Unhandled 16-bit host YGR read from {:02X}\n", address); return 0u;
    }
}

void YGR::HostWriteWord(uint32 address, uint16 value) {
    if ((address & 0x7FFF) >= 0x1000) {
        // Out of range
        return;
    }

    address &= 0x3C;
    switch (address) {
    case 0x00:
        if (m_regs.TRCTL.DIR) {
            m_fifo.Write(value);
        }
        break;
    case 0x08: m_regs.HIRQ &= value; break;
    case 0x0C: m_regs.HIRQMASK = value; break;
    case 0x18: m_regs.CR[0] = value; break;
    case 0x1C: m_regs.CR[1] = value; break;
    case 0x20: m_regs.CR[2] = value; break;
    case 0x24:
        m_regs.CR[3] = value;
        m_cbAssertIRQ6();
        break;
    case 0x28: /* TODO: write MPEGRGB */ break;
    default:
        devlog::trace<grp::ygr_regs>("Unhandled 16-bit host YGR write from {:02X} = {:04X}\n", address, value);
        break;
    }
}

} // namespace ymir::cdblock
