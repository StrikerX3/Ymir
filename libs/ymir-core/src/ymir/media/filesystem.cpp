#include <ymir/media/filesystem.hpp>

#include <ymir/util/dev_assert.hpp>
#include <ymir/util/scope_guard.hpp>

#include <xxh3.h>

#include <cassert>
#include <unordered_map>
#include <vector>

using namespace ymir::media::iso9660;

namespace ymir::media::fs {

Filesystem::Filesystem() {
    Clear();
}

void Filesystem::Clear() {
    m_directories.clear();
    m_hash.fill(0);
    m_fadToFiles.clear();
}

bool Filesystem::Read(const Disc &disc) {
    Clear();
    util::ScopeGuard sgInvalidate = [&] { Clear(); };

    // TODO: test multisession discs

    // Bail out if there are no sessions in the disc
    if (disc.sessions.empty()) {
        return false;
    }

    // The Saturn uses the volume descriptor from the final session on the disc
    const Session &session = disc.sessions.back();

    // The volume descriptor is at frame address 166 (00:02:16) from the start of the session
    const uint32 absVolumeDescAddress = session.startFrameAddress + 166;

    // Find the track containing the frame address
    const Track *pTrack = session.FindTrack(absVolumeDescAddress);
    if (pTrack == nullptr) {
        // Could not find track with the specified frame address
        return false;
    }
    const Track &track = *pTrack;
    if (track.controlADR != 0x41) {
        // Not a data track
        return false;
    }

    // Buffer for sector data
    std::array<uint8, 2048> buf{};

    // Found the track; begin hashing the first 16 sectors
    XXH3_state_t *xxh3State = XXH3_createState();
    assert(xxh3State != NULL && "Out of memory!");
    util::ScopeGuard sgFreeXXH3State{[&] { XXH3_freeState(xxh3State); }};
    XXH3_128bits_reset(xxh3State);
    for (uint32 sectorIndex = 150; sectorIndex < 166; sectorIndex++) {
        // Fail if we can't read the sector
        if (!track.ReadSectorUserData(sectorIndex, buf)) {
            YMIR_DEV_CHECK();
            return false;
        }
        XXH3_128bits_update(xxh3State, buf.data(), buf.size());
    }

    // Read volume descriptors; hash these sectors as well
    const uint32 volumeDescAddress = absVolumeDescAddress;
    for (uint32 sectorIndex = volumeDescAddress;; sectorIndex++) {
        // Fail if we can't read the sector
        if (!track.ReadSectorUserData(sectorIndex, buf)) {
            YMIR_DEV_CHECK();
            return false;
        }
        XXH3_128bits_update(xxh3State, buf.data(), buf.size());

        // Try reading volume descriptor; fail if invalid
        VolumeDescriptorHeader volDescHeader{};
        if (!volDescHeader.Read(buf)) {
            YMIR_DEV_CHECK();
            return false;
        }

        // Succeed if we found a terminator
        if (volDescHeader.type == VolumeDescriptorType::Terminator) {
            sgInvalidate.Cancel();
            if (!IsValid()) {
                YMIR_DEV_CHECK();
                return false;
            } else {
                XXH128_hash_t hash = XXH3_128bits_digest(xxh3State);
                XXH128_canonical_t canonicalHash{};
                XXH128_canonicalFromHash(&canonicalHash, hash);
                std::copy_n(canonicalHash.digest, m_hash.size(), m_hash.begin());
                return true;
            }
        }

        // Parse the different types of volume descriptors
        // TODO: parse supplementary/enhanced volume descriptors, and maybe volume partition descriptors too
        if (volDescHeader.type == VolumeDescriptorType::Primary) {
            VolumeDescriptor volDesc{};
            if (!volDesc.Read(buf)) {
                YMIR_DEV_CHECK();
                return false;
            }

            // Try reading the path table records from the disc; fail on error
            if (!ReadPathTableRecords(track, volDesc)) {
                YMIR_DEV_CHECK();
                return false;
            }
        }
    }

    // Fail if we somehow get here without finding a terminator
    YMIR_DEV_CHECK();
    return false;
}

const FilesystemEntry *Filesystem::GetFileAtFrameAddress(uint32 fad) const {
    if (auto index = LookupFileIndexAtFrameAddress(fad)) {
        auto &dir = m_directories[index->directory];
        auto &contents = dir.GetContents();
        return &contents[index->file];
    }
    return nullptr;
}

std::string Filesystem::GetPathAtFrameAddress(uint32 fad) const {
    if (auto index = LookupFileIndexAtFrameAddress(fad)) {
        auto &dir = m_directories[index->directory];
        auto &contents = dir.GetContents();
        std::string path = BuildPath(index->directory);
        std::string filename(contents[index->file].Name());
        if (path == "/") {
            return filename;
        } else {
            return path + "/" + filename;
        }
    }
    return "";
}

std::optional<Filesystem::FileIndex> Filesystem::LookupFileIndexAtFrameAddress(uint32 fad) const {
    auto it = m_fadToFiles.upper_bound(fad);
    if (it == m_fadToFiles.end()) {
        return std::nullopt;
    }

    assert(it->second.directory < m_directories.size());
    auto &dir = m_directories[it->second.directory];
    auto &contents = dir.GetContents();

    assert(it->second.file < contents.size());
    auto &file = contents[it->second.file];
    if (fad < file.FrameAddress()) {
        return std::nullopt;
    }
    return it->second;
}

std::string Filesystem::BuildPath(uint16 directoryIndex) const {
    if (directoryIndex == 0) {
        // Root directory
        return "/";
    }

    // Build path from components
    std::vector<uint32> fullPath{};
    uint32 currDir = directoryIndex;
    fullPath.push_back(currDir);
    while (currDir != 0 && fullPath.size() < 32) {
        currDir = m_directories[currDir].Parent() - 1; // 1-indexed
        if (currDir != 0) {
            fullPath.push_back(currDir);
        }
    }

    std::string out{};

    bool first = true;
    for (auto it = fullPath.rbegin(); it != fullPath.rend(); ++it) {
        if (first) {
            first = false;
        } else {
            out += "/";
        }
        out += m_directories[*it].Name();
    }
    return out;
}

bool Filesystem::ReadPathTableRecords(const Track &track, const VolumeDescriptor &volDesc) {
    // Fail if there is no LSB path table
    // TODO: support MSB path table
    if (volDesc.pathTableLPos == 0) {
        return false;
    }

    // Buffer for path table sector data
    std::array<uint8, 2048> pathTableBuf{};
    std::vector<uint8> pathTableTempBuf{};

    // Buffer for directory record sector data
    std::array<uint8, 2048> dirRecBuf{};
    std::vector<uint8> dirRecTempBuf{};

    // Read all path table records
    const uint32 pathSectorCount = (volDesc.pathTableSize + 2047) / 2048;
    for (uint32 pathSectorIndex = 0; pathSectorIndex < pathSectorCount; pathSectorIndex++) {
        if (!track.ReadSectorUserData(volDesc.pathTableLPos + pathSectorIndex + 150, pathTableBuf)) {
            return false;
        }

        PathTableRecord pathTableRecord{};
        for (uint32 pathRecIndex = 0;; pathRecIndex += pathTableRecord.recordSize) {
            // Try reading the path table record
            const std::span<uint8> pathRecBufData{pathTableBuf.begin() + pathRecIndex, pathTableBuf.end()};

            if (!pathTableTempBuf.empty()) {
                // Use leftover buffer to read a complete entry
                const uint32 pathRecSize = PathTableRecord::ReadSize(pathTableTempBuf);
                pathRecIndex -= pathTableTempBuf.size();
                pathTableTempBuf.insert(pathTableTempBuf.end(), pathRecBufData.begin(),
                                        pathRecBufData.begin() + pathRecSize - pathTableTempBuf.size());
                if (!pathTableRecord.Read(pathTableTempBuf)) {
                    return false;
                }

                pathTableTempBuf.clear();
            } else {
                const uint32 pathRecSize = PathTableRecord::ReadSize(pathRecBufData);
                if (pathRecSize <= pathRecBufData.size()) {
                    // Read directly from buffer if the record fits
                    if (!pathTableRecord.Read(pathRecBufData)) {
                        return false;
                    }
                } else {
                    // Make a temporary buffer with whatever data we have so far
                    pathTableTempBuf.clear();
                    pathTableTempBuf.insert(pathTableTempBuf.end(), pathRecBufData.begin(), pathRecBufData.end());
                    break;
                }
            }

            // Bail out if this is the last record in the current sector
            if (pathTableRecord.recordSize == 0) {
                break;
            }
            // TODO: read extended attributes if present

            // Try reading the directory record
            DirectoryRecord dirRecord{};
            if (!track.ReadSectorUserData(pathTableRecord.extentPos + 150, dirRecBuf)) {
                return false;
            }
            if (!dirRecord.Read(dirRecBuf)) {
                return false;
            }
            // Fail if there is no directory record
            if (dirRecord.recordSize == 0) {
                return false;
            }
            // Fail if it's not a directory
            if (!bit::test<1>(dirRecord.flags)) {
                return false;
            }

            // Create a directory entry
            const size_t directoryIndex = m_directories.size();
            Directory &directory =
                m_directories.emplace_back(dirRecord, pathTableRecord.parentDirNumber, pathTableRecord.directoryID);
            auto &contents = directory.GetContents();

            // Read directory contents
            const uint32 dirSectorCount = (dirRecord.dataSize + 2047) / 2048;
            for (uint32 dirSectorIndex = 0; dirSectorIndex < dirSectorCount; dirSectorIndex++) {
                if (!track.ReadSectorUserData(dirRecord.extentPos + dirSectorIndex + 150, dirRecBuf)) {
                    return false;
                }

                uint32 dirRecOffset = 0;
                DirectoryRecord subdirRecord{};
                for (;;) {
                    // Try reading the directory record
                    const std::span<uint8> dirRecBufData{dirRecBuf.begin() + dirRecOffset, dirRecBuf.end()};

                    if (!dirRecTempBuf.empty()) {
                        // Use leftover buffer to read a complete entry
                        const uint32 dirRecSize = DirectoryRecord::ReadSize(dirRecTempBuf);
                        dirRecOffset -= dirRecTempBuf.size();
                        dirRecTempBuf.insert(dirRecTempBuf.end(), dirRecBufData.begin(),
                                             dirRecBufData.begin() + dirRecSize - dirRecTempBuf.size());
                        if (!subdirRecord.Read(dirRecTempBuf)) {
                            return false;
                        }

                        dirRecTempBuf.clear();
                    } else {
                        const uint32 dirRecSize = DirectoryRecord::ReadSize(dirRecBufData);
                        if (dirRecSize <= dirRecBufData.size()) {
                            // Read directly from buffer if the record fits
                            if (!subdirRecord.Read(dirRecBufData)) {
                                return false;
                            }
                        } else {
                            // Make a temporary buffer with whatever data we have so far
                            dirRecTempBuf.clear();
                            dirRecTempBuf.insert(dirRecTempBuf.end(), dirRecBufData.begin(), dirRecBufData.end());
                            break;
                        }
                    }
                    // Bail out if this is the end of the list
                    if (subdirRecord.recordSize == 0) {
                        break;
                    }
                    // TODO: read extended attributes if present
                    const uint8 fileNum = 0;

                    // Add record to directory
                    const size_t fsEntryIndex = contents.size();
                    auto &fsEntry = contents.emplace_back(subdirRecord, pathRecIndex + 1, fileNum);

                    // Map FAD->entry
                    if (fsEntry.IsFile()) {
                        const uint32 entryBaseFAD = fsEntry.FrameAddress();
                        const uint32 entryFADCount = (fsEntry.Size() + 2047) / 2048;
                        const uint32 finalFAD = entryBaseFAD + entryFADCount - 1;
                        m_fadToFiles.insert({finalFAD, {directoryIndex, fsEntryIndex}});
                    }

                    dirRecOffset += subdirRecord.recordSize;
                }
            }
        }
    }

    // Map subdirectories to their directory entries
    std::unordered_map<std::string, size_t> dirRefs{};
    for (size_t i = 0; i < m_directories.size(); ++i) {
        const auto &dir = m_directories[i];
        if (dir.IsRoot()) {
            continue;
        }
        dirRefs[std::string(dir.Name())] = i;
    }

    for (size_t i = 0; i < m_directories.size(); ++i) {
        auto &dir = m_directories[i];
        const auto &subdirs = dir.GetContents();
        for (size_t j = 0; j < subdirs.size(); ++j) {
            auto &subdir = subdirs[j];
            if (!subdir.IsDirectory()) {
                continue;
            }
            if (subdir.IsSelfDirectory() || subdir.IsParentDirectory()) {
                continue;
            }

            if (dirRefs.contains(std::string(subdir.Name()))) {
                const size_t pos = dirRefs.at(std::string(subdir.Name()));
                dir.GetDirectoryMappings()[j] = pos;
            }
        }
    }

    return true;
}

// -----------------------------------------------------------------------------

FilesystemState::FilesystemState(const Filesystem &fs)
    : m_fs(fs) {
    Reset();
}

void FilesystemState::Reset() {
    m_currDirectory = ~0;
    m_currFileOffset = 0;
}

bool FilesystemState::ChangeDirectory(uint32 fileID) {
    if (!m_fs.IsValid()) {
        return false;
    }

    const auto &dirs = m_fs.GetDirectories();

    if (fileID == 0xFFFFFF) {
        // Go to root directory; should be the first in the list
        m_currDirectory = 0;
    } else if (fileID == 0) {
        // Self directory; no change
    } else if (m_currDirectory != ~0 && fileID == 1) {
        // Go to parent directory
        m_currDirectory = dirs[m_currDirectory].Parent() - 1;
    } else if (fileID + m_currFileOffset < dirs[m_currDirectory].GetContents().size()) {
        // Go to specified directory
        const auto &mapping = dirs[m_currDirectory].GetDirectoryMappings();
        const uint32 id = fileID + m_currFileOffset;
        if (!mapping.contains(id)) {
            return false;
        }
        m_currDirectory = mapping.at(id);
    } else {
        // File ID out of range or invalid current directory
        return false;
    }

    m_currFileOffset = 0;
    return true;
}

bool FilesystemState::ReadDirectory(uint32 fileID) {
    if (!m_fs.IsValid()) {
        return false;
    }
    if (!HasCurrentDirectory()) {
        return false;
    }
    assert(m_currDirectory < m_fs.GetDirectories().size());

    // TODO: should read and retain up to 254 files (plus self and parent dirs) starting from fileID
    return true;
}

std::string FilesystemState::GetCurrentPath() const {
    if (!m_fs.IsValid()) {
        return "";
    }
    if (!HasCurrentDirectory()) {
        return "";
    }
    assert(m_currDirectory < m_fs.GetDirectories().size());

    return m_fs.BuildPath(m_currDirectory);
}

uint32 FilesystemState::GetFileCount() const {
    if (!m_fs.IsValid()) {
        // No file system loaded
        return 0;
    }
    if (!HasCurrentDirectory()) {
        // Invalid directory
        return 0;
    }
    assert(m_currDirectory < m_fs.GetDirectories().size());

    return m_fs.GetDirectories()[m_currDirectory].GetContents().size() - 2;
}

static const FileInfo kEmptyFileInfo = {};

const FileInfo &FilesystemState::GetFileInfoWithOffset(uint8 fileID) const {
    if (!m_fs.IsValid()) {
        // No file system loaded
        return kEmptyFileInfo;
    }
    if (!HasCurrentDirectory()) {
        // Invalid directory
        return kEmptyFileInfo;
    }
    const auto offset = fileID + m_currFileOffset;
    const auto &currDirContents = m_fs.GetDirectories()[m_currDirectory].GetContents();
    if (offset >= currDirContents.size()) {
        return kEmptyFileInfo;
    }
    return currDirContents[offset].GetFileInfo();
}

const FileInfo &FilesystemState::GetFileInfo(uint32 fileID) const {
    if (!m_fs.IsValid()) {
        // No file system loaded
        return kEmptyFileInfo;
    }
    if (!HasCurrentDirectory()) {
        // Invalid directory
        return kEmptyFileInfo;
    }
    const auto &currDirContents = m_fs.GetDirectories()[m_currDirectory].GetContents();
    if (fileID >= currDirContents.size()) {
        return kEmptyFileInfo;
    }
    return currDirContents[fileID].GetFileInfo();
}

void FilesystemState::SaveState(state::CDBlockState::FilesystemState &state) const {
    state.currDirectory = m_currDirectory;
    state.currFileOffset = m_currFileOffset;
}

bool FilesystemState::ValidateState(const state::CDBlockState::FilesystemState &state) const {
    return true;
}

void FilesystemState::LoadState(const state::CDBlockState::FilesystemState &state) {
    m_currDirectory = state.currDirectory;
    m_currFileOffset = state.currFileOffset;
}

} // namespace ymir::media::fs
