#include <ymir/media/loader/loader_chd.hpp>

#include <ymir/media/binary_reader/binary_reader_subview.hpp>
#include <ymir/media/frame_address.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/scope_guard.hpp>

#include <fmt/format.h>
#include <fmt/std.h>

#include <libchdr/chd.h>

#include <charconv>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ymir::media::loader::chd {

// Implementation of IBinaryReader that reads from a CHD file.
class CHDBinaryReader final : public IBinaryReader {
public:
    // Initializes a CHD reader from the specified `chd_file` instance.
    // The instance is assumed to be already initialized.
    CHDBinaryReader(chd_file *file)
        : m_file(file) {
        m_header = chd_get_header(file);
        m_hunkBuffer.resize(m_header->hunkbytes);
    }
    ~CHDBinaryReader() {
        chd_close(m_file);
    }

    CHDBinaryReader(const CHDBinaryReader &) = default;
    CHDBinaryReader(CHDBinaryReader &&) = default;

    CHDBinaryReader &operator=(const CHDBinaryReader &) = default;
    CHDBinaryReader &operator=(CHDBinaryReader &&) = default;

    uint32 HunkSize() const {
        return m_header->hunkbytes;
    }

    uintmax_t Size() const final {
        return m_header->logicalbytes;
    }

    uintmax_t Read(uintmax_t offset, uintmax_t size, std::span<uint8> output) const final {
        if (offset >= m_header->logicalbytes) {
            return 0;
        }
        if (size == 0) {
            return 0;
        }

        // Limit size to the smallest of the requested size, the output buffer size and the amount of bytes available in
        // the file starting from offset
        size = std::min<uintmax_t>(size, m_header->logicalbytes - offset);
        size = std::min<uintmax_t>(size, output.size());
        const uint32 firstHunk = std::min<uint32>(offset / m_header->hunkbytes, m_header->hunkcount - 1);
        uint32 hunkOffset = offset % m_header->hunkbytes;
        const uint32 lastHunk = std::min<uint32>((offset + size - 1) / m_header->hunkbytes, m_header->hunkcount - 1);
        uintmax_t writeOffset = 0;
        uintmax_t remaining = size;
        for (uintmax_t hunkIndex = firstHunk; hunkIndex <= lastHunk; hunkIndex++) {
            if (!m_hunkCache.contains(hunkIndex)) {
                chd_read(m_file, hunkIndex, m_hunkBuffer.data());
                m_hunkCache[hunkIndex] = m_hunkBuffer;
            }
            auto &buffer = m_hunkCache[hunkIndex];
            const uint32 requested = std::min<size_t>(remaining, m_header->hunkbytes - hunkOffset);
            std::copy_n(buffer.begin() + hunkOffset, requested, output.begin() + writeOffset);

            remaining -= requested;
            if (remaining == 0) {
                break;
            }
            writeOffset += requested;
            hunkOffset = 0;
        }
        return size - remaining;
    }

private:
    chd_file *m_file;
    const chd_header *m_header;
    mutable std::vector<uint8> m_hunkBuffer;
    mutable std::map<uintmax_t, std::vector<uint8>> m_hunkCache;
};

static bool SetTrackInfo(const chd_header *header, std::string_view typestring, Track &track) {
    // NOTE: This loader uses raw sector sizes which is determined by the unit size from the CHD header
    if (typestring == "MODE1") {
        track.mode2 = false;
        track.SetSectorSize(header->unitbytes); // 2048
        track.controlADR = 0x41;

    } else if (typestring == "MODE1/2048") {
        track.mode2 = false;
        track.SetSectorSize(header->unitbytes); // 2048
        track.controlADR = 0x41;

    } else if (typestring == "MODE1_RAW") {
        track.mode2 = false;
        track.SetSectorSize(header->unitbytes); // 2352
        track.controlADR = 0x41;

    } else if (typestring == "MODE1/2352") {
        track.mode2 = false;
        track.SetSectorSize(header->unitbytes); // 2352
        track.controlADR = 0x41;

    } else if (typestring == "MODE2") {
        track.mode2 = true;
        track.SetSectorSize(header->unitbytes); // 2336
        track.controlADR = 0x41;

    } else if (typestring == "MODE2/2336") {
        track.mode2 = true;
        track.SetSectorSize(header->unitbytes); // 2336
        track.controlADR = 0x41;

    } else if (typestring == "MODE2_FORM1") {
        track.mode2 = true;
        track.SetSectorSize(header->unitbytes); // 2048
        track.controlADR = 0x41;

    } else if (typestring == "MODE2/2048") {
        track.mode2 = true;
        track.SetSectorSize(header->unitbytes); // 2048
        track.controlADR = 0x41;

    } else if (typestring == "MODE2_FORM2") {
        track.mode2 = true;
        track.SetSectorSize(header->unitbytes); // 2324
        track.controlADR = 0x41;

    } else if (typestring == "MODE2/2324") {
        track.mode2 = true;
        track.SetSectorSize(header->unitbytes); // 2324
        track.controlADR = 0x41;

    } else if (typestring == "MODE2_FORM_MIX") {
        track.mode2 = true;
        track.SetSectorSize(header->unitbytes); // 2336
        track.controlADR = 0x41;

    } else if (typestring == "MODE2_RAW") {
        track.mode2 = true;
        track.SetSectorSize(header->unitbytes); // 2352
        track.controlADR = 0x41;

    } else if (typestring == "MODE2/2352") {
        track.mode2 = true;
        track.SetSectorSize(header->unitbytes); // 2352
        track.controlADR = 0x41;

    } else if (typestring == "CDI/2352") {
        track.mode2 = false;
        track.SetSectorSize(header->unitbytes); // 2352
        track.controlADR = 0x41;

    } else if (typestring == "AUDIO") {
        track.mode2 = false;
        track.SetSectorSize(header->unitbytes); // 2352
        track.controlADR = 0x01;
        track.bigEndian = true;
    } else {
        return false;
    }
    return true;
}

