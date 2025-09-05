#include <ymir/media/loader/loader_mdf_mds.hpp>

#include <ymir/media/binary_reader/binary_reader_impl.hpp>
#include <ymir/media/frame_address.hpp>

#include <ymir/util/scope_guard.hpp>

#include <fmt/format.h>
#include <fmt/std.h>
#include <fmt/xchar.h>

#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace ymir::media::loader::mdfmds {

// Format parsing based on reverse-engineering work by Henrik Stokseth.

#pragma pack(push, 1)
struct MDSHeader {
    std::array<uint8, 16> signature; // 0x00  Signature, must be "MEDIA DESCRIPTOR"
    uint16 version;                  // 0x10  Version  (0x0301)
    uint16 mediumType;               // 0x12  Medium type
    uint16 numSessions;              // 0x14  Number of sessions
    uint8 _dummy16[4];               // 0x16  (unknown)
    uint16 bcaLength;                // 0x1A  Length of BCA data (DVD-ROM)
    uint8 _dummy1C[8];               // 0x1C  (unknown)
    uint32 bcaDataOffset;            // 0x26  Offset to BCA data (DVD-ROM)
    uint8 _dummy2A[24];              // 0x2A  (unknown)
    uint32 discStructuresOffset;     // 0x42  Offset to disc structures
    uint8 _dummy46[12];              // 0x46  (unknown)
    uint32 sessionDataOffset;        // 0x50  Offset to session blocks
    uint32 dpmDataOffset;            // 0x54  Offset to DPM data blocks
};
#pragma pack(pop)
static_assert(sizeof(MDSHeader) == 0x58);

#pragma pack(push, 1)
struct MDSSession {
    sint32 sessionStart;      // 0x00  Session's start address
    sint32 sessionEnd;        // 0x04  Session's end address
    uint16 sessionNumber;     // 0x08  Session number
    uint8 totalBlocks;        // 0x0A  Number of all data blocks
    uint8 leadinBlocks;       // 0x0B  Number of lead-in data blocks
    uint16 firstTrack;        // 0x0C  First track's number
    uint16 lastTrack;         // 0x0E  Last track's number
    uint32 _dummy10;          // 0x10  (unknown)
    uint32 trackBlocksOffset; // 0x14  Offset of lead-in and regular data blocks
};
#pragma pack(pop)
static_assert(sizeof(MDSSession) == 0x18);

#pragma pack(push, 1)
struct MDSTrack {
    uint8 mode;           // 0x00  Track mode
    uint8 subchannelMode; // 0x01  Subchannel mode
    uint8 controlADR;     // 0x02  Adr/Ctl
    uint8 _dummy03;       // 0x03  Track flags?
    uint8 trackNum;       // 0x04  Track number. (>0x99 is lead-in track)
    uint8 _dummy05[4];    // 0x05  (unknown)
    uint8 min;            // 0x09  Minute
    uint8 sec;            // 0x0A  Second
    uint8 frame;          // 0x0B  Frame
    uint32 extraOffset;   // 0x0C  Start offset of this track's extra block
    uint16 sectorSize;    // 0x10  Sector size
    uint8 _dummy12[18];   // 0x12  (unknown)
    uint32 startSector;   // 0x24  Track start sector (PLBA)
    uint64 startOffset;   // 0x28  Track start offset
    uint8 session;        // 0x30  Session or index?
    uint8 _dummy31[3];    // 0x31  (unknown)
    uint32 footerOffset;  // 0x34  Start offset of footer
    uint8 _dummy38[24];   // 0x38  (unknown)
};
#pragma pack(pop)
static_assert(sizeof(MDSTrack) == 0x50);

#pragma pack(push, 1)
struct MDSTrackExtra {
    uint32 pregap; // 0x00  Number of sectors in pregap
    uint32 length; // 0x00  Number of sectors in track
};
#pragma pack(pop)
static_assert(sizeof(MDSTrackExtra) == 0x8);

#pragma pack(push, 1)
struct MDSFooter {
    uint32 filenameOffset; // 0x00  Offset to filename
    uint32 charType;       // 0x04  Filename character type: 0=char, other values=wchar_t
    uint32 _dummy08;       // 0x08  (unused)
    uint32 _dummy0C;       // 0x0C  (unused)
};
#pragma pack(pop)
static_assert(sizeof(MDSFooter) == 0x10);

