#include <ymir/hw/smpc/peripheral/peripheral_impl_control_pad.hpp>

#include <ymir/util/bit_ops.hpp>

namespace ymir::peripheral {

ControlPad::ControlPad(CBPeripheralReport callback)
    : BasePeripheral(PeripheralType::ControlPad, 0x0, callback) {}

void ControlPad::UpdateInputs() {
    PeripheralReport report{.type = PeripheralType::ControlPad, .report = {.controlPad = {.buttons = Button::Default}}};
    m_cbPeripheralReport(report);
    m_report = report.report.controlPad;
    m_report.buttons &= Button::All;
    m_report.buttons |= static_cast<Button>(0b111);
}

uint8 ControlPad::GetReportLength() const {
    return 2;
}

void ControlPad::Read(std::span<uint8> out) {
    assert(out.size() == 2);

    // [0] 7-0 = right, left, down, up, start, A, C, B
    // [1] 7-3 = R, X, Y, Z, L; 2-0 = fixed 0b111
    const uint16 btnValue = static_cast<uint16>(m_report.buttons);
    out[0] = bit::extract<8, 15>(btnValue);
    out[1] = bit::extract<0, 7>(btnValue);
}

uint8 ControlPad::WritePDR(uint8 ddr, uint8 value) {
    const uint16 btnValue = static_cast<uint16>(m_report.buttons);

    switch (ddr & 0x7F) {
    case 0x40: // TH control mode
        if (value & 0x40) {
            return 0x70 | 0b0100;
        } else {
            return 0x30 | 0b0101;
        }
        break;
    case 0x60: // TH/TR control mode
        switch (value & 0x60) {
        case 0x60: // 1st data: L 1 0 0
            return 0x70 | (bit::extract<3>(btnValue) << 3) | 0b100;
        case 0x20: // 2nd data: right left down up
            return 0x30 | bit::extract<12, 15>(btnValue);
        case 0x40: // 3rd data: start A C B
            return 0x50 | bit::extract<8, 11>(btnValue);
        case 0x00: // 4th data: R X Y Z
            return 0x10 | bit::extract<4, 7>(btnValue);
        }
    }

    return 0xFF;
}

} // namespace ymir::peripheral
