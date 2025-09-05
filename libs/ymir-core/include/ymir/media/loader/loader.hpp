#pragma once

#include "loader_result.hpp"

#include <ymir/media/disc.hpp>

#include <filesystem>

namespace ymir::media {

// Attempts to load a CD image from any of the supported file formats:
//   MAME CHD    (if provided a .chd file)
//   BIN/CUE     (if provided a .cue file)
//   MDF/MDS     (if provided a .mds file)
//   IMG/CCD/SUB (if provided a .ccd file)
//   ISO         (if provided a .iso file)
// Returns true if loading the file (and any auxiliary files) succeeded.
// If this function returns false, the Disc object is invalidated.
// preloadToRAM specifies if the entire disc image should be preloaded into memory.
// cbMsg is the callback for message reporting.
bool LoadDisc(std::filesystem::path path, Disc &disc, bool preloadToRAM, CbLoaderMessage cbMsg);

} // namespace ymir::media
