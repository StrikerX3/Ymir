#pragma once

#include "peripheral_base.hpp"

namespace ymir::peripheral {

/// @brief Implements the 3D Control Pad (ID 0x0/2 bytes in digital mode, 0x1/6 bytes in analog mode) with:
/// - 6 digital buttons: ABC XYZ
/// - 2 analog triggers: L R, with values ranging from 0 (minimum) to 255 (maximum)
/// - Start button
/// - Directional pad
/// - Analog stick, with values ranging from 0 (left/up) to 127 (center) to 255 (right/down)
/// - Analog/digital mode toggle
///
/// In digital mode, the peripheral behaves exactly like a regular Control Pad, with L and R translated to digital
/// values based on the following thresholds:
/// - The button state is set to ON when the trigger value is 145 or higher
/// - The button state is set to OFF when the trigger value is 85 or lower
class AnalogPad final : public BasePeripheral {
public:
    explicit AnalogPad(CBPeripheralReport callback);

    void UpdateInputs() override;

    [[nodiscard]] uint8 GetReportLength() const override;

    void Read(std::span<uint8> out) override;

    [[nodiscard]] uint8 WritePDR(uint8 ddr, uint8 value, bool exle) override;

    void SetAnalogMode(bool mode);

private:
    bool m_analogMode = false;

    AnalogPadReport m_report;

    uint8 m_reportPos = 0;
    bool m_tl = false;
};

} // namespace ymir::peripheral
