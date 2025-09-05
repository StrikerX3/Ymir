#pragma once

#include "loader_result.hpp"

#include <ymir/media/disc.hpp>

#include <filesystem>

// CHD (Compressed Hunks of Data) is a file format created by MAME authors to store compressed CD-ROM data.
//
// This loader uses libchdr (https://github.com/rtissera/libchdr) to load Saturn discs from CHD files.

namespace ymir::media::loader::chd {

// Attempts to load a CHD file from chdPath into the specified Disc object.
// Returns true if loading all files succeeded.
// If this function returns false, the Disc object is invalidated.
// preloadToRAM specifies if the entire disc image should be preloaded into memory.
// cbMsg is the callback for message reporting.
bool Load(std::filesystem::path chdPath, Disc &disc, bool preloadToRAM, CbLoaderMessage cbMsg);

} // namespace ymir::media::loader::chd
