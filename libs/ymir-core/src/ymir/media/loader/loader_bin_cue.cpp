#include <ymir/media/loader/loader_bin_cue.hpp>

#include <ymir/media/binary_reader/binary_reader_impl.hpp>
#include <ymir/media/frame_address.hpp>

#include <ymir/util/scope_guard.hpp>

#include <fmt/format.h>
#include <fmt/std.h>

#include <fstream>
#include <set>
#include <sstream>

namespace ymir::media::loader::bincue {

const std::set<std::string> kValidCueKeywords = {
    // General commands
    "CATALOG", "CD_DA", "CD_ROM", "CD_ROM_XA", "CDTEXTFILE", "FILE", "REM", "TRACK",
    // CD-Text commands
    "ARRANGER", "COMPOSER", "DISC_ID", "GENRE", "ISRC", "MESSAGE", "PERFORMER", "SIZE_INFO", "SONGWRITER", "TITLE",
    "TOC_INFO1", "TOC_INFO2", "UPC_EAN",
    // Track commands
    "COPY", "DATAFILE", "FLAGS", "FIFO", "FOUR_CHANNEL_AUDIO", "INDEX", "POSTGAP", "PREGAP", "PRE_EMPHASIS", "SILENCE",
    "START", "TWO_CHANNEL_AUDIO", "ZERO",
    "NO" // NO COPY, NO PRE_EMPHASIS
};

const std::set<std::string> kValidCueNOKeywords = {"COPY", "PRE_EMPHASIS"};

namespace fs = std::filesystem;

bool Load(std::filesystem::path cuePath, Disc &disc, bool preloadToRAM) {
    std::ifstream in{cuePath, std::ios::binary};

    util::ScopeGuard sgInvalidateDisc{[&] { disc.Invalidate(); }};

    if (!in) {
        // fmt::println("BIN/CUE: Could not load CUE file");
        return false;
    }

    // Peek first line to check if this is really a CUE file
    // uint32 lineNum = 1;
    std::string line{};
    if (!std::getline(in, line)) {
        // fmt::println("BIN/CUE: Could not read file");
        return false;
    }

    {
        std::istringstream ins{line};
        std::string keyword{};
        ins >> keyword;
        if (!kValidCueKeywords.contains(keyword)) {
            // fmt::println("BIN/CUE: Not a valid CUE file");
            return false;
        }
        if (keyword == "NO") {
            // NO must be followed by COPY or PRE_EMPHASIS
            ins >> keyword;
            if (!kValidCueNOKeywords.contains(keyword)) {
                // fmt::println("BIN/CUE: Not a valid CUE file");
                return false;
            }
        }
    }

    // BIN/CUE files have only one session
    disc.sessions.clear();
    auto &session = disc.sessions.emplace_back();
    session.startFrameAddress = 0;

    // auto cueFileSize = fs::file_size(cuePath);
    // fmt::println("BIN/CUE: File size: {} bytes", cueFileSize);

    fs::path curFile{};

    uint32 nextTrackNum = 0;
    uint32 frameAddress = 150; // start with lead-in
    uint32 prevTrackIndex = ~0;
    uint32 currTrackIndex = ~0;
    uint32 currFileIndex = 0;
    uintmax_t binFileOffset = 0;
    uintmax_t binFileSize = 0;
    uint32 prevM = 0;
    uint32 prevS = 0;
    uint32 prevF = 0;
    uint32 pregapM = 0;
    uint32 pregapS = 0;
    uint32 pregapF = 0;
    uint32 pregapLength = 0;
    std::array<uintmax_t, 99> trackFileOffsets{};
    std::array<uint32, 99> trackFileIndices{};

    // File format validation flags
    bool hasFile = false;
    bool hasTrack = false;
    bool hasIndex = false;
    bool hasIndex0 = false;
    bool hasPregap = false;
    bool hasPostgap = false;

    std::shared_ptr<IBinaryReader> binaryReader{};

    do {
        std::istringstream ins{line};
        std::string keyword{};
        ins >> keyword;

        // Skip blank lines
        if (keyword.empty()) {
            continue;
        }

        if (!kValidCueKeywords.contains(keyword)) {
            // fmt::println("BIN/CUE: Found invalid keyword {} (line {})", keyword, lineNum);
            return false;
        }
        if (keyword == "NO") {
            // NO must be followed by COPY or PRE_EMPHASIS
            ins >> keyword;
            if (!kValidCueNOKeywords.contains(keyword)) {
                // fmt::println("BIN/CUE: Found invalid keyword NO {} (line {})", keyword, lineNum);
                return false;
            }
            keyword = "NO " + keyword;
        }

        if (keyword == "FILE") {
            // FILE [filename] [format]
            std::string filename = line.substr(line.find("FILE") + 5);
            auto pos = filename.find_last_of(' ') + 1;
            if (pos == std::string::npos) {
                // fmt::println("BIN/CUE: Invalid FILE entry: {} (line {})", line, lineNum);
                return false;
            }

            filename = filename.substr(0, pos - 1);
            if (filename.starts_with('\"')) {
                filename = filename.substr(1);
            }
            if (filename.ends_with('\"')) {
                filename = filename.substr(0, filename.size() - 1);
            }

            std::u8string u8filename{filename.begin(), filename.end()};

            fs::path filePath = u8filename;
            fs::path binPath;
            if (filePath.is_absolute()) {
                binPath = cuePath.parent_path() / filePath.filename();
            } else {
                binPath = cuePath.parent_path() / filePath;
            }
            if (!fs::is_regular_file(binPath)) {
                // fmt::println("BIN/CUE: File not found: {} (line {})", binPath, lineNum);
                return false;
            }

            // Close current track
            if (hasTrack) {
                auto &track = session.tracks[currTrackIndex];

                const uintmax_t length = binFileSize - binFileOffset;
                const uint32 frames = length / track.sectorSize;
                track.endFrameAddress = track.startFrameAddress + frames - 1;
                track.binaryReader =
                    std::make_unique<SharedSubviewBinaryReader>(binaryReader, trackFileOffsets[currTrackIndex], length);
                frameAddress += frames;
            }

            // Reset pointer and load new file
            std::error_code err{};
            if (preloadToRAM) {
                binaryReader = std::make_shared<MemoryBinaryReader>(binPath, err);
            } else {
                binaryReader = std::make_shared<MemoryMappedBinaryReader>(binPath, err);
            }
            if (err) {
                // fmt::println("BIN/CUE: Failed to load file - {} (line {})", err.message(), lineNum);
                return false;
            }

            binFileOffset = 0;
            binFileSize = binaryReader->Size();
            prevM = 0;
            prevS = 0;
            prevF = 0;

            // fmt::println("BIN/CUE: File {} - {} bytes", filename, binFileSize);

            hasTrack = false;
            hasFile = true;
            currFileIndex++;
        } else if (keyword == "TRACK") {
            // TRACK [number] [datatype]
            if (!hasFile) {
                // fmt::println("BIN/CUE: Found TRACK without a FILE (line {})", lineNum);
                return false;
            }

            bool mode2 = false;

            uint32 trackNum{};
            std::string format{};
            ins >> trackNum >> format;

            if (nextTrackNum == 0) {
                nextTrackNum = trackNum + 1;
                session.firstTrackIndex = trackNum - 1;
            } else if (trackNum < nextTrackNum) {
                // fmt::println("BIN/CUE: Unexpected track order: expected {} but found {} (line {})", nextTrackNum,
                //              trackNum, lineNum);
                return false;
            }
            session.lastTrackIndex = trackNum - 1;

            // fmt::println("BIN/CUE:   Track {:02d} - {}", trackNum, format);
            uint32 sectorSize{};
            uint32 controlADR{};
            if (format.starts_with("MODE")) {
                // Data track
                if (format.ends_with("_RAW")) {
                    // MODE1_RAW and MODE2_RAW
                    sectorSize = 2352;
                } else {
                    // Known modes:
                    // MODE1/2048   MODE2/2048
                    //              MODE2/2324
                    //              MODE2/2336
                    // MODE1/2352   MODE2/2352
                    sectorSize = std::stoi(format.substr(6));
                }
                mode2 = format.starts_with("MODE2");
                controlADR = 0x41;
            } else if (format == "CDG") {
                // Karaoke CD+G track
                sectorSize = 2448;
                // TODO: control/ADR?
            } else if (format == "AUDIO") {
                // Audio track
                sectorSize = 2352;
                controlADR = 0x01;
            } else {
                // fmt::println("BIN/CUE: Unsupported track format (line {})", lineNum);
                return false;
            }
            // fmt::println("BIN/CUE:   Sector size: {} bytes", sectorSize);
            // fmt::println("BIN/CUE:   Control/ADR: {:02X}", controlADR);

            prevTrackIndex = currTrackIndex;
            currTrackIndex = trackNum - 1;

            auto &track = session.tracks[currTrackIndex];
            track.mode2 = mode2;
            track.SetSectorSize(sectorSize);
            track.controlADR = controlADR;
            track.interleavedSubchannel = false;

            session.numTracks++;
            if (session.numTracks > 99) {
                // fmt::println("BIN/CUE: Too many tracks");
                return false;
            }

            // If this is the first track in the first file, read the header
            if (currFileIndex == 1 && session.numTracks == 1) {
                // Skip sync bytes and/or header if present
                const uintmax_t userDataOffset = sectorSize == 2352 ? 16 : sectorSize == 2340 ? 4 : 0;

                std::array<uint8, 256> header{};
                const uintmax_t readSize = binaryReader->Read(userDataOffset, 256, header);
                if (readSize < 256) {
                    // fmt::println("BIN/CUE: File truncated");
                    return false;
                }

                disc.header.ReadFrom(header);
            }

            hasTrack = true;
            hasIndex = false;
            hasIndex0 = false;
            hasPregap = false;
            hasPostgap = false;
            pregapLength = 0;
            trackFileIndices[currTrackIndex] = currFileIndex;
        } else if (keyword == "INDEX") {
            // INDEX [number] [mm:ss:ff]
            if (hasPostgap) {
                // fmt::println("BIN/CUE: Found INDEX after POSTGAP in a TRACK (line {})", lineNum);
                return false;
            }

            uint32 indexNum{};
            std::string msf{};
            ins >> indexNum >> msf;

            uint32 m = std::stoi(msf.substr(0, 2));
            uint32 s = std::stoi(msf.substr(3, 5));
            uint32 f = std::stoi(msf.substr(6, 8));
            // fmt::println("BIN/CUE:     Index {:d} - {:02d}:{:02d}:{:02d}", indexNum, m, s, f);

            if (!hasTrack) {
                // fmt::println("BIN/CUE: Found INDEX before TRACK (line {})", lineNum);
                return false;
            }
            auto &track = session.tracks[currTrackIndex];

            if (prevTrackIndex != ~0 && trackFileIndices[currTrackIndex] == trackFileIndices[prevTrackIndex]) {
                auto &prevTrack = session.tracks[prevTrackIndex];
                binFileOffset += (TimestampToFrameAddress(m, s, f) - TimestampToFrameAddress(prevM, prevS, prevF)) *
                                 prevTrack.sectorSize;
            }

            // Process indices
            if (indexNum == 0) {
                if (hasPregap) {
                    // fmt::println("BIN/CUE: Found INDEX 00 and PREGAP in the same TRACK (line {})", lineNum);
                    // return false;
                } else {
                    hasPregap = true;
                    hasIndex0 = true;
                }
            } else if (indexNum == 1) {
                // Close previous track
                if (prevTrackIndex != ~0) {
                    auto &prevTrack = session.tracks[prevTrackIndex];
                    const uintmax_t binFileLength = binFileOffset - trackFileOffsets[prevTrackIndex];
                    if (prevTrack.endFrameAddress < prevTrack.startFrameAddress) {
                        const uint32 frames = binFileLength / prevTrack.sectorSize;
                        prevTrack.endFrameAddress = prevTrack.startFrameAddress + frames - 1;
                        prevTrack.binaryReader = std::make_unique<SharedSubviewBinaryReader>(
                            binaryReader, trackFileOffsets[prevTrackIndex], binFileLength);
                        frameAddress += frames;
                    }

                    // Finish last index of previous track
                    prevTrack.indices.back().endFrameAddress = track.startFrameAddress + frameAddress - 1;
                }

                // Skip pregap in audio tracks or data tracks not starting at the beginning of a file
                if (hasPregap) {
                    const uintmax_t pregapEnd = TimestampToFrameAddress(m, s, f);
                    const uintmax_t pregapStart = hasIndex0
                                                      ? TimestampToFrameAddress(prevM, prevS, prevF)
                                                      : pregapEnd - TimestampToFrameAddress(pregapM, pregapS, pregapF);
                    const uintmax_t delta = pregapEnd - pregapStart;
                    if (track.controlADR == 0x01 || binFileOffset > 0) {
                        // fmt::println("BIN/CUE: Track {} has pregap of {} frames; skipping", currTrackIndex, delta);
                        binFileOffset += delta * track.sectorSize;
                    }
                    pregapLength = delta;
                }

                // Start new track
                track.startFrameAddress = frameAddress;
                trackFileOffsets[currTrackIndex] = binFileOffset;

                // Start index
                assert(track.indices.empty());
                auto &index = track.indices.emplace_back();
                index.startFrameAddress = frameAddress;

                // fmt::println("BIN/CUE: Track {} offset = {:X}", currTrackIndex, trackFileOffsets[currTrackIndex]);
            } else {
                // Indices must be sequential
                if (indexNum != track.indices.size() + 1) {
                    // fmt::println("BIN/CUE: Unexpected index {} on track {}", indexNum, currTrackIndex);
                    return false;
                }
                const uint32 indexFrameAddress = TimestampToFrameAddress(m, s, f) - pregapLength;

                // Finish previous index
                track.indices.back().endFrameAddress = track.startFrameAddress + indexFrameAddress - 1;

                // Add new index to current track
                auto &index = track.indices.emplace_back();
                index.startFrameAddress = track.startFrameAddress + indexFrameAddress;
            }

            prevM = m;
            prevS = s;
            prevF = f;
            hasIndex = true;
        } else if (keyword == "PREGAP") {
            // PREGAP [mm:ss:ff]
            if (hasIndex) {
                // fmt::println("BIN/CUE: Found PREGAP after INDEX (line {})", lineNum);
                return false;
            }
            if (hasPregap) {
                // fmt::println("BIN/CUE: Found multiple PREGAPS in a TRACK (line {})", lineNum);
                return false;
            }

            std::string msf{};
            ins >> msf;

            const uint32 m = std::stoi(msf.substr(0, 2));
            const uint32 s = std::stoi(msf.substr(3, 5));
            const uint32 f = std::stoi(msf.substr(6, 8));
            // fmt::println("BIN/CUE:     Pregap - {:02d}:{:02d}:{:02d}", m, s, f);

            hasPregap = true;
            pregapM = m;
            pregapS = s;
            pregapF = f;
            pregapLength = TimestampToFrameAddress(m, s, f);
        } else if (keyword == "POSTGAP") {
            // POSTGAP [mm:ss:ff]
            if (!hasIndex) {
                // fmt::println("BIN/CUE: Found POSTGAP before INDEX (line {})", lineNum);
                return false;
            }
            if (hasPostgap) {
                // fmt::println("BIN/CUE: Found multiple POSTGAPS in a TRACK (line {})", lineNum);
                return false;
            }

            std::string msf{};
            ins >> msf;
            // uint32 m = std::stoi(msf.substr(0, 2));
            // uint32 s = std::stoi(msf.substr(3, 5));
            // uint32 f = std::stoi(msf.substr(6, 8));
            //  fmt::println("BIN/CUE:     Postgap - {:02d}:{:02d}:{:02d}", m, s, f);

            hasPostgap = true;
        } else {
            // fmt::println("BIN/CUE: Skipping {}", keyword);
        }

        // lineNum++;
    } while (std::getline(in, line));

    // Sanity checks
    if (!hasFile) {
        // fmt::println("BIN/CUE: No FILE specified");
        return false;
    }

    // Finish last track
    if (hasTrack) {
        auto &track = session.tracks[currTrackIndex];

        const uintmax_t length = binFileSize - binFileOffset;
        const uint32 frames = length / track.sectorSize;
        track.endFrameAddress = track.startFrameAddress + frames - 1;
        track.binaryReader =
            std::make_unique<SharedSubviewBinaryReader>(binaryReader, trackFileOffsets[currTrackIndex], length);
        frameAddress += frames;

        // Finish last index
        track.indices.back().endFrameAddress = frameAddress - 1;
    }

    // Finish session
    session.endFrameAddress = frameAddress - 1;
    session.BuildTOC();

    sgInvalidateDisc.Cancel();

    return true;
}

} // namespace ymir::media::loader::bincue
