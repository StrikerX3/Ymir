#pragma once

/**
@file
@brief Sega Saturn CD media definitions.
*/

#include <ymir/util/bitmask_enum.hpp>

#include <ymir/core/types.hpp>

namespace ymir::media {

/// @brief Compatible area codes.
enum class AreaCode : uint16 {
    None = 0,

    Japan = 1u << 0x1,        ///< (J) Domestic NTSC - Japan
    AsiaNTSC = 1u << 0x2,     ///< (T) Asia NTSC - Asia Region (Taiwan, Philippines, South Korea)
    NorthAmerica = 1u << 0x4, ///< (U) North America NTSC - North America (US, Canada), Latin America (Brazil only)
    EuropePAL = 1u << 0xC,    ///< (E) PAL - Europe, Southeast Asia (China, Middle East), Latin America

    CentralSouthAmericaNTSC = 1u << 0x5, ///< (B) (obsolete -> U) Central/South America NTSC
    Korea = 1u << 0x6,                   ///< (K) (obsolete -> T) South Korea
    AsiaPAL = 1u << 0xA,                 ///< (A) (obsolete -> E) Asia PAL
    CentralSouthAmericaPAL = 1u << 0xD,  ///< (L) (obsolete -> E) Central/South America PAL
};

/// @brief Compatible peripherals.
enum class PeripheralCode : uint16 {
    None = 0,

    ControlPad = 1u << 0u,    ///< (J) Control Pad
    AnalogPad = 1u << 1u,     ///< (A) Analog controller
    Mouse = 1u << 2u,         ///< (M) Shuttle Mouse
    Keyboard = 1u << 3u,      ///< (K) Saturn Keyboard
    SteeringWheel = 1u << 4u, ///< (S) Arcade Racer
    Multitap = 1u << 5u,      ///< (T) Multitap (6Player)
    VirtuaGun = 1u << 6u,     ///< (G) Virtua Gun
};

/// @brief Converts an area code bitmask to a string
/// @param[in] areaCode the area code bitmask
/// @return a string representation of the area code using Sega's conventions (one letter per area)
std::string AreaCodeToString(AreaCode areaCode);

} // namespace ymir::media

ENABLE_BITMASK_OPERATORS(ymir::media::AreaCode);
ENABLE_BITMASK_OPERATORS(ymir::media::PeripheralCode);
