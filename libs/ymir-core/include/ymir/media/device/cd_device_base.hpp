#pragma once

/**
@file
@brief Defines `ICDDevice`, an interface for devices capable of reading CDs.
*/

#include <ymir/core/types.hpp>

#include <span>

namespace ymir::media {

/// @brief Interface for physical, virtual and image-based devices capable of reading CDs.
class ICDDevice {
public:
    virtual ~ICDDevice() = default;

    /// @brief Attempts to read a raw sector and returns whether the read was successful or not.
    ///
    /// The output buffer must be at least 2352 bytes long.
    /// When successful, the buffer is filled with the entire raw sector data, which may be synthesized if the
    /// underlying implementation cannot fully read the sector.
    ///
    /// @param[in] frameAddress the frame address to read (0 = first data sector in Saturn discs)
    /// @param[in] out an output buffer with at least 2352 bytes to write data into
    /// @return `true` if the sector was read successfully, `false` if not
    bool ReadRawSector(uint32 frameAddress, std::span<uint8, 2352> out);

protected:
    /// @brief Attempts to read a raw sector and returns the number of bytes read.
    ///
    /// The output buffer is guaranteed to be 2352 bytes.
    /// Implementations must synthesize the rest of the sector if the underlying source cannot read raw sectors.
    ///
    /// @param[in] frameAddress the frame address to read (0 = first data sector in Saturn discs)
    /// @param[in] out an output buffer with at least 2352 bytes to write data into
    /// @return the number of bytes read. 0 if the read failed
    virtual size_t ReadRawSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) = 0;
};

} // namespace ymir::media
