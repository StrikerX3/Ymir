#pragma once

#include "loader_result.hpp"

#include <ymir/media/disc.hpp>

#include <filesystem>

// A CUE file describes a full CD image with one session and multiple tracks built from one or more files.
//
// Each line starts with a keyword with a specific function. For Sega Saturn disc images, we're only interested in a
// small subset of them, described below.
//
// - FILE selects a file to map to the disc.
// - TRACK starts a new track.
//   Tracks must be between 1 and 99.
//   The first track is usually 1, but may start at any number.
//   Subsequent track numbers must be sequential.
//   Tracks support several different modes; typical ones found in Sega Saturn images are:
//     MODE1/2048
//     MODE1/2352
//     MODE2/2336
//     MODE2/2352
//     AUDIO
// - PREGAP and POSTGAP specify a length of time (mm:ss:ff) containing silence before of after a track. They may
//   appear at most once per track.
//   If specified, PREGAP must appear at the beginning of a TRACK entry (before all INDEX entries) and POSTGAP must
//   appear at the end.
// - INDEX specifies the starting time (mm:ss:ff) of several parts of the track.
//   Index must be between 0 and 99.
//   - INDEX 00 specifies the starting time of the pregap. It is effecively the same as a PREGAP, except INDEX uses
//     absolute times and PREGAP specifies durations.
//   - INDEX 01 is the start of the actual data in the track. This is the only index stored in the TOC of the disc.
//   - Other INDEX entries specify subindices within a track.
//   The first index in a FILE must be at 00:00:00.
//
// Time is specified in minutes, seconds and frames (mm:ss:ff). There are 75 frames in a second. Each frame
// corresponds to a sector.

namespace ymir::media::loader::bincue {

// Attempts to load a CUE file (along with any referenced BIN files) from cuePath into the specified Disc object.
// Returns true if loading all files succeeded.
// If this function returns false, the Disc object is invalidated.
// preloadToRAM specifies if the entire disc image should be preloaded into memory.
// cbMsg is the callback for message reporting.
bool Load(std::filesystem::path cuePath, Disc &disc, bool preloadToRAM, CbLoaderMessage cbMsg);

} // namespace ymir::media::loader::bincue
