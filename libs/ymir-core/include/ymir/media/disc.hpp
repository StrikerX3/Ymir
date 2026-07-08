#pragma once

#include <ymir/core/types.hpp>

#include <ymir/media/binary_reader/binary_reader.hpp>

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/dev_assert.hpp>

#include "cd_defs.hpp"
#include "cd_utils.hpp"
#include "saturn_header.hpp"
#include "subheader.hpp"

// #include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <span>
#include <vector>

namespace ymir::media {

struct Index {
    uint32 startFrameAddress = 0;
    uint32 endFrameAddress = 0;
};

struct Track {
    std::unique_ptr<IBinaryReader> binaryReader;
    uint32 index = 0;
    uint32 unitSize = 0;   // size of a unit, always >= sectorSize
    uint32 sectorSize = 0; // size of the valid data in the sector
    uint32 headerOffset = 0;
    uint32 userDataOffset = 0;
    uint8 controlADR = 0;
    bool mode2 = false;
    bool interleavedSubchannel = false; // true=96-byte PW subchannel, interleaved
    bool bigEndian = false;             // indicates audio data endianness on audio tracks
    bool hasSyncBytes = false;
    bool hasHeader = false;
    bool hasECC = false;

    uint32 startFrameAddress = 0;
    uint32 endFrameAddress = 0;
    uint32 index01FrameAddress = 0;

    std::vector<Index> indices; // 00 to 99

    uint8 FindIndex(uint32 frameAddress) const {
        auto it = std::find_if(indices.begin(), indices.end(), [=](const Index &index) {
            return frameAddress >= index.startFrameAddress && frameAddress <= index.endFrameAddress;
        });

        if (it == indices.end()) {
            return 0xFF;
        } else {
            return std::distance(indices.begin(), it);
        }
    }

    void SetSectorSize(uint32 size) {
        unitSize = size;
        sectorSize = size;
        hasSyncBytes = size >= 2352;
        hasHeader = size >= 2340;
        hasECC = size >= 2336;
        headerOffset = hasSyncBytes ? 12 : 0;
        userDataOffset = hasSyncBytes ? (mode2 ? 24 : 16) : hasHeader ? (mode2 ? 12 : 4) : 0;
    }

    // Reads the user data portion of a sector.
    // Returns true if the sector was read successfully.
    // Returns false if the sector could not be fully read or the frame address is out of range.
    // TODO: support CD-ROM XA mode 2 form 2 user data (2324 bytes)
    bool ReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outBuf) const {
        if (frameAddress < startFrameAddress || frameAddress > endFrameAddress) [[unlikely]] {
            return false;
        }

        const uint32 sectorOffset = (frameAddress - startFrameAddress) * unitSize;
        return binaryReader->Read(sectorOffset + userDataOffset, 2048, outBuf) == 2048;
    }

    // Reads a sector from the given absolute frame address.
    // If the track sector size is less than 2352, the missing parts are synthesized in the output buffer:
    // - 2048 bytes: sync bytes + header + checksums/ECC
    // - 2336 bytes: sync bytes + header
    // - 2340 bytes: sync bytes
    // - 2352 bytes: nothing
    // Returns true if the sector was read successfully.
    // Returns false if the sector could not be fully read, the frame address is out of range or the requested size is
    // unsupported.
    bool ReadSector(uint32 frameAddress, std::span<uint8, 2352> outBuf) const {
        if (frameAddress < startFrameAddress || frameAddress > endFrameAddress) [[unlikely]] {
            return false;
        }

        // Audio tracks always have 2352 bytes
        if (controlADR == 0x01) {
            const uint32 sectorOffset = (frameAddress - startFrameAddress) * unitSize;
            const uintmax_t readSize = binaryReader->Read(sectorOffset, 2352, outBuf);
            return readSize == 2352;
        }

        // Determine which components are present and where to write the sector data in the output buffer
        const uint32 writeOffset = !hasSyncBytes * 12 + !hasHeader * 4;

        // Try to read raw sector data based on specifications
        const uint32 outputSize = std::min(sectorSize, 2352u);
        const uint32 sectorOffset = (frameAddress - startFrameAddress) * unitSize;
        const std::span<uint8> output{outBuf.begin() + writeOffset, outputSize};
        const uintmax_t readSize = binaryReader->Read(sectorOffset, outputSize, output);
        if (readSize != outputSize) {
            return false;
        }

        /*fmt::println("== SECTOR DUMP - FAD {:X} ==", frameAddress);
        fmt::println("Track sector size: {} bytes", sectorSize);
        fmt::println("Track unit size:   {} bytes", unitSize);*/

        // Fill in any missing data
        SynthesizeSectorData(outBuf, outputSize, frameAddress, controlADR, mode2);

        /*fmt::println("Raw sector data:");
        for (uint32 i = 0; i < outBuf.size(); i++) {
            fmt::print("{:02X}", outBuf[i]);
            if (i % 32 == 31) {
                fmt::println("");
            } else {
                fmt::print(" ");
            }
        }
        fmt::println("");*/

        return true;
    }

