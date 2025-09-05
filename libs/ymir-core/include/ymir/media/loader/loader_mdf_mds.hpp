#pragma once

#include "loader_result.hpp"

#include <ymir/media/disc.hpp>

#include <filesystem>

namespace ymir::media::loader::mdfmds {

// Alcohol 120% Media Descriptor File/Sidecar files describe a full CD image with multiple sessions and tracks.
// The MDF file contains the binary contents and the MDS file contains the disc structure.

// Attempts to load an MDS file and its associated MDF file from mdsPath into the specified Disc object.
// Returns true if loading the files succeeded.
// If this function returns false, the Disc object is invalidated.
// preloadToRAM specifies if the entire disc image should be preloaded into memory.
// cbMsg is the callback for message reporting.
bool Load(std::filesystem::path mdsPath, Disc &disc, bool preloadToRAM, CbLoaderMessage cbMsg);

} // namespace ymir::media::loader::mdfmds
