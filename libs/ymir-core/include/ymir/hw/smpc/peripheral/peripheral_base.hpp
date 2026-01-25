#pragma once

#include "peripheral_callbacks.hpp"
#include "peripheral_defs.hpp"

#include <ymir/core/types.hpp>

#include <ymir/util/inline.hpp>

#include <span>

namespace ymir::peripheral {

class BasePeripheral {
public:
    BasePeripheral(PeripheralType type, uint8 typeCode, CBPeripheralReport callback)
        : m_type(type)
        , m_typeCode(typeCode)
        , m_cbPeripheralReport(callback) {}

    virtual ~BasePeripheral() = default;

    // If this peripheral object has the specified PeripheralType, casts it to the corresponding concrete type.
    // Returns nullptr otherwise.
    template <PeripheralType type>
    FORCE_INLINE typename detail::PeripheralType_t<type> *As() {
        if (m_type == type) {
            return static_cast<detail::PeripheralType_t<type> *>(this);
        } else {
            return nullptr;
        }
    }

    // If this peripheral object has the specified PeripheralType, casts it to the corresponding concrete type.
    // Returns nullptr otherwise.
    template <PeripheralType type>
    FORCE_INLINE const typename detail::PeripheralType_t<type> *As() const {
        if (m_type == type) {
            return static_cast<detail::PeripheralType_t<type> *>(this);
        } else {
            return nullptr;
        }
    }

    std::string_view GetName() const {
        return GetPeripheralName(m_type);
    }

    PeripheralType GetType() const {
        return m_type;
    }

    uint8 GetTypeCode() const {
        return m_typeCode;
    }

    virtual bool IsConnected() const {
        return true;
    }

    /// @brief Updates the peripheral's inputs.
    virtual void UpdateInputs() = 0;

    /// @brief Retrieves the length of the peripheral data report in bytes.
    /// @return the number of bytes of the peripheral report
    virtual uint8 GetReportLength() const = 0;

    /// @brief Reads the report into the specified output buffer.
    /// @param[out] out the output buffer
    virtual void Read(std::span<uint8> out) = 0;

    /// @brief Performs low-level access to the peripheral.
    /// @param[in] ddr the value of the DDR register (direction bits)
    /// @param[in] value the value to write
    /// @param[in] exle the state of the port's EXLE bit
    /// @return the response data
    virtual uint8 WritePDR(uint8 ddr, uint8 value, bool exle) = 0;

    /// @brief Retrieves the screen coordinates for the external latch signal.
    /// @param[out] x the horizontal coordinate
    /// @param[out] y the vertical coordinate
    /// @return `true` if the coordinates were retrieved, `false` otherwise
    virtual bool GetExternalLatchCoordinates(uint16 &x, uint16 &y) {
        return false;
    }

private:
    const PeripheralType m_type;
    uint8 m_typeCode;

protected:
    friend class PeripheralPort;

    CBPeripheralReport m_cbPeripheralReport;

    void SetTypeCode(uint8 typeCode) {
        m_typeCode = typeCode & 0xF;
    }
};

} // namespace ymir::peripheral