    void ReadSectorSubheader(uint32 frameAddress, Subheader &subheader) const {
        subheader.fileNum = 0;
        subheader.chanNum = 0;
        subheader.submode = 0;
        subheader.codingInfo = 0;

        if (frameAddress < startFrameAddress || frameAddress > endFrameAddress) [[unlikely]] {
            return;
        }

        // Subheader is only present in mode 2 tracks
        if (!mode2) {
            return;
        }

        // Read subheader
        const uintmax_t baseOffset = static_cast<uintmax_t>(frameAddress - startFrameAddress) * unitSize;
        const uintmax_t subheaderOffset = hasSyncBytes ? 16 : hasHeader ? 4 : 0;
        std::array<uint8, 4> subheaderData{};
        if (binaryReader->Read(baseOffset + subheaderOffset, 4, subheaderData) < 4) {
            return;
        }

        // Fill in subheader data
        subheader.fileNum = subheaderData[0];
        subheader.chanNum = subheaderData[1];
        subheader.submode = subheaderData[2];
        subheader.codingInfo = subheaderData[3];
    }
};

struct Session {
    std::array<Track, 99> tracks;
    uint32 numTracks = 0;
    uint32 firstTrackIndex = 0;
    uint32 lastTrackIndex = 0;

    uint32 startFrameAddress = 0;
    uint32 endFrameAddress = 0;

    Session() {
        for (int i = 0; i < tracks.size(); i++) {
            tracks[i].index = i + 1;
        }
    }

    const Track *FindTrack(uint32 absFrameAddress) const {
        const uint8 trackIndex = FindTrackIndex(absFrameAddress);
        if (trackIndex != 0xFF) {
            return &tracks[trackIndex];
        }
        return nullptr;
    }

    uint8 FindTrackIndex(uint32 absFrameAddress) const {
        for (int i = 0; i < numTracks; i++) {
            const auto &track = tracks[firstTrackIndex + i];
            if (absFrameAddress >= track.startFrameAddress && absFrameAddress <= track.endFrameAddress) {
                return firstTrackIndex + i;
            }
        }
        return 0xFF;
    }

    // TOC entries listed in the lead-in area
    std::array<TOCEntry, 99 + 3> toc{};
    uint32 tocSize = 0;

    // Build table of contents using track information
    void BuildTOC() {
        // -----------------------------------------------------------------------------------------
        // Raw TOC data (10 bytes per entry)
        // Stored in lead-in area of the disc

        tocSize = 0;

        // Point A0 - first data track
        {
            auto &tocEntry = toc[tocSize++];
            tocEntry.controlADR = 0x41;
            tocEntry.trackNum = 0x00;
            tocEntry.pointOrIndex = 0xA0;
            tocEntry.min = 0x00;
            tocEntry.sec = 0x00;
            tocEntry.frac = 0x00;
            tocEntry.zero = 0x00;
            tocEntry.amin = util::to_bcd(firstTrackIndex + 1);
            tocEntry.asec = 0x00;
            tocEntry.afrac = 0x00;
        }

        // Point A1 - last data track
        {
            auto &tocEntry = toc[tocSize++];
            tocEntry.controlADR = 0x41;
            tocEntry.trackNum = 0x00;
            tocEntry.pointOrIndex = 0xA1;
            tocEntry.min = 0x00;
            tocEntry.sec = 0x00;
            tocEntry.frac = 0x00;
            tocEntry.zero = 0x00;
            tocEntry.amin = util::to_bcd(lastTrackIndex + 1);
            tocEntry.asec = 0x00;
            tocEntry.afrac = 0x00;
        }

        // Point A2 - start of leadout track
        {
            const uint32 leadOutFAD = endFrameAddress + 1;
            auto &tocEntry = toc[tocSize++];
            tocEntry.controlADR = 0x41;
            tocEntry.trackNum = 0x00;
            tocEntry.pointOrIndex = 0xA2;
            tocEntry.min = util::to_bcd(startFrameAddress / 75 / 60);
            tocEntry.sec = util::to_bcd(startFrameAddress / 75 % 60);
            tocEntry.frac = util::to_bcd(startFrameAddress % 75);
            tocEntry.zero = 0x00;
            tocEntry.amin = util::to_bcd(leadOutFAD / 75 / 60);
            tocEntry.asec = util::to_bcd(leadOutFAD / 75 % 60);
            tocEntry.afrac = util::to_bcd(leadOutFAD % 75);
        }

        // Tracks
        for (int i = 0; i < 99; i++) {
            auto &track = tracks[i];
            if (track.controlADR == 0x00) {
                continue;
            }

            const uint32 relFAD = track.index01FrameAddress - track.startFrameAddress;
            auto &entry = toc[tocSize++];
            entry.controlADR = track.controlADR;
            entry.trackNum = 0x00;
            entry.pointOrIndex = util::to_bcd(i + 1);
            entry.min = util::to_bcd(relFAD / 75 / 60);
            entry.sec = util::to_bcd(relFAD / 75 % 60);
            entry.frac = util::to_bcd(relFAD % 75);
            entry.zero = 0x00;
            entry.amin = util::to_bcd(track.index01FrameAddress / 75 / 60);
            entry.asec = util::to_bcd(track.index01FrameAddress / 75 % 60);
            entry.afrac = util::to_bcd(track.index01FrameAddress % 75);
        }
    }
};

struct Disc {
    std::vector<Session> sessions;

    SaturnHeader header;

    Disc() {
        Invalidate();
    }

    Disc(const Disc &) = delete;
    Disc(Disc &&) = default;

    Disc &operator=(const Disc &) = delete;
    Disc &operator=(Disc &&) = default;

    void Swap(Disc &&disc) {
        sessions.swap(disc.sessions);
        header.Swap(std::move(disc.header));
    }

    void Invalidate() {
        sessions.clear();
        header.Invalidate();
    }
};

} // namespace ymir::media
