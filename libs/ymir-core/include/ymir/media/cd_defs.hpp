#pragma once

/**
@file
@brief Common CD and CD-ROM data structure definitions.
*/

#include <ymir/core/types.hpp>

#include <array>
#include <span>
#include <vector>

namespace ymir::media {

/// @brief Indicates the current state of a CD drive.
enum class DriveState {
    Unknown,      ///< State cannot be determined, typically because a physical device was disconnected
    TrayOpen,     ///< Tray open, no media
    NoDisc,       ///< Tray closed, no media
    MediaPresent, ///< Tray closed, media present
};

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

/// @brief Expanded track information derived from the table of contents.
struct TrackInfo {
    uint32 startFrameAddress;
    uint32 endFrameAddress;
    uint8 number;
    uint8 controlADR;
};

/// @brief A disc's table of contents.
class TOC {
public:
    TOC() {
        Clear();
    }

    /// @brief Loads a table of contents from the given source.
    /// @param[in] table the source TOC to copy from
    void LoadFrom(std::span<const TOCEntry> table);

    /// @brief Clears the table of contents.
    void Clear();

    /// @brief Retrieves the table of contents in the original disc's format.
    /// @return the CD table of contents in its original format.
    std::span<const TOCEntry> GetTable() const {
        return m_table;
    }

    /// @brief Retrieves the table of contents in Saturn's format.
    /// @return the CD table of contents formatted for the output of the CD Block Get TOC command.
    const std::array<uint32, 99 + 3> &GetSaturnTable() const {
        return m_saturnTable;
    }

    /// @brief Retrieves the starting frame address of the disc.
    /// @return the starting frame address
    [[nodiscard]] uint32 GetStartFrameAddress() const {
        return m_startFrameAddress;
    }

    /// @brief Retrieves the ending frame address of the disc.
    /// @return the ending frame address
    [[nodiscard]] uint32 GetEndFrameAddress() const {
        return m_endFrameAddress;
    }

    /// @brief Retrieves the starting lead-out frame address of the disc.
    /// @return the starting lead-out frame address
    [[nodiscard]] uint32 GetLeadOutFrameAddress() const {
        return m_endFrameAddress + 1;
    }

    /// @brief Retrieves the first track number.
    /// @return the first track number, or 0xFF if there is no disc
    uint8 GetFirstTrackNumber() const {
        return m_firstTrackIndex < 99 ? m_firstTrackIndex + 1 : 0xFF;
    }

    /// @brief Retrieves the last track number.
    /// @return the last track number, or 0xFF if there is no disc
    uint8 GetLastTrackNumber() const {
        return m_lastTrackIndex < 99 ? m_lastTrackIndex + 1 : 0xFF;
    }

    /// @brief Retrieves information about the specified track.
    /// @param[in] number the tracak number
    /// @return a pointer to the specified track information, or `nullptr` if no such track exists or there is no disc
    /// inserted
    const TrackInfo *GetTrackInfoForNumber(uint8 number) const;

    /// @brief Retrieves track information for the specified frame address.
    /// @param[in] frameAddress the frame address
    /// @return a pointer to the track information at the specified frame address, or `nullptr` if no such track exists
    /// or there is no disc inserted
    const TrackInfo *GetTrackInfoForFAD(uint32 frameAddress) const;

    /// @brief Determines the track number for the specified frame address
    /// @param[in] frameAddress the frame address to check.
    /// @return 1 to 99 if the frame address points to a valid track in the disc, 0 if in the lead-in area, 100 if in
    /// the lead-out area, or 0xFF if there is no disc.
    uint8 GetTrackNumberForFAD(uint32 frameAddress) const;

    /// @brief Retrieves the starting frame address for the given track number.
    /// @param[in] trackNum the track number
    /// @return the starting frame address, or 0xFFFFFFFF if the track number is out of the valid range for the disc.
    uint32 GetStartFADForTrack(uint8 trackNum) const {
        if (trackNum < m_firstTrackIndex + 1 || trackNum > m_lastTrackIndex + 1) {
            return 0xFFFFFFFF;
        }
        return m_trackInfos[trackNum - 1].startFrameAddress;
    }

    /// @brief Retrieves the ending frame address for the given track number.
    /// @param[in] trackNum the track number
    /// @return the starting frame address, or 0xFFFFFFFF if the track number is out of the valid range for the disc.
    uint32 GetEndFADForTrack(uint8 trackNum) const {
        if (trackNum < m_firstTrackIndex + 1 || trackNum > m_lastTrackIndex + 1) {
            return 0xFFFFFFFF;
        }
        return m_trackInfos[trackNum - 1].endFrameAddress;
    }

private:
    std::vector<TOCEntry> m_table;            ///< The table as it exists on the disc
    std::array<uint32, 99 + 3> m_saturnTable; ///< The table, converted to Saturn format

    // -----------------------------------------------------------------------------------------------------------------
    // Cached data parsed from the TOC

    uint32 m_startFrameAddress = 0;
    uint32 m_endFrameAddress = 0;

    uint8 m_firstTrackIndex = 0xFF;
    uint8 m_lastTrackIndex = 0xFF;

    std::array<TrackInfo, 99> m_trackInfos;
};

/// @brief Disc position extracted from Subcode Q data, with ADR=1.
struct DiscPosition {
    uint8 controlADR; // bits 0..3=ADR; bits 4..7=control
    uint8 track;      // 0x01..0x99(bcd)=tracks, 0xAA=lead-out
    uint8 index;      // 0x00..0x99(bcd)=index, (=0x01 in lead-out)
    uint8 min;        // track-relative [M]SF address
    uint8 sec;        // track-relative M[S]F address
    uint8 frac;       // track-relative MS[F] address
    uint8 zero;       // always zero
    uint8 amin;       // absolute [M]SF address
    uint8 asec;       // absolute M[S]F address
    uint8 afrac;      // absolute MS[F] address
};

} // namespace ymir::media
