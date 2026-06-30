#pragma once

/**
@file
@brief CD utilities, including CD-ROM error detection code calculation routines and sector data synthesis.
*/

#include <ymir/core/types.hpp>

#include <span>

namespace ymir::media {

/// @brief Calculates the CRC for the given sector.
/// @param[in] sector the sector to checksum
/// @return the CD-ROM ECC for the sector
uint32 CalcCRC(std::span<uint8, 2064> sector);

/// @brief Synthesizes missing data from the sector given its read size.
/// The missing data is derived from the read size:
/// - If less than 2352 bytes, synthesizes the 12 synchronization bytes
/// - If less than 2340 bytes, synthesizes the 4-byte header
/// - If less than 2336 bytes, synthesizes the EDC and ECC codes
/// This function will not (and cannot) synthesize missing Mode 2 Form 2 subheader data.
///
/// For sizes less than 2352 bytes, it is assumed that the existing data is in the correct location in the buffer (e.g.
/// the user data area must start at offset 0x10 in the buffer). This function will not move any data to make room for
/// sync and header bytes.
///
/// @param[in] sector the sector buffer
/// @param[in] sectorSize the size of the sector data read into the buffer
/// @param[in] frameAddress the frame address of the sector
/// @param[in] controlADR the control and ADR bits of the sector
/// @param[in] mode whether the sector is a Mode 2 sector (any form)
void SynthesizeSectorData(std::span<uint8, 2352> sector, uint32 sectorSize, uint32 frameAddress, uint8 controlADR,
                          bool mode2);

} // namespace ymir::media
