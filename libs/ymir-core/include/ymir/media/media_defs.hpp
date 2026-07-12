#pragma once

/**
@file
@brief Sega Saturn CD media definitions.
*/

#include <ymir/util/bitmask_enum.hpp>

#include <ymir/core/types.hpp>

#include <string>

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

    ControlPad = 1u << 0u,       ///< (J) Control Pad
    ControlPad3D = 1u << 1u,     ///< (E) 3D Control Pad
    AnalogPad = 1u << 2u,        ///< (A) Analog controller (includes 3D Control Pad, Virtua Stick)
    Mouse = 1u << 3u,            ///< (M) Shuttle Mouse
    Keyboard = 1u << 4u,         ///< (K) Saturn Keyboard
    SteeringWheel = 1u << 5u,    ///< (S) Arcade Racer
    Multitap = 1u << 6u,         ///< (T) Multitap (6Player)
    VirtuaGun = 1u << 7u,        ///< (G) Virtua Gun
    Pachinko = 1u << 8u,         ///< (P) Pachinko controller
    ROMCart = 1u << 9u,          ///< (R) ROM cart (unspecified)
    RAMCart = 1u << 10u,         ///< (W) RAM carts (size not specified)
    LinkCableJP = 1u << 11u,     ///< (C) Link Cable (Japan)
    LinkCableUS = 1u << 12u,     ///< (D) Link Cable (USA)
    FloppyDiskDrive = 1u << 13u, ///< (F) Floppy Disk Drive
    VideoCDCard = 1u << 14u,     ///< (P) Video CD Card
    Modem = 1u << 15u,           ///< (X) X-Band/Netlink modem
};

/// @brief Converts an area code bitmask to a string
/// @param[in] areaCode the area code bitmask
/// @return a string representation of the area code using Sega's conventions (one letter per area)
std::string AreaCodeToString(AreaCode areaCode);

} // namespace ymir::media

ENABLE_BITMASK_OPERATORS(ymir::media::AreaCode);
ENABLE_BITMASK_OPERATORS(ymir::media::PeripheralCode);