bool Load(std::filesystem::path chdPath, Disc &disc, bool preloadToRAM) {
    util::ScopeGuard sgInvalidateDisc{[&] { disc.Invalidate(); }};

    chd_file *file = nullptr;
    chd_error error = chd_open(chdPath.string().c_str(), CHD_OPEN_READ, nullptr, &file);
    if (error != CHDERR_NONE) {
        // fmt::println("CHD: Failed to open file {}: {}", chdPath, chd_error_string(error));
        return false;
    }
    const chd_header *header = chd_get_header(file);

    if (preloadToRAM) {
        chd_precache(file);
    }

    auto binaryReader = std::make_shared<CHDBinaryReader>(file);

    auto &session = disc.sessions.emplace_back();

    std::vector<char> metabuf;
    uint32 resultlen;
    uint32 resulttag;
    uint8 resultflags;

    // Parse metadata and build track list
    uint32 metaIndex = 0;
    uint32 frameAddress = 150;
    uintmax_t byteOffset = 0;
    bool foundTrack = false;
    while (true) {
        error = chd_get_metadata(file, CDROM_TRACK_METADATA2_TAG, metaIndex, metabuf.data(), metabuf.size(), &resultlen,
                                 &resulttag, &resultflags);
        if (error == CHDERR_METADATA_NOT_FOUND) {
            // Reached end of metadata list
            break;
        }
        if (error != CHDERR_NONE) {
            // fmt::println("CHD: Failed to read metadata: {}", chd_error_string(error));
            return false;
        }

        if (metabuf.size() < resultlen) {
            // Too small; make room for it
            metabuf.resize(resultlen);
        } else {
            int tracknum = 0;
            std::string type{};
            std::string subtype{};
            int frames = 0;
            int pregap = 0;
            std::string pgtype{};
            std::string pgsub{};
            int postgap = 0;

            // MAME uses sscanf, but the format strings contain unbounded %s. Risky!
            // To be extra safe, we're manually parsing the strings here. More work, but no risk of buffer overflows.
            if (resulttag == CDROM_TRACK_METADATA2_TAG || resulttag == CDROM_TRACK_METADATA_TAG) {
                // CHTR: "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d"
                // CHT2: "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d"

                size_t pos = 0;
                auto readFixed = [&](std::string_view expected) -> bool {
                    std::string_view buf{metabuf.begin() + pos, metabuf.begin() + pos + expected.size()};
                    pos += expected.size();
                    return buf == expected;
                };
                auto readInt = [&](int &out) -> bool {
                    std::string_view buf{metabuf.begin() + pos, metabuf.end()};
                    auto [ptr, ec] = std::from_chars(buf.data(), buf.data() + buf.size(), out);
                    if (ec != std::errc{}) {
                        return false;
                    }
                    pos += std::distance(static_cast<const char *>(&metabuf[pos]), ptr);
                    return true;
                };
                auto readString = [&](std::string &out) -> bool {
                    std::string_view buf{metabuf.begin() + pos, metabuf.end()};
                    buf = buf.substr(0, buf.find(' '));
                    if (buf.empty()) {
                        return false;
                    }
                    pos += buf.size();
                    out = buf;
                    return true;
                };

                // Parse common section first
                if (!readFixed("TRACK:")) {
                    return false;
                }
                if (!readInt(tracknum)) {
                    return false;
                }
                if (!readFixed(" TYPE:")) {
                    return false;
                }
                if (!readString(type)) {
                    return false;
                }
                if (!readFixed(" SUBTYPE:")) {
                    return false;
                }
                if (!readString(subtype)) {
                    return false;
                }
                if (!readFixed(" FRAMES:")) {
                    return false;
                }
                if (!readInt(frames)) {
                    return false;
                }

                if (resulttag == CDROM_TRACK_METADATA2_TAG) {
                    // Parse version 2 parameters
                    if (!readFixed(" PREGAP:")) {
                        return false;
                    }
                    if (!readInt(pregap)) {
                        return false;
                    }
                    if (!readFixed(" PGTYPE:")) {
                        return false;
                    }
                    if (!readString(pgtype)) {
                        return false;
                    }
                    if (!readFixed(" PGSUB:")) {
                        return false;
                    }
                    if (!readString(pgsub)) {
                        return false;
                    }
                    if (!readFixed(" POSTGAP:")) {
                        return false;
                    }
                    if (!readInt(postgap)) {
                        return false;
                    }
                }
            } else {
                // fmt::println("CHD: Unknown metadata format {}, contents: {}", resulttag, metabuf.data());
                return false;
            }

            auto &track = session.tracks[tracknum - 1];
            if (!SetTrackInfo(header, type, track)) {
                // fmt::println("CHD: Unknown track type {}\n", type);
                return false;
            }
            uintmax_t subviewOffset = byteOffset;
            if (track.controlADR == 0x01) {
                // Add pregap on audio tracks
                subviewOffset += pregap * track.sectorSize;
            } else {
                // Find start of next sector and adjust offset accordingly
                uintmax_t offset = 0;
                while (true) {
                    static constexpr std::array<uint8, 12> kSyncBytes = {
                        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
                    };
                    std::array<uint8, 12> syncBuf{};
                    if (binaryReader->Read(byteOffset + offset, syncBuf.size(), syncBuf) == syncBuf.size() &&
                        syncBuf == kSyncBytes) {
                        std::array<uint8, 3> headerBuf{};
                        if (binaryReader->Read(byteOffset + offset + 0xC, headerBuf.size(), headerBuf) ==
                                headerBuf.size() &&
                            headerBuf[0] == util::to_bcd(frameAddress / 75 / 60) &&
                            headerBuf[1] == util::to_bcd((frameAddress / 75) % 60) &&
                            headerBuf[2] == util::to_bcd(frameAddress % 75)) {
                            break;
                        }
                    }

                    offset += track.sectorSize;
                    if (byteOffset + offset >= binaryReader->Size()) {
                        // fmt::println("CHD: Could not find starting sector for track {}", track.index);
                        return false;
                    }
                }

                byteOffset += offset;
                subviewOffset += offset;
            }
            track.binaryReader =
                std::make_unique<SharedSubviewBinaryReader>(binaryReader, subviewOffset, frames * track.sectorSize);
            track.startFrameAddress = frameAddress;
            track.endFrameAddress = frameAddress + frames - 1;
            track.track01FrameAddress = frameAddress;
            track.interleavedSubchannel = false;
            track.indices.emplace_back(); // Insert dummy index 00
            auto &index = track.indices.emplace_back();
            index.startFrameAddress = track.startFrameAddress;
            index.endFrameAddress = track.endFrameAddress;
            frameAddress += frames;
            byteOffset += frames * track.sectorSize;

            if (!foundTrack) {
                foundTrack = true;
                session.firstTrackIndex = tracknum - 1;
                session.lastTrackIndex = tracknum - 1;
                session.numTracks = 1;
            } else {
                session.firstTrackIndex = std::min<uint32>(session.firstTrackIndex, tracknum - 1);
                session.lastTrackIndex = std::max<uint32>(session.lastTrackIndex, tracknum - 1);
                ++session.numTracks;
            }

            /*fmt::println("CHD: Track {}, {}, {}, {} frames, pregap: {} frames, {}, {}, postgap: {} frames\n",
               tracknum, type, subtype, frames, pregap, pgtype, pgsub, postgap);*/
            ++metaIndex;
        }
    }

    // Finish session
    session.startFrameAddress = 0;
    session.endFrameAddress = frameAddress - 1;
    session.BuildTOC();

    // Read Saturn disc header
    if (session.numTracks > 0) {
        std::array<uint8, 2048> headerData{};
        if (!session.tracks[session.firstTrackIndex].ReadSectorUserData(150, headerData)) {
            // fmt::println("CHD: Could not read Saturn disc header");
            return false;
        }

        disc.header.ReadFrom(std::span<uint8, 256>{headerData.begin(), 256});
    }

    sgInvalidateDisc.Cancel();
    return true;
}

} // namespace ymir::media::loader::chd
