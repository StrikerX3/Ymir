#include <ymir/hw/smpc/peripheral/peripheral_impl_mission_stick.hpp>

#include <ymir/util/bit_ops.hpp>

namespace ymir::peripheral {

MissionStick::MissionStick(CBPeripheralReport callback)
    : BasePeripheral(PeripheralType::MissionStick, 0x1, callback) {

    SetSixAxisMode(false);
}

void MissionStick::UpdateInputs() {
    PeripheralReport report{.type = PeripheralType::MissionStick,
                            .report = {.missionStick = {.buttons = Button::Default,
                                                        .sixAxis = m_sixAxisMode,
                                                        .x1 = 0x7F,
                                                        .y1 = 0x7F,
                                                        .z1 = 0x00,
                                                        .x2 = 0x7F,
                                                        .y2 = 0x7F,
                                                        .z2 = 0x00}}};
    m_cbPeripheralReport(report);

    m_report = report.report.missionStick;
    m_report.buttons &= Button::All;
    m_report.buttons |= static_cast<Button>(0b111);

    if (m_report.sixAxis != m_sixAxisMode) {
        SetSixAxisMode(m_report.sixAxis);
    }

    auto mapAnalogToLeftUpButton = [&](uint8 analogValue, Button button) {
        static constexpr uint8 kAnalogToDigitalOnThreshold = 86;
        static constexpr uint8 kAnalogToDigitalOffThreshold = 107;

        if (analogValue >= kAnalogToDigitalOffThreshold) {
            m_report.buttons |= button;
        } else if (analogValue <= kAnalogToDigitalOnThreshold) {
            m_report.buttons &= ~button;
        }
    };
    auto mapAnalogToRightDownButton = [&](uint8 analogValue, Button button) {
        static constexpr uint8 kAnalogToDigitalOffThreshold = 147;
        static constexpr uint8 kAnalogToDigitalOnThreshold = 170;

        if (analogValue <= kAnalogToDigitalOffThreshold) {
            m_report.buttons |= button;
        } else if (analogValue >= kAnalogToDigitalOnThreshold) {
            m_report.buttons &= ~button;
        }
    };
    mapAnalogToLeftUpButton(m_report.x1, Button::Left);
    mapAnalogToLeftUpButton(m_report.y1, Button::Up);
    mapAnalogToRightDownButton(m_report.x1, Button::Right);
    mapAnalogToRightDownButton(m_report.y1, Button::Down);
}

uint8 MissionStick::GetReportLength() const {
    return m_sixAxisMode ? 9 : 5;
}

void MissionStick::Read(std::span<uint8> out) {
    const auto btnValue = static_cast<uint16>(m_report.buttons);
    // [0] 7-0 = right, left, down, up, start, A, C, B
    // [1] 7-3 = R, X, Y, Z, L; 2-0 = fixed 0b100
    // [2] AX7-0
    // [3] AY7-0
    // [4] AZ7-0
    // ---- three-axis report ends here; six-axis report continues below ----
    // [5] 7-4 = undefined; 3-0 = fixed 0b0000
    // [6] BX7-0
    // [7] BY7-0
    // [8] BZ7-0
    assert(out.size() == GetReportLength());
    out[0] = bit::extract<8, 15>(btnValue);
    out[1] = (bit::extract<3, 7>(btnValue) << 3) | 0b100;
    out[2] = m_report.x1;
    out[3] = m_report.y1;
    out[4] = m_report.z1;
    if (m_sixAxisMode) {
        out[5] = 0b0000'0000;
        out[6] = m_report.x2;
        out[7] = m_report.y2;
        out[8] = m_report.z2;
    }
}

uint8 MissionStick::WritePDR(uint8 ddr, uint8 value, bool exle) {
    const auto btnValue = static_cast<uint16>(m_report.buttons);

    switch (ddr & 0x7F) {
    case 0x40: // TH control mode
        // Mega Drive peripheral ID acquisition sequence
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
            const uint8 reportLength = m_sixAxisMode ? 22 : 14;
            m_reportPos = (m_reportPos + 1) % reportLength;
            if (!m_sixAxisMode) {
                // End report early if using three-axis mode
                switch (pos) {
                case 12: return (m_tl << 4) | 0b0000;
                case 13: return (m_tl << 4) | 0b0001;
                }
            }
            switch (pos) {
            case 0: return (m_tl << 4) | 0b0001;
            case 1: return (m_tl << 4) | 0b0101;
            case 2: return (m_tl << 4) | bit::extract<12, 15>(btnValue);
            case 3: return (m_tl << 4) | bit::extract<8, 11>(btnValue);
            case 4: return (m_tl << 4) | bit::extract<4, 7>(btnValue);
            case 5: return (m_tl << 4) | (bit::extract<3>(btnValue) << 3) | 0b100;
            case 6: return (m_tl << 4) | bit::extract<4, 7>(m_report.x1);
            case 7: return (m_tl << 4) | bit::extract<0, 3>(m_report.x1);
            case 8: return (m_tl << 4) | bit::extract<4, 7>(m_report.y1);
            case 9: return (m_tl << 4) | bit::extract<0, 3>(m_report.y1);
            case 10: return (m_tl << 4) | bit::extract<4, 7>(m_report.z1);
            case 11: return (m_tl << 4) | bit::extract<0, 3>(m_report.z1);
            case 12: return (m_tl << 4) | 0b0000;
            case 13: return (m_tl << 4) | 0b0000;
            case 14: return (m_tl << 4) | bit::extract<4, 7>(m_report.x2);
            case 15: return (m_tl << 4) | bit::extract<0, 3>(m_report.x2);
            case 16: return (m_tl << 4) | bit::extract<4, 7>(m_report.y2);
            case 17: return (m_tl << 4) | bit::extract<0, 3>(m_report.y2);
            case 18: return (m_tl << 4) | bit::extract<4, 7>(m_report.z2);
            case 19: return (m_tl << 4) | bit::extract<0, 3>(m_report.z2);
            case 20: return (m_tl << 4) | 0b0000;
            case 21: return (m_tl << 4) | 0b0001;
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

void MissionStick::SetSixAxisMode(bool mode) {
    m_sixAxisMode = mode;
}

} // namespace ymir::peripheral
