#include <ymir/sys/backup_ram.hpp>

#include <ymir/core/types.hpp>

#include <fmt/format.h>

#include <filesystem>
#include <string>

void runBUPSandbox() {
    // Valid backup memory parameters:
    // Device      Size     Block size
    // Internal    32 KiB   64 b
    // External    512 KiB  512 b
    // External    1 MiB    512 b
    // External    2 MiB    512 b
    // External    4 MiB    1 KiB

    ymir::bup::BackupMemory mem{};
    std::error_code error{};
    mem.CreateFrom("bup-int.bin", false, error, ymir::bup::BackupMemorySize::_256Kbit);
    if (error) {
        fmt::println("Failed to read backup memory file: {}", error.message());
        return;
    }
    mem.Delete("GBASICSS_01");

    ymir::bup::BackupFile file{};
    file.header.filename = "ANDROMEDA_3";
    file.header.comment = "ANDROMEDA_";
    file.header.date = 0;
    file.header.language = ymir::bup::Language::Japanese;
    for (uint32 i = 0; i < 256; i++) {
        file.data.push_back(i);
    }
    file.data.push_back('t');
    file.data.push_back('e');
    file.data.push_back('s');
    file.data.push_back('t');
    /*file.data.push_back('0');
    file.data.push_back('1');
    file.data.push_back('2');
    file.data.push_back('3');*/

    auto result = mem.Import(file, true);
    switch (result) {
    case ymir::bup::BackupFileImportResult::Imported: fmt::println("File imported successfully"); break;
    case ymir::bup::BackupFileImportResult::Overwritten: fmt::println("File overwritten successfully"); break;
    case ymir::bup::BackupFileImportResult::FileExists: fmt::println("File not imported: file already exists"); break;
    case ymir::bup::BackupFileImportResult::NoSpace: fmt::println("File not imported: not enough space"); break;
    }

    const uint32 usedBlocks = mem.GetUsedBlocks();
    const uint32 totalBlocks = mem.GetTotalBlocks();
    fmt::println("Backup memory size: {} bytes", mem.Size());
    fmt::println("Blocks: {} of {} used ({} free)", usedBlocks, totalBlocks, totalBlocks - usedBlocks);

    static constexpr const char *kLanguages[] = {"JP", "EN", "FR", "DE", "SP", "IT"};

    for (const auto &file : mem.List()) {
        auto trimToNull = [](std::string &str) {
            auto zeroPos = str.find_first_of('\0');
            if (zeroPos != std::string::npos) {
                str.resize(zeroPos, '\0');
            }
        };

        std::string filename = file.header.filename;
        std::string comment = file.header.comment;
        trimToNull(filename);
        trimToNull(comment);
        std::transform(filename.begin(), filename.end(), filename.begin(), [](char c) { return c < 0 ? '?' : c; });
        std::transform(comment.begin(), comment.end(), comment.begin(), [](char c) { return c < 0 ? '?' : c; });

        fmt::println("{:11s} | {:10s} | {} | {:3d} | {:6d} bytes | {:02d} {:02d}:{:02d}", filename, comment,
                     kLanguages[static_cast<uint8>(file.header.language)], file.numBlocks, file.size,
                     file.header.date / 60 / 24, (file.header.date / 60) % 24, file.header.date % 60);

        auto optFileData = mem.Export(file.header.filename);
        if (optFileData) {
            auto &fileData = *optFileData;
            uint32 pos = 0;
            for (auto b : fileData.data) {
                if (pos % 16 == 0) {
                    fmt::print("  {:06X} |", pos);
                }
                if (pos % 16 == 8) {
                    fmt::print(" ");
                }
                fmt::print(" {:02X}", b);
                if (pos % 16 == 15 || pos == fileData.data.size() - 1) {
                    fmt::println("");
                }
                pos++;
            }
        }
    }
}
