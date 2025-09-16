#pragma once

#include <ymir/core/hash.hpp>
#include <ymir/core/types.hpp>

#include <ymir/state/state_cdblock.hpp>

#include <ymir/util/bit_ops.hpp>

#include "disc.hpp"
#include "iso9660.hpp"

#include <cassert>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ymir::media::fs {

struct FileInfo {
    FileInfo() = default;
    FileInfo(const iso9660::DirectoryRecord &dirRecord, uint8 fileID)
        : frameAddress(dirRecord.extentPos + 150)
        , fileSize(dirRecord.dataSize)
        , unitSize(dirRecord.fileUnitSize)
        , interleaveGapSize(dirRecord.interleaveGapSize)
        , fileNumber(fileID)
        , attributes(dirRecord.flags)
        , name(dirRecord.fileID) {}

    uint32 frameAddress = ~0;
    uint32 fileSize = ~0;
    uint8 unitSize = ~0;
    uint8 interleaveGapSize = ~0;
    uint8 fileNumber = ~0;
    uint8 attributes = ~0;
    std::string name = "";

    bool IsValid() const {
        return frameAddress != ~0;
    }

    bool IsDirectory() const {
        return bit::test<1>(attributes);
    }

    bool IsFile() const {
        return !IsDirectory();
    }
};

// Represents a file or directory in a path table directory.
class FilesystemEntry {
public:
    FilesystemEntry(const iso9660::DirectoryRecord &dirRecord, uint16 parent, uint8 fileID)
        : m_frameAddress(dirRecord.extentPos)
        , m_size(dirRecord.dataSize)
        , m_parent(parent)
        , m_isDirectory(bit::test<1>(dirRecord.flags))
        , m_fileInfo(dirRecord, fileID)
        , m_name(dirRecord.fileID) {}

    uint32 FrameAddress() const {
        return m_frameAddress;
    }

    uint32 Size() const {
        return m_size;
    }

    uint16 Parent() const {
        return m_parent;
    }

    bool IsFile() const {
        return !m_isDirectory;
    }

    bool IsDirectory() const {
        return m_isDirectory;
    }

    bool IsSelfDirectory() const {
        return m_isDirectory && m_name == ".";
    }

    bool IsParentDirectory() const {
        return m_isDirectory && m_name == "..";
    }

    const FileInfo &GetFileInfo() const {
        return m_fileInfo;
    }

    const std::string_view Name() const {
        return m_name;
    }

private:
    uint32 m_frameAddress;
    uint32 m_size;
    uint16 m_parent;
    bool m_isDirectory;
    FileInfo m_fileInfo;
    std::string m_name;
};

// Represents a path table directory.
class Directory {
public:
    Directory(const iso9660::DirectoryRecord &dirRecord, uint16 parent, std::string_view name)
        : m_frameAddress(dirRecord.extentPos)
        , m_parent(parent)
        , m_name(name) {
        assert(bit::test<1>(dirRecord.flags));
    }

    bool IsRoot() const {
        return m_name == ".";
    }

    uint32 FrameAddress() const {
        return m_frameAddress;
    }

    uint16 Parent() const {
        return m_parent;
    }

    const std::string_view Name() const {
        return m_name;
    }

    const std::vector<FilesystemEntry> &GetContents() const {
        return m_contents;
    }

private:
    uint32 m_frameAddress;
    uint16 m_parent;
    std::string m_name;

    std::vector<FilesystemEntry> m_contents;

    std::map<uint32, uint32> m_dirMappings; // file ID to Directory index mapping

    std::vector<FilesystemEntry> &GetContents() {
        return m_contents;
    }

    std::map<uint32, uint32> &GetDirectoryMappings() {
        return m_dirMappings;
    }

    friend class Filesystem;
};

class Filesystem {
public:
    Filesystem();

    // Clears the loaded file system.
    void Clear();

    // Attempts to read the filesystem structure from the specified disc.
    // Returns true if successful.
    // If this function returns false, the filesystem object is invalidated.
    bool Read(const Disc &disc);

    // Attempts to switch to the specified directory.
    // Returns true if succesful, false if fileID is not a directory or does not exist.
    // The filesystem state is not modified on failure.
    bool ChangeDirectory(uint32 fileID);

    // Attempts to read the specified directory.
    // Returns true if succesful, false if fileID is not a directory or does not exist.
    // The filesystem state is not modified on failure.
    bool ReadDirectory(uint32 fileID);

    // Retrieves the path to the current directory.
    // Returns an empty string if the file system is invalid.
    // Returns "/" if the current directory is the root directory.
    std::string GetCurrentPath() const;

    // Determines if the file system is valid, i.e., there is at least one directory.
    bool IsValid() const {
        return !m_directories.empty();
    }

    // Returns the disc hash, which comprises the first 16 data sectors and those containing the volume descriptors.
    XXH128Hash GetHash() const {
        return m_hash;
    }

    // Determines if the file system has a valid current directory.
    bool HasCurrentDirectory() const {
        return m_currDirectory < m_directories.size();
    }

    // Returns the current file offset for file listings.
    uint32 GetFileOffset() const {
        return m_currFileOffset;
    }

    // Returns the number of files in the current directory, minus the self and parent directory references (. and ..)
    uint32 GetFileCount() const;

    // Retrieves the file info from the current directory for the given file ID relative to the current file offset.
    const FileInfo &GetFileInfoWithOffset(uint8 fileID) const;

    // Retrieves the file info from the current directory for the given absolute file ID.
    const FileInfo &GetFileInfo(uint32 fileID) const;

    // Retrieves the filesystem entry at the specified frame address.
    // Retuns nullptr if there is no file at that FAD, it is out of range or it doesn't point to a data track.
    const FilesystemEntry *GetFileAtFrameAddress(uint32 fad) const;

    // Retrieves the full path of the file at the specified frame address.
    // Retuns nullptr if there is no file at that FAD, it is out of range or it doesn't point to a data track.
    std::string GetPathAtFrameAddress(uint32 fad) const;

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::CDBlockState::FilesystemState &state) const;
    [[nodiscard]] bool ValidateState(const state::CDBlockState::FilesystemState &state) const;
    void LoadState(const state::CDBlockState::FilesystemState &state);

private:
    // Directories parsed from the path table records.
    std::vector<Directory> m_directories;

    // Disc hash
    XXH128Hash m_hash{};

    struct FileIndex {
        size_t directory;
        size_t file;
    };
    // Frame address to file map.
    // The key is actually the upper bound of the filesystem entry.
    std::map<uint32, FileIndex> m_fadToFiles{};

    std::optional<FileIndex> LookupFileIndexAtFrameAddress(uint32 fad) const;

    std::string BuildPath(uint16 directoryIndex) const;

    // Current file system operation state.
    // These fields should be stored in the save state

    uint32 m_currDirectory;
    uint32 m_currFileOffset;

    bool ReadPathTableRecords(const Track &track, const media::iso9660::VolumeDescriptor &volDesc);
};

} // namespace ymir::media::fs
