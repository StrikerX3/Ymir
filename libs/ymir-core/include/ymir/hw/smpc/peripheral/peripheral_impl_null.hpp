#pragma once

#include "peripheral_base.hpp"

namespace ymir::peripheral {

class NullPeripheral final : public BasePeripheral {
public:
    NullPeripheral()
        : BasePeripheral(PeripheralType::None, 0x0, {}) {}

    [[nodiscard]] bool IsConnected() const override {
        return false;
    }

    void UpdateInputs() override {}

    [[nodiscard]] uint8 GetReportLength() const override {
        return 0;
    }

    void Read(std::span<uint8> out) override {}

    [[nodiscard]] uint8 WritePDR(uint8 ddr, uint8 value, bool exle) override {
        return 0;
    }
};

} // namespace ymir::peripheral
