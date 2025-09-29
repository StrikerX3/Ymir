#pragma once

/**
@file
@brief CD-ROM error detection code calculation routines.
*/

#include <ymir/core/types.hpp>

#include <span>

namespace ymir::media {

/// @brief Calculates the CRC for the given sector.
/// @param[in] sector the sector to checksum
/// @return the CD-ROM ECC for the sector
uint32 CalcCRC(std::span<uint8, 2064> sector);

} // namespace ymir::media
