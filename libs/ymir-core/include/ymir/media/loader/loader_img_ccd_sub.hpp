#pragma once

#include "loader_result.hpp"

#include <ymir/media/disc.hpp>

#include <filesystem>

// A CCD (CloneCD Control) file describes a full CD image with multiple session and multiple tracks built from one or
// more files.
//
// CCD files are text files resembling the INI format, with sections in [brackets] and key=value assignments per
// section. All values seem to be integers in signed decimal or unsigned hexadecimal format.
//
// The [CloneCD] section contains the version of the software that generated this image.
//
// The [Disc] section describes the overall contents of the disc:
// - TocEntries: The number of TOC entries, in [Entry <index>] sections
// - Sessions: The number of sessions, in [Session <number>] sections
// - DataTracksScrambled: If set to 1, the data blocks are scrambled in the binary image
// - CDTextLength: The size of the CD-Text file, minus the terminating null character
// - CATALOG: The Media Catalog Number (MCN) of the disc
//
// Scrambled blocks are not supported by this loader.
//
// If the disc has CD-Text metadata, it will be described in the [CDText] section which this loader ignores.
//
// For each session starting at 1, there's a [Session <number>] with two unknown properties: PreGapMode and PreGapSubC.
// The loader also ignores these sections and their contents.
//
// Each TOC entry starting with index 0 has a [Entry <index>] section with these properties:
// - Session: The session number to which this TOC entry belongs
// - Point: The POINT (pointer) index
// - ADR: The ADR code
// - Control: The control code
// - TrackNo: The track number (non-zero for points 1-99, zero for points A0, A1, A2, etc.)
// - AMin, ASec, AFrame: Absolute time
// - ALBA: The absolute Logical Block Address
// - Zero: Must be zero
// - PMin, PSec, PFrame: The start position of the track specified by TrackNo
// - PLBA: The starting Logical Block Address of the track
//
// Finally, each track has a [TRACK <number>] section starting from 1 with a few properties:
// - MODE: Indicates the track mode - 0 is audio, 1 is MODE1/2352 and 2 is MODE2/2352
// - ISRC: The International Standard Recording Code
// - INDEX <n>: Starting LBAs of individual indexes in a track
// - FLAGS: Special properties such as 4-channel audio, some forms of DRM, etc.

namespace ymir::media::loader::ccd {

// Attempts to load a CUE file (along with any referenced BIN files) from cuePath into the specified Disc object.
// Returns true if loading all files succeeded.
// If this function returns false, the Disc object is invalidated.
// preloadToRAM specifies if the entire disc image should be preloaded into memory.
// cbMsg is the callback for message reporting.
bool Load(std::filesystem::path ccdPath, Disc &disc, bool preloadToRAM, CbLoaderMessage cbMsg);

} // namespace ymir::media::loader::ccd
