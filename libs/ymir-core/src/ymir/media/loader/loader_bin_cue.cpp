#include <ymir/media/loader/loader_bin_cue.hpp>

#include <ymir/media/binary_reader/binary_reader_impl.hpp>
#include <ymir/media/frame_address.hpp>

#include <ymir/util/scope_guard.hpp>

#include <fmt/format.h>
#include <fmt/std.h>

#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

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

// Index specification.
// INDEX <number> <pos>
// <number> is the index number, from 0 to 99
// <pos> is the position in MM:SS:FF format relative to the start of the current file.
// INDEX 00 specifies a pregap with data from the file.
// INDEX 01 is the starting point of the track.
struct CueIndex {
    uint32 number;
    uint32 pos; // in frame address, relative to the start of the file
};

// Track specification.
// TRACK <number> <format>
// <number> is the track number, from 1 to 99
// <format> is the track format, one of many options, including:
// - MODE1_RAW
// - MODE1/2048
// - MODE1/2352
// - MODE2_RAW
// - MODE2/2048
// - MODE2/2324
// - MODE2/2336
// - MODE2/2352
// - AUDIO
// - CDG
struct CueTrack {
    uint32 fileIndex;
    uint32 number;
    std::string format;
    uint32 pregap;  // number of pregap sectors from PREGAP command; generates silence
    uint32 postgap; // number of postgap sectors from POSTGAP command; generates silence
    std::vector<CueIndex> indexes;
};

// File reference.
//
// FILE <path> [<format>]
// <path> can be absolute or relative
// [<format>] can be:
// - BINARY: raw binary data - for data and audio tracks; default if omitted
// - WAVE: audio track in .WAV file - not supported
// - AIFF: audio track in .AIFF file - not supported
// - MP3: audio track in .MP3 file - not supported
// - many others, none of which are supported
struct CueFile {
    std::filesystem::path path;
    uintmax_t size;
    std::string format;
};

// Representation of the CUE sheet - a set of FILEs, each with TRACKs containing INDEXes and additional parameters.
struct CueSheet {
    std::vector<CueFile> files;
    std::vector<CueTrack> tracks;
};

