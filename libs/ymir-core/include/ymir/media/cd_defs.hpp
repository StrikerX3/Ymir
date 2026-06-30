#pragma once

/**
@file
@brief Common CD and CD-ROM data structure definitions.
*/

#include <ymir/core/types.hpp>

namespace ymir::media {

/// @brief Specifies an entry in the table of contents of a disc as reported by the Sega Saturn CD drive.
///
/// Table of contents order and format:
///   point  TNO ctl ADR  M  S  F  zero  PMin             PSec       PFrame
///      A0  00  4/6   1  00 00 00  00   first track num  disc type  00
///      A1  00  4/6   1  00 00 00  00   last track num   00         00
///      A2  00  4/6   1  00 00 00  00   --- start FAD of lead-out area ---
///   01-99* 00  4/6   1  rel. FAD  00   ------- start FAD of track -------
///   (other entries exist but are not needed)
/// * binary-coded decimal
struct TOCEntry {
    uint8 controlADR;        // Bits 7-4 = Control, bits 3-0 = q-Mode
                             //   Control = 0b0100 (0x4) = non-copyable data
                             //   Control = 0b0110 (0x6) = copyable data
                             //   q-Mode = 0b0001 (0x1) = lead-in, user data, lead-out areas
                             //   q-Mode = 0b0010 (0x2) = information area
    uint8 trackNum;          // 00 for lead-in, 01 to 99 for tracks, AA for lead-out
    uint8 pointOrIndex;      // Pointer field for lead-in, index for tracks and lead-out
                             //   For tracks: index 00 is pause, 01 to 99 are various indices within the track
                             //   Lead-out always uses 01
    uint8 min, sec, frac;    // Relative time. During pause (index 00) this time is relative to the start of the track
                             // (index 01) and counts in decreasing order
    uint8 zero;              // Must be 0x00
    uint8 amin, asec, afrac; // Absolute time. Monotonically increasing until the lead-out track.
};

} // namespace ymir::media