bool Load(std::filesystem::path mdsPath, Disc &disc, bool preloadToRAM) {
    std::ifstream in{mdsPath, std::ios::binary};

    util::ScopeGuard sgInvalidateDisc{[&] { disc.Invalidate(); }};

    if (!in) {
        // fmt::println("MDF/MDS: Could not load MDS file");
        return false;
    }

    auto read = [&](auto &value) {
        in.read(reinterpret_cast<char *>(&value), sizeof(value));
        if (!in) {
            std::error_code err{errno, std::generic_category()};
            // fmt::println("MDF/MDS: File could not be read: {}", err.message());
            return false;
        }
        if (in.gcount() < sizeof(value)) {
            // fmt::println("MDF/MDS: File truncated");
            return false;
        }
        return true;
    };

    // Read and validate header
    MDSHeader header{};
    if (!read(header)) {
        // fmt::println("MDF/MDS: Could not read MDS header");
        return false;
    }

    // Expect "MEDIA DESCRIPTOR" signature
    static constexpr std::array<uint8, 16> expected = {0x4D, 0x45, 0x44, 0x49, 0x41, 0x20, 0x44, 0x45,
                                                       0x53, 0x43, 0x52, 0x49, 0x50, 0x54, 0x4F, 0x52};
    if (header.signature != expected) {
        // fmt::println("MDF/MDS: Not a valid MDS file");
        return false;
    }

    // Expect version 1
    if (header.version != 0x0301) {
        // fmt::println("MDF/MDS: Unsupported MDS version {:04X}", header.version);
        return false;
    }

    // Get medium type, check that this is a CD image
    // Known medium types:
    //   00  CD-ROM
    //   01  CD-R
    //   02  CD-RW
    //   10  DVD-ROM
    //   12  DVD-R
    if (header.mediumType >= 0x10) {
        // fmt::println("MDF/MDS: MDS file describes a DVD image - not supported");
        return false;
    }

    // Setup variables to read data from the file
    MDSSession sessionData{};
    MDSTrack trackData{};
    MDSFooter footerData{};

    // Get session count
    // fmt::println("MDF/MDS: {} {}", header.numSessions, (header.numSessions > 1 ? "sessions" : "session"));

    // Housekeeping
    bool hasHeader = false;
    uint32 endFrameAddress = 0;
    std::array<uint32, 99> trackStartOffsets{};
    std::array<std::filesystem::path, 99> trackMDFs{};
    std::unordered_map<std::filesystem::path, std::shared_ptr<IBinaryReader>> files{};

    // Build sessions
    disc.sessions.clear();
    for (uint32 i = 0; i < header.numSessions; i++) {
        in.seekg(header.sessionDataOffset + i * sizeof(MDSSession), std::ios::beg);
        if (!read(sessionData)) {
            // fmt::println("MDF/MDS: Could not read session {}", i);
            return false;
        }

        // fmt::println("MDF/MDS: Session {} - tracks {} to {}", sessionData.sessionNumber, sessionData.firstTrack,
        //              sessionData.lastTrack);

        auto &session = disc.sessions.emplace_back();

        for (int j = 0; j < sessionData.totalBlocks; j++) {
            const uint32 trackBlockOffset = sessionData.trackBlocksOffset + j * sizeof(MDSTrack);
            in.seekg(trackBlockOffset, std::ios::beg);
            if (!read(trackData)) {
                // fmt::println("MDF/MDS: Could not read track {} in session {} at 0x{:X}", j,
                // sessionData.sessionNumber,
                //              trackBlockOffset);
                return false;
            }

            // Sanity check
            if (trackData.trackNum < 0xA0) {
                if (trackData.trackNum < sessionData.firstTrack || trackData.trackNum > sessionData.lastTrack) {
                    fmt::println("MDF/MDS: Track number {} out of range of session parameters ({} to {})",
                                 trackData.trackNum, sessionData.firstTrack, sessionData.lastTrack);
                    return false;
                }
            }

            const uint32 trackIndex = trackData.trackNum - 1;

            /*fmt::println(
                "MDF/MDS: Session {} track {:3d} - #{:<3d} mode={:02X} subch={:02X} CTL/ADR={:02X} "
                "{:02d}:{:02d}:{:02d} session={:d} sector={:4d}b:{:<6d} start={:<9d} extra={:<5d} footer={:<5d}",
                sessionData.sessionNumber, j, trackData.trackNum, trackData.mode, trackData.subchannelMode,
                trackData.controlADR, trackData.min, trackData.sec, trackData.frame, trackData.session,
                trackData.sectorSize, trackData.startSector, trackData.startOffset, trackData.extraOffset,
                trackData.footerOffset);*/

            if (trackData.trackNum <= 99) {
                auto &track = session.tracks[trackIndex];
                track.indices.emplace_back(); // Insert dummy index 00
                auto &index = track.indices.emplace_back();
                track.controlADR = (trackData.controlADR << 4u) | (trackData.controlADR >> 4u);
                if (track.controlADR == 0x01 && trackData.sectorSize != 2352) {
                    /*fmt::println("MDF/MDS: Session {} audio track {:3d} has invalid sector size: {}",
                                 sessionData.sessionNumber, trackData.trackNum, trackData.sectorSize);*/
                    return false;
                }
                track.SetSectorSize(trackData.sectorSize);

                track.startFrameAddress = trackData.startSector + 150;
                index.startFrameAddress = track.startFrameAddress;
                track.track01FrameAddress = track.startFrameAddress;
                track.interleavedSubchannel = trackData.subchannelMode != 0;

                // Complete previous track
                if (trackData.trackNum > sessionData.firstTrack) {
                    auto &prevTrack = session.tracks[trackIndex - 1];
                    if (prevTrack.endFrameAddress < prevTrack.startFrameAddress) {
                        prevTrack.endFrameAddress = track.startFrameAddress - 1;
                    }
                    auto &prevIndex = prevTrack.indices.back();
                    prevIndex.endFrameAddress = prevTrack.endFrameAddress;

                    const uintmax_t viewOffset = trackStartOffsets[trackIndex - 1];
                    const uintmax_t viewSize = trackData.startOffset - viewOffset;
                    const auto &mdfPath = trackMDFs[trackIndex - 1];

                    prevTrack.binaryReader =
                        std::make_unique<SharedSubviewBinaryReader>(files.at(mdfPath), viewOffset, viewSize);
                }

                if (trackData.footerOffset == 0) {
                    // fmt::println("MDF/MDS: No footer data found for track {} in session {} at 0x{:X}", j,
                    //              sessionData.sessionNumber, trackData.footerOffset);
                    return false;
                }

                // Get data file name
                in.seekg(trackData.footerOffset);
                if (!read(footerData)) {
                    // fmt::println("MDF/MDS: Could not read footer data from track {} in session {}", j,
                    //              sessionData.sessionNumber);
                    return false;
                }

                // Convert from wchar/char to std::filesystem::path
                std::filesystem::path mdfPath{};
                if (footerData.charType) {
                    std::wifstream win{mdsPath, std::ios::binary};
                    std::wstring filename{};
                    win.seekg(footerData.filenameOffset);
                    win >> filename;
                    mdfPath = filename;
                } else {
                    std::string filename{};
                    in.seekg(footerData.filenameOffset);
                    in >> filename;
                    mdfPath = filename;
                }
                if (mdfPath.string().starts_with("*.")) {
                    mdfPath = mdsPath;
                    mdfPath.replace_extension("mdf");
                }
                // fmt::println("MDF/MDS: MDF path: {}", mdfPath);

                if (!files.contains(mdfPath)) {
                    std::error_code err{};
                    if (preloadToRAM) {
                        files.insert({mdfPath, std::make_shared<MemoryBinaryReader>(mdfPath, err)});
                    } else {
                        files.insert({mdfPath, std::make_shared<MemoryMappedBinaryReader>(mdfPath, err)});
                    }
                    if (err) {
                        // fmt::println("MDF/MDS: Failed to load MDF file {} - {}", mdfPath, err.message());
                        return false;
                    }

                    // Try to read the header
                    if (!hasHeader) {
                        auto &reader = files.at(mdfPath);

                        // Check for sync bytes
                        static constexpr std::array<uint8, 12> kSyncBytes = {
                            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
                        };
                        static constexpr std::array<uint8, 12> kSegaBytes = {
                            0x53, 0x45, 0x47, 0x41, 0x20, 0x53, 0x45, 0x47, 0x41, 0x53, 0x41, 0x54,
                        };

                        uintmax_t offset = 0;
                        std::array<uint8, 12> prefix{};

                        reader->Read(0, 12, prefix);
                        if (prefix == kSyncBytes) {
                            offset = 16;
                        } else if (prefix == kSegaBytes) {
                            offset = 0;
                        } else if (std::equal(prefix.begin() + 4, prefix.end(), kSegaBytes.begin())) {
                            offset = 4;
                        }

                        std::array<uint8, 256> header{};
                        const uintmax_t readSize = reader->Read(offset, 256, header);
                        if (readSize < 256) {
                            // fmt::println("MDF/MDS: Image file truncated");
                            return false;
                        }

                        hasHeader = disc.header.ReadFrom(header);
                    }
                }

                trackStartOffsets[trackIndex] = trackData.startOffset;
                trackMDFs[trackIndex] = mdfPath;
            } else if (trackData.trackNum == 0xA2) {
                endFrameAddress = TimestampToFrameAddress(trackData.min, trackData.sec, trackData.frame);
            }
        }

        // Complete last track
        auto &lastTrack = session.tracks[sessionData.lastTrack - 1];
        if (lastTrack.endFrameAddress < lastTrack.startFrameAddress) {
            lastTrack.endFrameAddress = endFrameAddress;

            const auto &mdfPath = trackMDFs[sessionData.lastTrack - 1];
            auto &file = files.at(mdfPath);
            const uintmax_t viewOffset = trackStartOffsets[sessionData.lastTrack - 1];
            const uintmax_t viewSize = file->Size() - viewOffset;

            lastTrack.binaryReader = std::make_unique<SharedSubviewBinaryReader>(file, viewOffset, viewSize);
        }
        auto &lastIndex = lastTrack.indices.back();
        lastIndex.endFrameAddress = lastTrack.endFrameAddress;

        // Finish session
        session.numTracks = sessionData.lastTrack - sessionData.firstTrack + 1;
        session.firstTrackIndex = sessionData.firstTrack - 1;
        session.lastTrackIndex = sessionData.lastTrack - 1;
        session.startFrameAddress = sessionData.sessionStart + 150;
        session.endFrameAddress = endFrameAddress;
        session.BuildTOC();
    }

    sgInvalidateDisc.Cancel();

    return true;
}

} // namespace ymir::media::loader::mdfmds
