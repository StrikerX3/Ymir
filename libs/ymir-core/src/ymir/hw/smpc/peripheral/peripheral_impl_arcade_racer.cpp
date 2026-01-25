#include <ymir/hw/smpc/peripheral/peripheral_impl_arcade_racer.hpp>

#include <ymir/util/bit_ops.hpp>

namespace ymir::peripheral {

ArcadeRacerPeripheral::ArcadeRacerPeripheral(CBPeripheralReport callback)
    : BasePeripheral(PeripheralType::ArcadeRacer, 0x1, callback) {}

void ArcadeRacerPeripheral::UpdateInputs() {
    PeripheralReport report{.type = PeripheralType::ArcadeRacer,
                            .report = {.arcadeRacer = {.buttons = Button::Default, .wheel = 0x7F}}};
    m_cbPeripheralReport(report);

    m_report = report.report.arcadeRacer;
    m_report.buttons &= Button::All;
    m_report.buttons |= Button::Left | Button::Right | Button::R | Button::L;
    m_report.buttons |= static_cast<Button>(0b111);

    // Convert wheel position to left/right D-Pad button press
    static constexpr uint8 kAnalogToDigitalLeftThreshold = 111;
    static constexpr uint8 kAnalogToDigitalRightThreshold = 143;

    if (m_report.wheel <= kAnalogToDigitalLeftThreshold) {
        m_report.buttons &= ~Button::Left;
    } else if (m_report.wheel >= kAnalogToDigitalRightThreshold) {
        m_report.buttons &= ~Button::Right;
    }
}

uint8 ArcadeRacerPeripheral::GetReportLength() const {
    return 3;
}

void ArcadeRacerPeripheral::Read(std::span<uint8> out) {
    const auto btnValue = static_cast<uint16>(m_report.buttons);
    // [0] 7-0 = right, left, down, up, start, A, C, B
    // [1] 7-3 = 1, X, Y, Z, 1; 2-0 = fixed 0b100
    // [2] 7-0 = wheel value
    assert(out.size() == 3);
    out[0] = bit::extract<8, 15>(btnValue);
    out[1] = (bit::extract<3, 7>(btnValue) << 3) | 0b100;
    out[2] = m_report.wheel;
}

uint8 ArcadeRacerPeripheral::WritePDR(uint8 ddr, uint8 value, bool exle) {
    const auto btnValue = static_cast<uint16>(m_report.buttons);

    switch (ddr & 0x7F) {
    case 0x40: // TH control mode
        if (value & 0x40) {
            return 0x70 | 0b0001;
        } else {
            return 0x30 | 0b0001;
        }
        break;
    case 0x60: // TH/TR control mode
        // Saturn peripheral ID acquisition sequence
        // TODO: check correctness
        const bool th = bit::test<6>(value);
        const bool tr = bit::test<5>(value);
        if (th) {
            m_reportPos = 0;
            m_tl = false;
        } else if (m_reportPos == 0 || tr != m_tl) {
            m_tl = tr;
            const uint8 pos = m_reportPos;
            m_reportPos = (m_reportPos + 1) % 10;
            switch (pos) {
            case 0: return (m_tl << 4) | 0b0001;
            case 1: return (m_tl << 4) | 0b0101;
            case 2: return (m_tl << 4) | bit::extract<12, 15>(btnValue);
            case 3: return (m_tl << 4) | bit::extract<8, 11>(btnValue);
            case 4: return (m_tl << 4) | bit::extract<4, 7>(btnValue);
            case 5: return (m_tl << 4) | (bit::extract<3>(btnValue) << 3) | 0b100;
            case 6: return (m_tl << 4) | bit::extract<4, 7>(m_report.wheel);
            case 7: return (m_tl << 4) | bit::extract<0, 3>(m_report.wheel);
            case 8: return (m_tl << 4) | 0b0000;
            case 9: return (m_tl << 4) | 0b0001;
            }
        }
    }

    return 0xFF;
}

} // namespace ymir::peripheral
