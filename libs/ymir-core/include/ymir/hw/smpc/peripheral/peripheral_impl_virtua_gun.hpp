#pragma once

#include "peripheral_base.hpp"

namespace ymir::peripheral {

/// @brief Implements the Virtua Gun (ID 0xA/1 byte) with:
/// - 2 digital buttons: Trigger, Start
/// - External latch trigger
class VirtuaGun final : public BasePeripheral {
public:
    explicit VirtuaGun(CBPeripheralReport callback);

    void UpdateInputs() override;

    [[nodiscard]] uint8 GetReportLength() const override;

    void Read(std::span<uint8> out) override;

    [[nodiscard]] uint8 WritePDR(uint8 ddr, uint8 value, bool exle) override;

    bool GetExternalLatchCoordinates(uint16 &x, uint16 &y) override;

private:
    VirtuaGunReport m_report;
    bool m_reload = false;
};

} // namespace ymir::peripheral
