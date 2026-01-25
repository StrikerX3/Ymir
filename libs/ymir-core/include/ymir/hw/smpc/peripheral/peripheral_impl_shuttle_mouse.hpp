#pragma once

#include "peripheral_base.hpp"

namespace ymir::peripheral {

/// @brief Implements the Shuttle Mouse (ID 0xE/3 bytes) with:
/// - 4 digital buttons: Start Left Middle Right
/// - 2D relative movement axis
class ShuttleMouse final : public BasePeripheral {
public:
    explicit ShuttleMouse(CBPeripheralReport callback);

    void UpdateInputs() override;

    [[nodiscard]] uint8 GetReportLength() const override;

    void Read(std::span<uint8> out) override;

    [[nodiscard]] uint8 WritePDR(uint8 ddr, uint8 value, bool exle) override;

private:
    ShuttleMouseReport m_report;

    uint8 m_reportPos = 0;
    bool m_tl = false;
    bool m_reset = true;
};

} // namespace ymir::peripheral