static std::optional<CueSheet> LoadSheet(std::filesystem::path cuePath) {
    std::ifstream in{cuePath, std::ios::binary};

    if (!in) {
        // fmt::println("BIN/CUE: Could not load CUE file");
        return std::nullopt;
    }

    // Peek first non-blank line to check if this is really a CUE file
    std::string line{};
    while (true) {
        if (!std::getline(in, line)) {
            // fmt::println("BIN/CUE: Could not read file");
            return std::nullopt;
        }
        if (!line.empty()) {
            std::istringstream ins{line};
            std::string keyword{};
            ins >> keyword;
            if (!kValidCueKeywords.contains(keyword)) {
                // fmt::println("BIN/CUE: Not a valid CUE file");
                return std::nullopt;
            }
            if (keyword == "NO") {
                // NO must be followed by COPY or PRE_EMPHASIS
                ins >> keyword;
                if (!kValidCueNOKeywords.contains(keyword)) {
                    // fmt::println("BIN/CUE: Not a valid CUE file");
                    return std::nullopt;
                }
            }
            break;
        }
    }

    CueSheet sheet{};

    // Sanity check variables
    uint32 nextTrackNum = 0;
    uint32 numTracks = 0;
    bool hasPregap = false;
    bool hasPostgap = false;

    // uint32 lineNum = 1;

    do {
        std::istringstream ins{line};
        std::string keyword{};
        ins >> keyword;

        // fmt::println("BIN/CUE: [Line {}] {}", lineNum, line);

        // Skip blank lines
        if (keyword.empty()) {
            continue;
        }

        if (!kValidCueKeywords.contains(keyword)) {
            // fmt::println("BIN/CUE: Found invalid keyword {} (line {})", keyword, lineNum);
            return std::nullopt;
        }
        if (keyword == "NO") {
            // NO must be followed by COPY or PRE_EMPHASIS
            ins >> keyword;
            if (!kValidCueNOKeywords.contains(keyword)) {
                // fmt::println("BIN/CUE: Found invalid keyword NO {} (line {})", keyword, lineNum);
                return std::nullopt;
            }
            keyword = "NO " + keyword;
        }

        if (keyword == "FILE") {
            // FILE [filename] [format]
            std::string params = line.substr(line.find("FILE") + 5);
            auto pos = params.find_last_of(' ') + 1;
            if (pos == std::string::npos) {
                // fmt::println("BIN/CUE: Invalid FILE entry: {} (line {})", line, lineNum);
                return std::nullopt;
            }
            std::string format = params.substr(pos);
            if (format.ends_with('\r')) {
                format.resize(format.size() - 1);
            }

            std::string filename = params.substr(0, pos - 1);
            if (filename.starts_with('\"')) {
                filename = filename.substr(1);
            }
            if (filename.ends_with('\"')) {
                filename = filename.substr(0, filename.size() - 1);
            }

            std::u8string u8filename{filename.begin(), filename.end()};

            std::filesystem::path filePath = u8filename;
            std::filesystem::path binPath;
            if (filePath.is_absolute()) {
                binPath = cuePath.parent_path() / filePath.filename();
            } else {
                binPath = cuePath.parent_path() / filePath;
            }
            if (!std::filesystem::is_regular_file(binPath)) {
                // fmt::println("BIN/CUE: File not found: {} (line {})", binPath, lineNum);
                return std::nullopt;
            }
            const uintmax_t size = std::filesystem::file_size(binPath);

            // fmt::println("BIN/CUE: File {} - {} bytes", filename, size);

            sheet.files.push_back({.path = binPath, .size = size, .format = format});
        } else if (keyword == "TRACK") {
            // TRACK [number] [format]
            if (sheet.files.empty()) {
                // fmt::println("BIN/CUE: Found TRACK without a FILE (line {})", lineNum);
                return std::nullopt;
            }

            ++numTracks;
            if (numTracks > 99) {
                // fmt::println("BIN/CUE: Too many tracks");
                return std::nullopt;
            }

            auto &track = sheet.tracks.emplace_back();
            track.fileIndex = sheet.files.size() - 1;

            ins >> track.number >> track.format;

            if (nextTrackNum == 0) {
                nextTrackNum = track.number + 1;
            } else if (track.number < nextTrackNum) {
                // fmt::println("BIN/CUE: Unexpected track order: expected {} but found {} (line {})", nextTrackNum,
                //              track.number, lineNum);
                return std::nullopt;
            }

            // fmt::println("BIN/CUE:   Track {:02d} - {}", track.number, track.format);
            if (!track.format.starts_with("MODE") && track.format != "CDG" && track.format != "AUDIO") {
                // fmt::println("BIN/CUE: Unsupported track format (line {})", lineNum);
                return std::nullopt;
            }

            hasPregap = false;
            hasPostgap = false;
        } else if (keyword == "INDEX") {
            // INDEX [number] [mm:ss:ff]
            if (hasPostgap) {
                // fmt::println("BIN/CUE: Found INDEX after POSTGAP in a TRACK (line {})", lineNum);
                return std::nullopt;
            }

            if (sheet.tracks.empty()) {
                // fmt::println("BIN/CUE: Found INDEX without a TRACK (line {})", lineNum);
                return std::nullopt;
            }
            auto &track = sheet.tracks.back();
            auto &index = track.indexes.emplace_back();

            std::string msf{};
            ins >> index.number >> msf;

            uint32 m = std::stoi(msf.substr(0, 2));
            uint32 s = std::stoi(msf.substr(3, 5));
            uint32 f = std::stoi(msf.substr(6, 8));
            // fmt::println("BIN/CUE:     Index {:d} - {:02d}:{:02d}:{:02d}", index.number, m, s, f);
            index.pos = TimestampToFrameAddress(m, s, f);
        } else if (keyword == "PREGAP") {
            // PREGAP [mm:ss:ff]
            if (sheet.tracks.empty()) {
                // fmt::println("BIN/CUE: Found PREGAP without TRACK (line {})", lineNum);
                return std::nullopt;
            }

            auto &track = sheet.tracks.back();
            if (!track.indexes.empty()) {
                // fmt::println("BIN/CUE: Found PREGAP after INDEX (line {})", lineNum);
                return std::nullopt;
            }
            if (hasPregap) {
                // fmt::println("BIN/CUE: Found multiple PREGAPS in a TRACK (line {})", lineNum);
                return std::nullopt;
            }

            std::string msf{};
            ins >> msf;

            const uint32 m = std::stoi(msf.substr(0, 2));
            const uint32 s = std::stoi(msf.substr(3, 5));
            const uint32 f = std::stoi(msf.substr(6, 8));
            // fmt::println("BIN/CUE:     Pregap - {:02d}:{:02d}:{:02d}", m, s, f);

            track.pregap = TimestampToFrameAddress(m, s, f);

            hasPregap = true;
        } else if (keyword == "POSTGAP") {
            // POSTGAP [mm:ss:ff]
            if (sheet.tracks.empty()) {
                // fmt::println("BIN/CUE: Found POSTGAP without TRACK (line {})", lineNum);
                return std::nullopt;
            }

            auto &track = sheet.tracks.back();
            if (!track.indexes.empty()) {
                // fmt::println("BIN/CUE: Found POSTGAP without INDEX (line {})", lineNum);
                return std::nullopt;
            }
            if (hasPostgap) {
                // fmt::println("BIN/CUE: Found multiple POSTGAPS in a TRACK (line {})", lineNum);
                return std::nullopt;
            }

            std::string msf{};
            ins >> msf;
            uint32 m = std::stoi(msf.substr(0, 2));
            uint32 s = std::stoi(msf.substr(3, 5));
            uint32 f = std::stoi(msf.substr(6, 8));
            // fmt::println("BIN/CUE:     Postgap - {:02d}:{:02d}:{:02d}", m, s, f);

            track.postgap = TimestampToFrameAddress(m, s, f);

            hasPostgap = true;
        } else {
            // fmt::println("BIN/CUE: Skipping {}", keyword);
        }

        // lineNum++;
    } while (std::getline(in, line));

    // Sanity checks
    if (sheet.files.empty()) {
        // fmt::println("BIN/CUE: No FILE specified");
        return std::nullopt;
    }

    return sheet;
}

