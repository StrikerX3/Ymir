#pragma once

#include "peripheral_base.hpp"

namespace ymir::peripheral {

/// @brief Implements the Arcade Racer controller (ID 0x1/3 bytes) with:
/// - 6 digital buttons: ABC XYZ
/// - Start button
/// - Butterfly shifter unit (mapped to D-Pad up/down)
/// - Analog wheel
///
/// The analog wheel is also converted to D-Pad left/right when its value reaches the following thresholds:
/// - The left button is set to ON when the trigger value is 111 or lower
/// - The right button is set to ON when the trigger value is 143 or higher
class ArcadeRacerPeripheral final : public BasePeripheral {
public:
    explicit ArcadeRacerPeripheral(CBPeripheralReport callback);

    void UpdateInputs() override;

    [[nodiscard]] uint8 GetReportLength() const override;

    void Read(std::span<uint8> out) override;

    [[nodiscard]] uint8 WritePDR(uint8 ddr, uint8 value, bool exle) override;

private:
    ArcadeRacerReport m_report;

    uint8 m_reportPos = 0;
    bool m_tl = false;
};

} // namespace ymir::peripheral
