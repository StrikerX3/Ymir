#include <ymir/media/loader/loader_img_ccd_sub.hpp>

#include <ymir/media/binary_reader/binary_reader_impl.hpp>
#include <ymir/media/frame_address.hpp>

#include <ymir/util/scope_guard.hpp>

#include <fmt/format.h>
#include <fmt/std.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

namespace ymir::media::loader::ccd {

struct CloneCDTocEntry {
    uint8 point;
    uint8 control;
    uint8 adr;
    uint8 min;   // PMin
    uint8 sec;   // PSec
    uint8 frame; // PFrame
    uint32 lba;  // PLBA
};

struct CloneCDSession {
    // Indexed by point
    std::array<CloneCDTocEntry, 256> tocEntries;
};

[[nodiscard]] static std::string ToLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](char c) { return std::tolower(c); });
    return str;
}

struct CaseInsensitiveStringCompare {
    bool operator()(const std::string &lhs, const std::string &rhs) const {
        return ToLower(lhs) < ToLower(rhs);
    }
};

const std::set<std::string, CaseInsensitiveStringCompare> kValidSectionNames = {"CloneCD", "Disc",  "CDText",
                                                                                "Session", "Entry", "TRACK"};

bool Load(std::filesystem::path ccdPath, Disc &disc, bool preloadToRAM, CbLoaderMessage cbMsg) {
    std::ifstream in{ccdPath, std::ios::binary};

    util::ScopeGuard sgInvalidateDisc{[&] { disc.Invalidate(); }};

    auto invFmtMsg = [&](std::string message) { cbMsg(MessageType::InvalidFormat, message); };
    auto errorMsg = [&](std::string message) { cbMsg(MessageType::Error, message); };
    auto debugMsg = [&](std::string message) { cbMsg(MessageType::Debug, message); };

    if (!in) {
        errorMsg("IMG/CCD: Could not load CCD file");
        return false;
    }

    auto trimWhitespace = [](std::string str) -> std::string {
        const auto posFirst = str.find_first_not_of(" \t\r\n");
        const auto posLast = str.find_last_not_of(" \t\r\n") + 1;
        if (posFirst > posLast) {
            return "";
        } else {
            return str.substr(posFirst, posLast);
        }
    };

    // Housekeeping variables
    uint32 lineNum = 0;
    std::string line{};
    std::string currSection{};
    std::vector<CloneCDSession> ccdSessions{};

    CloneCDTocEntry currTocEntry{};
    uint32 currTocEntrySession{};

    auto tryGetSection = [&]() -> bool {
        // Sections are enclosed in [brackets]
        if (!line.starts_with('[')) {
            return false;
        }

        // Check that they end in close brackets
        std::string section = line.substr(1);
        if (section.ends_with(']')) {
            section = section.substr(0, section.size() - 1);
        } else {
            // Let's be lenient here and allow this small syntax error, but report it to the user
            debugMsg(fmt::format("IMG/CCD: Section \"{}\" is missing closing bracket (line {})", line, lineNum));
        }

        // Some section contain index numbers; allow those
        std::string sectionPrefix = section;
        const auto posSpace = sectionPrefix.find(' ');
        if (posSpace != std::string::npos) {
            sectionPrefix = sectionPrefix.substr(0, posSpace);
        }

        // Check that it is a valid section
        if (!kValidSectionNames.contains(sectionPrefix)) {
            errorMsg(fmt::format("IMG/CCD: Invalid section name: \"{}\"", section));
            return false;
        }

        // Good to go
        currSection = section;
        debugMsg(fmt::format("IMG/CCD: Entering section \"{}\"", currSection));
        return true;
    };

    // Peek the first non-blank line to ensure this file looks like a CloneCD Control File
    {
        do {
            if (!std::getline(in, line)) {
                errorMsg("IMG/CCD: Could not read file");
                return false;
            }
            line = trimWhitespace(line);
            ++lineNum;
        } while (line.empty());

        // Check if it looks like a section
        if (!tryGetSection()) {
            invFmtMsg("IMG/CCD: Not a valid CCD file");
            return false;
        }
    }

    disc.sessions.clear();
    while (std::getline(in, line)) {
        line = trimWhitespace(line);
        // lineNum++;

        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        // Try reading a section.
        // If not a section, try reading a key=value pair
        if (tryGetSection()) {
            if (currTocEntrySession != 0) {
                ccdSessions[currTocEntrySession - 1].tocEntries[currTocEntry.point] = currTocEntry;
            }

            currTocEntry = {};
            currTocEntrySession = 0;
        } else {
            // Does it look like a valid key=value pair?
            const auto pos = line.find('=');
            if (pos == std::string::npos) {
                // Let's be lenient here and allow this small syntax error, but report it to the user
                debugMsg(fmt::format("IMG/CCD: Line \"{}\" is missing value (line {}); skipping", line, lineNum));
            } else {
                const std::string key = line.substr(0, pos);
                const std::string valueStr = ToLower(line.substr(pos + 1));
                sint64 value{};
                if (valueStr.starts_with("0x")) {
                    value = std::stoll(valueStr.substr(2), nullptr, 16);
                } else {
                    value = std::stoll(valueStr);
                }
                debugMsg(fmt::format("IMG/CCD:   {} = {}", key, value));

                const std::string lcKey = ToLower(key);
                if (ToLower(currSection) == "disc") {
                    if (lcKey == "sessions") {
                        if (value < 1) {
                            errorMsg(fmt::format("IMG/CCD: Invalid number of sessions ({}) (line {})", value, lineNum));
                            return false;
                        }
                        debugMsg(fmt::format("IMG/CCD: {} {}", value, (value > 1 ? "sessions" : "session")));
                        ccdSessions.resize(value);
                    } else if (lcKey == "datatracksscrambled") {
                        if (value) {
                            errorMsg("IMG/CCD: Scrambled data tracks are not supported");
                            return false;
                        }
                    }
                } else if (ToLower(currSection).starts_with("entry")) {
                    if (lcKey == "session") {
                        if (value < 1) {
                            errorMsg(fmt::format("IMG/CCD: Invalid session number ({}) in [{}] (line {})", value,
                                                 currSection, lineNum));
                            return false;
                        }
                        currTocEntrySession = value;
                    } else if (lcKey == "point") {
                        if (value < 1) {
                            errorMsg(fmt::format("IMG/CCD: Invalid point number ({}) in [{}] (line {})", value,
                                                 currSection, lineNum));
                            return false;
                        }
                        currTocEntry.point = value;
                    } else if (lcKey == "control") {
                        if (value < 0 || value > 0xF) {
                            debugMsg(fmt::format("IMG/CCD: Control value ({}) out of range in [{}] (line {})", value,
                                                 currSection, lineNum));
                        }
                        currTocEntry.control = value;
                    } else if (lcKey == "adr") {
                        if (value < 0 || value > 0xF) {
                            debugMsg(fmt::format("IMG/CCD: ADR value ({}) out of range in [{}] (line {})", value,
                                                 currSection, lineNum));
                        }
                        currTocEntry.adr = value;
                    } else if (lcKey == "pmin") {
                        currTocEntry.min = value;
                    } else if (lcKey == "psec") {
                        currTocEntry.sec = value;
                    } else if (lcKey == "pframe") {
                        currTocEntry.frame = value;
                    } else if (lcKey == "plba") {
                        currTocEntry.lba = value;
                    }
                }
            }
        }
    }

    // Finish any pending TOC entries
    if (currTocEntrySession != 0) {
        ccdSessions[currTocEntrySession - 1].tocEntries[currTocEntry.point] = currTocEntry;
    }

    // Try loading the image file
    std::filesystem::path imgPath = ccdPath;
    imgPath.replace_extension("img");
    std::error_code err{};
    std::shared_ptr<IBinaryReader> imgFile;
    if (preloadToRAM) {
        imgFile = std::make_shared<MemoryBinaryReader>(imgPath, err);
    } else {
        imgFile = std::make_shared<MemoryMappedBinaryReader>(imgPath, err);
    }
    if (err) {
        errorMsg(fmt::format("IMG/CCD: Failed to load image file {}: {}", imgPath, err.message()));
        return false;
    }

    // Read the header
    {
        std::array<uint8, 256> header{};
        const uintmax_t readSize = imgFile->Read(16, 256, header);
        if (readSize < 256) {
            errorMsg(fmt::format("IMG/CCD: Image file {} truncated", imgPath));
            return false;
        }

        disc.header.ReadFrom(header);
    }

    // Build the disc structure
    static constexpr uint32 pregapSize = 150; // seems to be constant

    disc.sessions.resize(ccdSessions.size());
    for (int i = 0; i < disc.sessions.size(); i++) {
        auto &session = disc.sessions[i];
        auto &ccdSession = ccdSessions[i];

        // We require points A0, A1 and A2 to properly build the disc structure
        for (uint32 point = 0xA0; point <= 0xA2; point++) {
            if (ccdSession.tocEntries[point].point != point) {
                errorMsg(fmt::format("IMG/CCD: Missing point {:02X} information for session {}", point, i));
                return false;
            }
        }

        auto &pointA0 = ccdSession.tocEntries[0xA0];
        auto &pointA1 = ccdSession.tocEntries[0xA1];
        auto &pointA2 = ccdSession.tocEntries[0xA2];

        const uint32 firstTrackNum = pointA0.min;
        const uint32 lastTrackNum = pointA1.min;

        session.numTracks = lastTrackNum - firstTrackNum + 1;
        session.firstTrackIndex = firstTrackNum - 1;
        session.lastTrackIndex = lastTrackNum - 1;
        session.startFrameAddress = 0;
        session.endFrameAddress = TimestampToFrameAddress(pointA2.min, pointA2.sec, pointA2.frame);

        for (int j = 0; j < session.numTracks; j++) {
            const uint32 trackNum = firstTrackNum + j;
            auto &track = session.tracks[trackNum - 1];
            track.indices.emplace_back(); // Insert dummy index 00
            auto &index = track.indices.emplace_back();
            auto &point = ccdSession.tocEntries[trackNum];
            if (point.point != trackNum) {
                errorMsg(fmt::format("IMG/CCD: Missing track {} information for session {}", trackNum, i));
                return false;
            }

            track.SetSectorSize(2352);
            track.controlADR = (point.control << 4u) | point.adr;
            track.interleavedSubchannel = false;

            track.startFrameAddress = TimestampToFrameAddress(point.min, point.sec, point.frame);
            index.startFrameAddress = track.startFrameAddress;
            track.index01FrameAddress = track.startFrameAddress;

            // Finish previous track
            if (j > 0) {
                const auto &prevPoint = ccdSession.tocEntries[trackNum - 1];
                auto &prevTrack = session.tracks[trackNum - 2];
                auto &prevIndex = prevTrack.indices.back();

                prevTrack.endFrameAddress = track.startFrameAddress - 1;
                prevIndex.endFrameAddress = prevTrack.endFrameAddress;
                uintmax_t fileOffset = static_cast<uintmax_t>(prevPoint.lba) * prevTrack.sectorSize;
                uintmax_t fileSize =
                    (prevTrack.endFrameAddress - prevTrack.startFrameAddress + 1) * prevTrack.sectorSize;

                // Data track must exclude the pregap
                if (prevTrack.controlADR == 0x41) {
                    fileSize -= static_cast<uintmax_t>(pregapSize) * prevTrack.sectorSize;
                }

                prevTrack.binaryReader = std::make_unique<SharedSubviewBinaryReader>(imgFile, fileOffset, fileSize);
            }
        }

        // Finish last track
        const auto &lastPoint = ccdSession.tocEntries[lastTrackNum];
        auto &lastTrack = session.tracks[lastTrackNum - 1];
        auto &lastIndex = lastTrack.indices.back();

        lastTrack.endFrameAddress = session.endFrameAddress;
        lastIndex.endFrameAddress = lastTrack.endFrameAddress;
        uintmax_t fileOffset = static_cast<uintmax_t>(lastPoint.lba) * lastTrack.sectorSize;
        uintmax_t fileSize = (lastTrack.endFrameAddress - lastTrack.startFrameAddress + 1) * lastTrack.sectorSize;

        // Data track must exclude the pregap
        if (lastTrack.controlADR == 0x41) {
            fileSize -= static_cast<uintmax_t>(pregapSize) * lastTrack.sectorSize;
        }

        lastTrack.binaryReader = std::make_unique<SharedSubviewBinaryReader>(imgFile, fileOffset, fileSize);

        // Finish session
        session.BuildTOC();
    }

    sgInvalidateDisc.Cancel();

    return true;
}

} // namespace ymir::media::loader::ccd