bool Load(std::filesystem::path cuePath, Disc &disc, bool preloadToRAM) {
    util::ScopeGuard sgInvalidateDisc{[&] { disc.Invalidate(); }};

    if (auto optSheet = LoadSheet(cuePath)) {
        CueSheet &sheet = *optSheet;

        // Build binary reader
        // - use file reader directly if there's only one file in the sheet
        // - use composite reader if there are multiple files
        std::shared_ptr<IBinaryReader> reader;
        if (sheet.files.size() == 1) {
            auto &file = sheet.files.front();
            std::error_code err{};
            if (preloadToRAM) {
                reader = std::make_shared<MemoryBinaryReader>(file.path, err);
            } else {
                reader = std::make_shared<MemoryMappedBinaryReader>(file.path, err);
            }
            if (err) {
                // fmt::println("BIN/CUE: Failed to load file - {}", err.message());
                return false;
            }
        } else {
            auto compReader = std::make_shared<CompositeBinaryReader>();
            for (auto &file : sheet.files) {
                std::shared_ptr<IBinaryReader> fileReader;
                std::error_code err{};
                if (preloadToRAM) {
                    fileReader = std::make_shared<MemoryBinaryReader>(file.path, err);
                } else {
                    fileReader = std::make_shared<MemoryMappedBinaryReader>(file.path, err);
                }
                if (err) {
                    // fmt::println("BIN/CUE: Failed to load file - {}", err.message());
                    return false;
                }
                compReader->Append(fileReader);
            }
            reader = compReader;
        }

        // BIN/CUE images have only one session
        disc.sessions.clear();
        auto &session = disc.sessions.emplace_back();
        session.startFrameAddress = 0;

        uint32 frameAddress = 150;         // Current (absolute) frame address
        uint32 accumPrePostGaps = 0;       // Accumulated pre/postgaps
        uint32 currFileFrameAddress = 150; // Starting frame address of the current file
        uintmax_t binOffset = 0;           // Current byte offset into binary data
        uintmax_t currFileBinOffset = 0;   // Byte offset into binary data of the current file

        auto closeTrack = [&](uint32 sheetTrackIndex) {
            CueTrack *sheetTrack = sheetTrackIndex < sheet.tracks.size() ? &sheet.tracks[sheetTrackIndex] : nullptr;
            auto &prevSheetTrack = sheet.tracks[sheetTrackIndex - 1];
            auto &prevTrack = session.tracks[prevSheetTrack.number - 1];

            uint32 trackSectors;
            const bool switchedToNewFile = sheetTrack == nullptr || sheetTrack->fileIndex != prevSheetTrack.fileIndex;
            if (switchedToNewFile) {
                // Changed to a new file or reached last track
                auto &file = sheet.files[prevSheetTrack.fileIndex];
                const uintmax_t sectorBytes = file.size + currFileBinOffset - binOffset;
                trackSectors = sectorBytes / prevTrack.sectorSize;
            } else {
                // Continuing in the same file
                trackSectors = sheetTrack->indexes[0].pos - prevTrack.startFrameAddress + 150 + accumPrePostGaps;
            }

            const uintmax_t trackSizeBytes = static_cast<uintmax_t>(trackSectors) * prevTrack.sectorSize;
            prevTrack.endFrameAddress = prevTrack.startFrameAddress + trackSectors - 1;
            prevTrack.binaryReader = std::make_unique<SharedSubviewBinaryReader>(reader, binOffset, trackSizeBytes);

            // TODO: for data tracks with at least the header bytes available, manually scan sectors to find the
            // *actual* end of the track, because some dumps are just bad

            frameAddress += trackSectors;
            binOffset += trackSizeBytes;
            if (switchedToNewFile) {
                currFileFrameAddress = frameAddress;
                currFileBinOffset = binOffset;
            }

            // Close last index
            assert(!prevTrack.indices.empty());
            auto &lastIndex = prevTrack.indices.back();
            lastIndex.endFrameAddress = frameAddress - 1;
        };

        // Process sheet
        for (size_t i = 0; i < sheet.tracks.size(); ++i) {
            auto &sheetTrack = sheet.tracks[i];
            auto &track = session.tracks[sheetTrack.number - 1];
            if (i == 0) {
                session.firstTrackIndex = sheetTrack.number - 1;
            } else {
                // Close previous track
                closeTrack(i);
            }
            session.lastTrackIndex = sheetTrack.number - 1;
            ++session.numTracks;

            if (sheetTrack.format.starts_with("MODE")) {
                // Data track
                if (sheetTrack.format.ends_with("_RAW")) {
                    // MODE1_RAW and MODE2_RAW
                    track.SetSectorSize(2352);
                } else {
                    // Known modes:
                    // MODE1/2048   MODE2/2048
                    //              MODE2/2324
                    //              MODE2/2336
                    // MODE1/2352   MODE2/2352
                    track.SetSectorSize(std::stoi(sheetTrack.format.substr(6)));
                }
                track.mode2 = sheetTrack.format.starts_with("MODE2");
                track.controlADR = 0x41;
            } else if (sheetTrack.format == "CDG") {
                // Karaoke CD+G track
                track.SetSectorSize(2448);
                track.controlADR = 0x41; // TODO: check this, might have to be 0x61 instead
            } else if (sheetTrack.format == "AUDIO") {
                // Audio track
                track.SetSectorSize(2352);
                track.controlADR = 0x01;
            } else {
                // fmt::println("BIN/CUE: Unsupported track format: {}", sheetTrack.format);
                return false;
            }

            track.startFrameAddress = frameAddress;

            accumPrePostGaps += sheetTrack.pregap + sheetTrack.postgap;

            assert(!sheetTrack.indexes.empty());

            uint32 indexOffset = 0;
            if (sheetTrack.indexes.front().number != 0) {
                track.indices.emplace_back();
                indexOffset = 1;
            }
            for (uint32 j = 0; j < sheetTrack.indexes.size(); ++j) {
                auto &sheetIndex = sheetTrack.indexes[j];
                auto &index = track.indices.emplace_back();
                index.startFrameAddress = sheetIndex.pos + currFileFrameAddress + accumPrePostGaps;
                if (j > 0) {
                    // Close previous index
                    auto &prevIndex = track.indices[j - 1 + indexOffset];
                    prevIndex.endFrameAddress = index.startFrameAddress - 1;
                }
                if (sheetIndex.number == 1) {
                    track.track01FrameAddress = index.startFrameAddress;
                }
            }
        }

        // Close last track
        closeTrack(sheet.tracks.size());

        // Finish session
        session.endFrameAddress = frameAddress - 1;
        session.BuildTOC();

        // Read header
        const uint32 firstSectorSize = session.tracks.front().sectorSize;
        const uintmax_t userDataOffset = firstSectorSize == 2352 ? 16 : firstSectorSize == 2340 ? 4 : 0;

        std::array<uint8, 256> header{};
        const uintmax_t readSize = reader->Read(userDataOffset, 256, header);
        if (readSize < 256) {
            // fmt::println("BIN/CUE: File truncated");
            return false;
        }
        disc.header.ReadFrom(header);

        // fmt::println("BIN/CUE: Final FAD = {:6d}", frameAddress - 1);

        sgInvalidateDisc.Cancel();

        return true;
    }

    return false;
}

} // namespace ymir::media::loader::bincue
