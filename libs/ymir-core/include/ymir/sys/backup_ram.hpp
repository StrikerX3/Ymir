#pragma once

#include <ymir/core/types.hpp>

#include <ymir/sys/backup_ram_defs.hpp>
#include <ymir/sys/bus.hpp>

#include <mio/mmap.hpp>

#include <filesystem>
#include <optional>
#include <vector>

namespace ymir::bup {

enum class BackupMemoryImageLoadResult { Success, FilesystemError, InvalidSize };

class BackupMemory final : public IBackupMemory {
public:
    void MapMemory(sys::SH2Bus &bus, uint32 start, uint32 end);

    // -------------------------------------------------------------------------
    // Image management

    // Loads a backup memory file at the specified path.
    // The file size determines the backup memory size.
    // The image is not modified in any way.
    //
    // `path` is the path to the backup memory file to create.
    // `error` will contain any error that occurs while loading or manipulating the file.
    //
    // Returns BackupMemoryImageLoadResult::Success if the image was loaded successfully.
    // Returns BackupMemoryImageLoadResult::FilesystemError if there was a filesystem error while loading the image.
    // `error` will contain the error.
    // Returns BackupMemoryImageLoadResult::InvalidSize if the image size doesn't match any of the valid sizes.
    // `error` is not modified in this case.
    BackupMemoryImageLoadResult LoadFrom(const std::filesystem::path &path, std::error_code &error);

    // Creates or replaces a backup memory file at the specified path with the given size.
    // If the file does not exist, it is created with the given size.
    // If the file exists with a different size, it is resized or truncated to match.
    // If the file had to be created, resized or did not contain a valid backup memory, it is formatted.
    //
    // `path` is the path to the backup memory file to create.
    // `size` is the total backup memory size.
    // `error` will contain any error that occurs while loading or manipulating the file.
    void CreateFrom(const std::filesystem::path &path, BackupMemorySize size, std::error_code &error);

    // Creates an in-memory backup memory with the given size, not backed by a file.
    // The memory is formatted if it does not contain a valid backup memory header.
    void CreateInMemory(BackupMemorySize size);

    bool CopyFrom(const IBackupMemory &backupRAM) final;

    std::filesystem::path GetPath() const final;

    // -------------------------------------------------------------------------
    // Bus interface

    uint8 ReadByte(uint32 address) const final;
    uint16 ReadWord(uint32 address) const final;
    uint32 ReadLong(uint32 address) const final;

    void WriteByte(uint32 address, uint8 value) final;
    void WriteWord(uint32 address, uint16 value) final;
    void WriteLong(uint32 address, uint32 value) final;

    // -------------------------------------------------------------------------
    // Backup file management

    std::vector<uint8> ReadAll() const final;

    bool IsHeaderValid() const final;

    uint32 Size() const final;
    uint32 GetBlockSize() const final;
    uint32 GetTotalBlocks() const final;
    uint32 GetUsedBlocks() final;

    void Format() final;

    std::vector<BackupFileInfo> List() const final;
    std::optional<BackupFileInfo> GetInfo(std::string_view filename) const final;
    std::optional<BackupFile> Export(std::string_view filename) const final;
    std::vector<BackupFile> ExportAll() const final;
    BackupFileImportResult Import(const BackupFile &file, bool overwrite) final;
    bool Delete(std::string_view filename) final;

private:
    // TODO: support three types
    // - in-memory (std::vector)
    // - memory-mapped file (mio::mmap_sink)
    // - memory-mapped copy-on-write file (mio::mmap_cow_sink)
    mio::mmap_sink m_backupRAM;
    std::vector<uint8_t> m_memoryBuffer;
    uint8_t *m_data = nullptr;
    size_t m_dataSize = 0;

    std::filesystem::path m_path;

    size_t m_addressMask = 0;
    uint32 m_blockSize = 0;
    size_t m_addressShift = 0;

    bool m_dirty = false;

    struct BackupFileParams {
        BackupFileInfo info;
        std::vector<uint16> blocks;
    };

    bool m_headerValid;
    std::vector<BackupFileParams> m_fileParams;
    std::vector<uint64> m_blockBitmap;

    // -------------------------------------------------------------------------
    // Data interface

    uint8 DataReadByte(uint32 address) const;
    uint16 DataReadWord(uint32 address) const;
    uint32 DataReadLong(uint32 address) const;
    void DataReadString(uint32 address, void *str, uint32 length) const;

    void DataWriteByte(uint32 address, uint8 value);
    void DataWriteWord(uint32 address, uint16 value);
    void DataWriteLong(uint32 address, uint32 value);
    void DataWriteString(uint32 address, const void *str, uint32 length);
    void DataFill(uint32 address, uint8 value, uint32 length);

    // -------------------------------------------------------------------------
    // Backup file management

    // Rebuilds the file list from the contents of the backup memory.
    //
    // `force` forces the rebuild even if the dirty flag is clear.
    void RebuildFileList(bool force = false);
    void RebuildFileList(bool force = false) const;

    // Checks if the backup image is interleaved.
    bool CheckInterleaved() const;

    // Checks if the header is valid.
    bool CheckHeader() const;

    // Finds the file with the given filename.
    // Returns nullptr if the file cannot be found.
    BackupFileParams *FindFile(std::string_view filename);
    const BackupFileParams *FindFile(std::string_view filename) const;

    // Reads in the backup file header from the given block.
    // Returns true if the header is valid.
    [[nodiscard]] bool ReadHeader(uint16 blockIndex, BackupFileHeader &header) const;

    // Reads the block list from the given block.
    // The list contains `blockIndex` as the first entry.
    std::vector<uint16> ReadBlockList(uint16 blockIndex) const;

    // Builds a backup file.
    BackupFile BuildFile(const BackupFileParams &params) const;
};

} // namespace ymir::bup
