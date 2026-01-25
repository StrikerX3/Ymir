#pragma once

#include "peripheral_base.hpp"

namespace ymir::peripheral {

/// @brief Implements the Mission Stick (ID 0x1/5 bytes in three-axis mode, 0x1/9 bytes in six-axis mode) with:
/// - 8 digital buttons: ABC XYZ LR
/// - Start button
/// - Analog stick with three digital triggers mapped to ABC and an analog throttle control
/// - In six-axis mode a second joystick is added to the set with digital triggers mapped to XYZ
///
/// The analog stick value range is as follows:
///
///      (0,0)     UP    (255,0)
///      +---------------------+
///      |     86  UP ON       |
///      |    107  UP OFF      |
///      |                     |
///      | 86 107      147 170 |
/// LEFT | LEFT     +    RIGHT | RIGHT
///      | ON OFF      OFF ON  |
///      |                     |
///      |   147  DOWN OFF     |
///      |   170  DOWN ON      |
///      +---------------------+
///      (0,255)  DOWN (255,255)
///
/// Center is at (127,127).
///
/// The throttle ranges from 0 (down) to 255 (up).
///
/// The main analog stick is translated into Up/Down/Left/Right signals on the following thresholds:
/// - Left/up state is set to ON when the analog value is 86 or lower
/// - Left/up state is set to OFF when the analog value is 107 or higher
/// - Right/down state is set to ON when the analog value is 170 or higher
/// - Right/down state is set to OFF when the analog value is 147 or lower
class MissionStick final : public BasePeripheral {
public:
    explicit MissionStick(CBPeripheralReport callback);

    void UpdateInputs() override;

    [[nodiscard]] uint8 GetReportLength() const override;

    void Read(std::span<uint8> out) override;

    [[nodiscard]] uint8 WritePDR(uint8 ddr, uint8 value, bool exle) override;

    void SetSixAxisMode(bool mode);

private:
    bool m_sixAxisMode = false;

    MissionStickReport m_report;

    uint8 m_reportPos = 0;
    bool m_tl = false;
};

} // namespace ymir::peripheral
