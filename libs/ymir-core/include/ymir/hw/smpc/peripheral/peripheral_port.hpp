#pragma once

#include "peripheral_base.hpp"
#include "peripheral_impl_analog_pad.hpp"
#include "peripheral_impl_arcade_racer.hpp"
#include "peripheral_impl_control_pad.hpp"
#include "peripheral_impl_mission_stick.hpp"
#include "peripheral_impl_null.hpp"
#include "peripheral_impl_shuttle_mouse.hpp"
#include "peripheral_impl_virtua_gun.hpp"

#include <ymir/core/types.hpp>

#include <memory>
#include <span>
#include <utility>

// -----------------------------------------------------------------------------
// Forward declarations

namespace ymir::smpc {

class SMPC;

} // namespace ymir::smpc

// -----------------------------------------------------------------------------

namespace ymir::peripheral {

class PeripheralPort {
public:
    PeripheralPort() {
        DisconnectPeripherals();
    }

    void SetPeripheralReportCallback(CBPeripheralReport callback) {
        m_cbPeripheralReport = callback;
        m_peripheral->m_cbPeripheralReport = callback;
    }

    ControlPad *ConnectControlPad() {
        return ConnectPeripheral<ControlPad>(m_cbPeripheralReport);
    }

    AnalogPad *ConnectAnalogPad() {
        return ConnectPeripheral<AnalogPad>(m_cbPeripheralReport);
    }

    ArcadeRacerPeripheral *ConnectArcadeRacer() {
        return ConnectPeripheral<ArcadeRacerPeripheral>(m_cbPeripheralReport);
    }

    MissionStick *ConnectMissionStick() {
        return ConnectPeripheral<MissionStick>(m_cbPeripheralReport);
    }

    VirtuaGun *ConnectVirtuaGun() {
        return ConnectPeripheral<VirtuaGun>(m_cbPeripheralReport);
    }

    ShuttleMouse *ConnectShuttleMouse() {
        return ConnectPeripheral<ShuttleMouse>(m_cbPeripheralReport);
    }

    void DisconnectPeripherals() {
        ConnectPeripheral<NullPeripheral>();
    }

    BasePeripheral &GetPeripheral() {
        return *m_peripheral;
    }
    const BasePeripheral &GetPeripheral() const {
        return *m_peripheral;
    }

private:
    // TODO: implement multi-tap as an array of peripherals
    std::unique_ptr<BasePeripheral> m_peripheral;

    CBPeripheralReport m_cbPeripheralReport;

    template <typename T, typename... Args>
        requires std::derived_from<T, BasePeripheral>
    T *ConnectPeripheral(Args &&...args) {
        T *peripheral = new T(std::forward<Args>(args)...);
        m_peripheral.reset(peripheral);
        return peripheral;
    }

    friend class smpc::SMPC;

    // Updates inputs and returns the report length
    uint32 UpdateInputs() const {
        if (m_peripheral->IsConnected()) {
            m_peripheral->UpdateInputs();
            if (m_peripheral->GetType() != PeripheralType::VirtuaGun) {
                return 2 + m_peripheral->GetReportLength();
            }
        }
        return 1;
    }

    void Read(std::span<uint8> out) const {
        if (m_peripheral->IsConnected()) {
            if (m_peripheral->GetType() == PeripheralType::VirtuaGun) {
                // [0] 0xA0 -> 7-4 = A=6Player connected; 3-0 = 0 connectors
                out[0] = 0xA0;
                return;
            }
            if (out.size() <= 15) {
                // TODO: support multi-tap
                // TODO: support report lengths longer than 15
                // [0] 0xF1 -> 7-4 = F=no multitap/device directly connected; 3-0 = 1 device
                // [1] 0xIN -> 7-4 = I=control pad ID; 3-0 = N data bytes
                // [2..N]   -> peripheral-specific report
                out[0] = 0xF1;
                out[1] = (m_peripheral->GetTypeCode() << 4u) | m_peripheral->GetReportLength();
                m_peripheral->Read(out.subspan(2));
                return;
            }
        }

        // Nothing connected, or report is too long

        // [0] 0xF0 -> 7-4 = F=no multitap/device directly connected; 3-0 = 0 devices
        out[0] = 0xF0;
    }

    uint8 WritePDR(uint8 ddr, uint8 value, bool exle) {
        return m_peripheral->WritePDR(ddr, value, exle);
    }

    bool GetExternalLatchCoordinates(uint16 &x, uint16 &y) const {
        return m_peripheral->GetExternalLatchCoordinates(x, y);
    }
};

} // namespace ymir::peripheral
