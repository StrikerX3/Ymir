#include <ymir/hw/smpc/peripheral/peripheral_impl_virtua_gun.hpp>

#include <ymir/util/bit_ops.hpp>

namespace ymir::peripheral {

VirtuaGun::VirtuaGun(CBPeripheralReport callback)
    : BasePeripheral(PeripheralType::VirtuaGun, 0xE, callback) {}

void VirtuaGun::UpdateInputs() {
    PeripheralReport report{
        .type = PeripheralType::VirtuaGun,
        .report = {.virtuaGun = {.start = false, .trigger = false, .reload = false, .x = 0xFFFF, .y = 0xFFFF}}};
    m_cbPeripheralReport(report);
    m_report = report.report.virtuaGun;
}

uint8 VirtuaGun::GetReportLength() const {
    return 0;
}

void VirtuaGun::Read(std::span<uint8> out) {
    assert(out.size() == 0);

    // Virtua Gun cannot be used with INTBACK
}

uint8 VirtuaGun::WritePDR(uint8 ddr, uint8 value, bool exle) {
    switch (ddr & 0x7F) {
    case 0x40: // TH control mode
        if (value & 0x40) {
            return 0x70 | 0b1100;
        } else {
            return 0x30 | 0b1100;
        }
        break;
    case 0x00: // read mode
    {
        // TODO: external latch should trigger right about when the light beam hits the light gun sensor
        if (exle) {
            m_reload = m_report.reload;
        }
        const bool extLatch = m_report.x != 0xFFFF && m_report.y != 0xFFFF;
        const bool trigger = m_report.trigger || m_reload;
        return (!extLatch << 6) | (!m_report.start << 5) | (!trigger << 4) | 0b1100;
    }
    }

    return 0xFF;
}

bool VirtuaGun::GetExternalLatchCoordinates(uint16 &x, uint16 &y) {
    x = m_reload ? 0xFFFF : m_report.x;
    y = m_reload ? 0xFFFF : m_report.y;
    return true;
}

} // namespace ymir::peripheral
