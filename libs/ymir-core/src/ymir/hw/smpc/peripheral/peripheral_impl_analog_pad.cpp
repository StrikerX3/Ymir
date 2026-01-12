#include <ymir/hw/smpc/peripheral/peripheral_impl_analog_pad.hpp>

#include <ymir/util/bit_ops.hpp>

namespace ymir::peripheral {

AnalogPad::AnalogPad(CBPeripheralReport callback)
    : BasePeripheral(PeripheralType::AnalogPad, 0x1, callback) {

    SetAnalogMode(true);
}

void AnalogPad::UpdateInputs() {
    PeripheralReport report{
        .type = PeripheralType::AnalogPad,
        .report = {
            .analogPad = {
                .buttons = Button::Default, .analog = m_analogMode, .x = 0x80, .y = 0x80, .l = 0x00, .r = 0x00}}};
    m_cbPeripheralReport(report);

    m_report = report.report.analogPad;
    m_report.buttons &= Button::All;
    m_report.buttons |= static_cast<Button>(0b111);

    if (m_report.analog != m_analogMode) {
        SetAnalogMode(m_report.analog);
    }

    auto mapAnalogToButton = [&](uint8 analogValue, Button button) {
        static constexpr uint8 kAnalogToDigitalOffThreshold = 85;
        static constexpr uint8 kAnalogToDigitalOnThreshold = 145;

        if (analogValue <= kAnalogToDigitalOffThreshold) {
            m_report.buttons |= button;
        } else if (analogValue >= kAnalogToDigitalOnThreshold) {
            m_report.buttons &= ~button;
        }
    };
    mapAnalogToButton(m_report.l, Button::L);
    mapAnalogToButton(m_report.r, Button::R);
}

uint8 AnalogPad::GetReportLength() const {
    return m_analogMode ? 6 : 2;
}

void AnalogPad::Read(std::span<uint8> out) {
    const auto btnValue = static_cast<uint16>(m_report.buttons);
    if (m_analogMode) {
        // [0] 7-0 = right, left, down, up, start, A, C, B
        // [1] 7-3 = R, X, Y, Z, L; 2-0 = fixed 0b100
        // [2] AX7-0
        // [3] AY7-0
        // [4] AR7-0
        // [5] AL7-0
        assert(out.size() == 6);
        out[0] = bit::extract<8, 15>(btnValue);
        out[1] = (bit::extract<3, 7>(btnValue) << 3) | 0b100;
        out[2] = m_report.x;
        out[3] = m_report.y;
        out[4] = m_report.r;
        out[5] = m_report.l;
    } else {
        // [0] 7-0 = right, left, down, up, start, A, C, B
        // [1] 7-3 = R, X, Y, Z, L; 2-0 = fixed 0b100
        assert(out.size() == 2);
        out[0] = bit::extract<8, 15>(btnValue);
        out[1] = (bit::extract<3, 7>(btnValue) << 3) | 0b100;
    }
}

uint8 AnalogPad::WritePDR(uint8 ddr, uint8 value, bool exle) {
    const auto btnValue = static_cast<uint16>(m_report.buttons);

    switch (ddr & 0x7F) {
    case 0x40: // TH control mode
        if (m_analogMode) {
            // Mega Drive peripheral ID acquisition sequence
            // TODO: check correctness
            if (value & 0x40) {
                return 0x70 | 0b0001;
            } else {
                return 0x30 | 0b0001;
            }
        } else {
            if (value & 0x40) {
                return 0x70 | 0b0100;
            } else {
                return 0x30 | 0b0101;
            }
        }
        break;
    case 0x60: // TH/TR control mode
        if (m_analogMode) {
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
                m_reportPos = (m_reportPos + 1) & 15;
                switch (pos) {
                case 0: return (m_tl << 4) | 0b0001;
                case 1: return (m_tl << 4) | 0b0101;
                case 2: return (m_tl << 4) | bit::extract<12, 15>(btnValue);
                case 3: return (m_tl << 4) | bit::extract<8, 11>(btnValue);
                case 4: return (m_tl << 4) | bit::extract<4, 7>(btnValue);
                case 5: return (m_tl << 4) | (bit::extract<3>(btnValue) << 3) | 0b100;
                case 6: return (m_tl << 4) | bit::extract<4, 7>(m_report.x);
                case 7: return (m_tl << 4) | bit::extract<0, 3>(m_report.x);
                case 8: return (m_tl << 4) | bit::extract<4, 7>(m_report.y);
                case 9: return (m_tl << 4) | bit::extract<0, 3>(m_report.y);
                case 10: return (m_tl << 4) | bit::extract<4, 7>(m_report.l);
                case 11: return (m_tl << 4) | bit::extract<0, 3>(m_report.l);
                case 12: return (m_tl << 4) | bit::extract<4, 7>(m_report.r);
                case 13: return (m_tl << 4) | bit::extract<0, 3>(m_report.r);
                case 14: return (m_tl << 4) | 0b0000;
                case 15: return (m_tl << 4) | 0b0001;
                }
            }
        } else {
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
    }

    return 0xFF;
}

void AnalogPad::SetAnalogMode(bool mode) {
    m_analogMode = mode;
    SetTypeCode(mode ? 0x1 : 0x0);
}

} // namespace ymir::peripheral
