#include <ymir/media/cd_defs.hpp>

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/dev_assert.hpp>

#include <cassert>

namespace ymir::media {

void TOC::LoadFrom(std::span<const TOCEntry> table) {
    m_table.resize(table.size());
    std::copy(table.begin(), table.end(), m_table.begin());

    // Parse table, convert to Saturn format and extract commonly used parameters.
    m_saturnTable.fill(0xFFFFFFFF);
    for (const TOCEntry &entry : table) {
        if (entry.pointOrIndex >= 0x01 && entry.pointOrIndex <= 0x99) {
            // Regular tracks
            //
            //  31 28 27 24 23                     0
            // +-----+-----+------------------------+
            // | CTL | ADR | Index 01 frame address |
            // +-----+-----+-------+-------+--------+
            //             | AMIN  | ASEC  | AFRAC  |
            //             +-------+-------+--------+
            //              23   16 15    8 7      0

            const uint8 trackNum = util::from_bcd(entry.pointOrIndex);
            const uint32 frameAddress =
                util::from_bcd(entry.amin) * 75 * 60 + util::from_bcd(entry.asec) * 75 + util::from_bcd(entry.afrac);
            TrackInfo &info = m_trackInfos[trackNum - 1];
            info.startFrameAddress = frameAddress;
            info.number = trackNum;
            info.controlADR = entry.controlADR;
            if (trackNum > 1) {
                m_trackInfos[trackNum - 2].endFrameAddress = frameAddress - 1;
            }
            m_saturnTable[trackNum - 1] = (entry.controlADR << 24u) | frameAddress;
        } else if (entry.pointOrIndex == 0xA0 || entry.pointOrIndex == 0xA1) {
            // Point A0 - first data track
            //
            //  31 28 27 24 23            16 15     0
            // +-----+-----+----------------+--------+
            // | CTL | ADR | 1st track no.  | (zero) |
            // +-----+-----+----------------+--------+
            //             |      AMIN      |
            //             +----------------+
            //              23            16
            //
            // Point A1 - last data track
            //
            //  31 28 27 24 23            16 15     0
            // +-----+-----+----------------+--------+
            // | CTL | ADR | Last track no. | (zero) |
            // +-----+-----+----------------+--------+
            //             |      AMIN      |
            //             +----------------+
            //              23            16
            //
            if (entry.pointOrIndex == 0xA0) {
                m_firstTrackIndex = util::from_bcd(entry.amin) - 1;
            } else if (entry.pointOrIndex == 0xA1) {
                m_lastTrackIndex = util::from_bcd(entry.amin) - 1;
            }
            m_saturnTable[entry.pointOrIndex - 0xA0 + 99] = (entry.controlADR << 24u) | (entry.amin << 16u);

        } else if (entry.pointOrIndex == 0xA2) {
            // Point A2 - start of lead-out track
            //
            //  31 28 27 24 23                     0
            // +-----+-----+------------------------+
            // | CTL | ADR | Lead-out frame address |
            // +-----+-----+-------+-------+--------+
            //             | AMIN  | ASEC  | AFRAC  |
            //             +-------+-------+--------+
            //              23   16 15    8 7      0

            const uint32 frameAddress =
                util::from_bcd(entry.amin) * 75 * 60 + util::from_bcd(entry.asec) * 75 + util::from_bcd(entry.afrac);
            m_endFrameAddress = frameAddress - 1;
            m_saturnTable[entry.pointOrIndex - 0xA0 + 99] = (entry.controlADR << 24u) | frameAddress;
        }
    }
    assert(m_lastTrackIndex <= 99);
    m_trackInfos[m_lastTrackIndex].endFrameAddress = m_endFrameAddress;
}

void TOC::Clear() {
    m_table.clear();
    m_saturnTable.fill(0xFFFFFFFF);
    m_startFrameAddress = 0;
    m_endFrameAddress = 0;
    m_firstTrackIndex = 0;
    m_lastTrackIndex = 0;
    m_trackInfos.fill({});
}

const TrackInfo *TOC::GetTrackInfoForNumber(uint8 number) const {
    if (number < m_firstTrackIndex + 1 || number > m_lastTrackIndex + 1) {
        return nullptr;
    }
    return &m_trackInfos[number - 1];
}

const TrackInfo *TOC::GetTrackInfoForFAD(uint32 frameAddress) const {
    if (m_table.empty()) {
        return nullptr;
    }
    if (frameAddress < m_startFrameAddress) {
        // Lead-in area
        return nullptr;
    }
    if (frameAddress > m_endFrameAddress) {
        // Lead-out area
        return nullptr;
    }

    // Find track
    const auto begin = m_trackInfos.cbegin();
    const auto first = begin + m_firstTrackIndex;
    const auto last = begin + m_lastTrackIndex + 1;
    const auto pos =
        std::upper_bound(first, last, frameAddress, [](uint32 lhsFrameAddress, const TrackInfo &rhsInfo) -> bool {
            return lhsFrameAddress < rhsInfo.endFrameAddress;
        });
    if (pos != last && frameAddress >= pos->startFrameAddress) {
        return &*pos;
    }

    // If we get here, the FAD is out of range
    return nullptr;
}

uint8 TOC::GetTrackNumberForFAD(uint32 frameAddress) const {
    if (m_table.empty()) {
        return 0xFF;
    }
    if (frameAddress < m_startFrameAddress) {
        // Lead-in area
        return 0;
    }
    if (frameAddress > m_endFrameAddress) {
        // Lead-out area
        return 100;
    }

    // Find track
    const TrackInfo *trackInfo = GetTrackInfoForFAD(frameAddress);
    if (trackInfo == nullptr) {
        // Possibly malformed TOC
        return 0xFF;
    }
    return trackInfo->number;
}

} // namespace ymir::media
