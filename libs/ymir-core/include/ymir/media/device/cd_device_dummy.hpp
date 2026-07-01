#pragma once

/**
@file
@brief Defines `DummyCDDevice`, a CD device that never contains a disc.
*/

#include "cd_device_base.hpp"

namespace ymir::media {

/// @brief A CD device that is always empty.
class DummyCDDevice final : public ICDDevice {
public:
    // -------------------------------------------------------------------------
    // ICDDevice implementation

    std::span<const TOCEntry> GetTOC() override {
        return {};
    }

protected:
    size_t ReadRawSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) override {
        return 0;
    }
};

} // namespace ymir::media
