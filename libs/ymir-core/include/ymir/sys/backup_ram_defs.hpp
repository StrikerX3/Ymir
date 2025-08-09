#pragma once

#include <ymir/core/types.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ymir::bup {

enum class BackupMemorySize {
    _256Kbit, // Internal Backup RAM
    _4Mbit,   // 4 Mbit External Backup RAM
    _8Mbit,   // 8 Mbit External Backup RAM
    _16Mbit,  // 16 Mbit External Backup RAM
    _32Mbit,  // 32 Mbit External Backup RAM
};

enum class Language : uint8 {
    Japanese = 0x00,
    English = 0x01,
    French = 0x02,
    German = 0x03,
    Spanish = 0x04,
    Italian = 0x05,
};

struct BackupFileHeader {
    std::string filename; // max 11 chars
    std::string comment;  // max 10 chars
    Language language;
    uint32 date; // minutes since 01/01/1980
};

struct BackupFileInfo {
    BackupFileHeader header;
    uint32 size; // in bytes (including block list)
    uint32 numRawBlocks;
    uint32 numBlocks;
};

struct BackupFile {
    BackupFileHeader header;
    std::vector<uint8> data;
};

enum class BackupFileImportResult { Imported, Overwritten, FileExists, NoSpace };

// Interface for backup memory operations.
class IBackupMemory {
public:
    virtual ~IBackupMemory() = default;

    virtual uint8 ReadByte(uint32 address) const = 0;
    virtual uint16 ReadWord(uint32 address) const = 0;
    virtual uint32 ReadLong(uint32 address) const = 0;

    virtual void WriteByte(uint32 address, uint8 value) = 0;
    virtual void WriteWord(uint32 address, uint16 value) = 0;
    virtual void WriteLong(uint32 address, uint32 value) = 0;

    // Replaces the contents of this backup memory with the contents of the given backup memory.
    //
    // Returns true if the entire contents of `backupRAM` were copied to this backup memory
    // Returns false if `backupRAM` is larger than this backup memory.
    // Note that copying from a smaller backup memory is supported.
    virtual bool CopyFrom(const IBackupMemory &backupRAM) = 0;

    // Retrieves the path to the file with the backing image of the backup memory if in use.
    virtual std::filesystem::path GetPath() const = 0;

    // Reads the entire backup memory into a vector.
    virtual std::vector<uint8> ReadAll() const = 0;

    // Checks if the backup memory header is valid.
    virtual bool IsHeaderValid() const = 0;

    // Retrieves the total size in bytes of the backup memory.
    virtual uint32 Size() const = 0;

    // Retrieves the block size.
    virtual uint32 GetBlockSize() const = 0;

    // Retrieves the total number of blocks.
    virtual uint32 GetTotalBlocks() const = 0;

    // Computes the number of blocks used by backup files.
    virtual uint32 GetUsedBlocks() = 0;

    // Formats the backup memory.
    virtual void Format() = 0;

    // Retrieves a list of backup files stored in this backup memory.
    virtual std::vector<BackupFileInfo> List() const = 0;

    // Attempts to get information about a backup file.
    //
    // Returns a BackupFileInfo with the file information if it exists.
    // Returns std::nullopt if no such file exists.
    virtual std::optional<BackupFileInfo> GetInfo(std::string_view filename) const = 0;

    // Attempts to export the backup file with the specified name.
    //
    // Returns a BackupFile with the file's contents if it exists.
    // Returns std::nullopt if no such file exists.
    virtual std::optional<BackupFile> Export(std::string_view filename) const = 0;

    // Exports all backup files.
    virtual std::vector<BackupFile> ExportAll() const = 0;

    // Attempts to import the specified backup file, optionally overwriting an existing file with the same name as the
    // one being imported.
    //
    // Returns BackupFileImportResult::Imported if the file was newly imported.
    // Returns BackupFileImportResult::Overwritten if the overwrite flag is set and an existing file was overwritten.
    // Returns BackupFileImportResult::FileExists if the overwrite flag is clear and the file already exists.
    // Returns BackupFileImportResult::NoSpace if there is not enough space to import the file. The contents of the
    // backup memory are not modified if this happens.
    virtual BackupFileImportResult Import(const BackupFile &file, bool overwrite) = 0;

    // Attempts to delete a backup file with the specified name.
    //
    // Returns true if the file was deleted.
    // Returns false if there was no file with the specified name.
    virtual bool Delete(std::string_view filename) = 0;
};

} // namespace ymir::bup
