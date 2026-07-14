#pragma once

// ISO 9660 / ECMA-119 CD-ROM file system data structures and readers.
//
// https://ecma-international.org/wp-content/uploads/ECMA-119_5th_edition_december_2024.pdf

#include <ymir/core/types.hpp>

#include <ymir/util/data_ops.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <string>

namespace ymir::media::iso9660 {

// Note on field formats: the specification describes numeric fields can be recorded as little-endian, big-endian or
// both byte orders simultaneously, where the number is recorded as little-endian followed by big-endian. This header
// documents these numeric types with the following convention:
//   <signedness>int<size><endianness>
//   where:
//   - signedness is "u" for unsigned integers or "s" for signed integers
//   - size is the bit width of the integer: 8, 16 or 32
//   - endianness is either "le" (little-endian), "be" (big-endian) or "lbe" (both byte orders)
// For example, uint32lbe represents an unsigned 32-bit integer stored as little-endian then big-endian.
// Endianness is omitted for 8-bit values.
//
// Date and time have two representations:
// - DDateTime: a series of 16 ASCII digits followed by one byte in the format YYYYMMDDHHMMSSssO:
//     YYYY: Year from 1 to 9999
//     MM: Month of the year from 1 to 12
//     DD: Day of the month from 1 to 31
//     HH: Hour of the day from 0 to 23
//     MM: Minute of the hour from 0 to 59
//     SS: Second of the minute from 0 to 59
//     ss: Hundredths of a second
//     O: Offset from GMT in 15-minute intervals from -48 to +52
// - NDateTime: a series of 7 bytes:
//     0: Year offset from 1900
//     1: Month of the year from 1 to 12
//     2: Day of the month from 1 to 31
//     3: Hour of the day from 0 to 23
//     4: Minute of the hour from 0 to 59
//     5: Second of the minute from 0 to 59
//     6: Offset from GMT in 15-minute intervals from -48 to +52

// -----------------------------------------------------------------------------
// Basic types

// Numeric date and time format (ECMA-119 9.4.27.2)
struct DateTime {
    uint16 year;       // Year from 1 to 9999
    uint8 month;       // Month of the year from 1 to 12
    uint8 day;         // Day of the month from 1 to 31
    uint8 hour;        // Hour of the day from 0 to 23
    uint8 minute;      // Minute of the hour from 0 to 59
    uint8 second;      // Second of the minute from 0 to 59
    uint8 centisecond; // Hundredths of a second
    uint8 gmtOffset;   // Offset from GMT in 15-minute intervals from -48 to +52

    DateTime() {
        Clear();
    }

    void Clear() {
        year = 0;
        month = 0;
        day = 0;
        hour = 0;
        minute = 0;
        second = 0;
        centisecond = 0;
        gmtOffset = 0;
    }

    // Parses a numeric date and time formatted as YYYYMMDDHHMMSSssO, where all but the last character are ASCII digits.
    bool ParseNumeric(std::span<uint8> dateTime) {
        if (dateTime.size() < 17) {
            return false;
        }
        year = util::DecimalToInt<uint16>(dateTime.subspan(0, 4));
        month = util::DecimalToInt<uint8>(dateTime.subspan(4, 2));
        day = util::DecimalToInt<uint8>(dateTime.subspan(6, 2));
        hour = util::DecimalToInt<uint8>(dateTime.subspan(8, 2));
        minute = util::DecimalToInt<uint8>(dateTime.subspan(10, 2));
        second = util::DecimalToInt<uint8>(dateTime.subspan(12, 2));
        centisecond = util::DecimalToInt<uint8>(dateTime.subspan(14, 2));
        gmtOffset = dateTime[16];
        return true;
    }

    // Builds a DateTime from the given values, where year is an offset from 1900.
    bool ParseValues(std::span<uint8> values) {
        if (values.size() < 7) {
            return false;
        }
        ParseValues(values[0], values[1], values[2], values[3], values[4], values[5], values[6]);
        return true;
    }

    void ParseValues(uint8 year, uint8 month, uint8 day, uint8 hour, uint8 minute, uint8 second, uint8 gmtOffset) {
        this->year = year + 1900;
        this->month = month;
        this->day = day;
        this->hour = hour;
        this->minute = minute;
        this->second = second;
        this->centisecond = 0;
        this->gmtOffset = gmtOffset;
    }
};

// -----------------------------------------------------------------------------
// Directory records

// Directory record structure:
//    0              uint8         Length of directory record (LEN_DR)
//    1              uint8         Extended attribute record length
//    2-9            uint32lbe     Location of extent
//   10-17           uint32lbe     Data length
//   18-24           NDateTime     Recording date and time
//   25              uint8         File flags
//                                   0  Existence: 0=must be listed to user, 1=may be hidden from user
//                                   1  Directory: 0=file, 1=directory
//                                   2  Associated file: 0=no, 1=yes
//                                   3  Record: 0=not used, 1=uses extended attribute record
//                                   4  Protection: 0=not protected, 1=protected
//                                   5  (reserved)
//                                   6  (reserved)
//                                   7  Multi-extent: 0=final extent, 1=continues
//   26              uint8         File unit size
//   27              uint8         Interleave gap size
//   28-31           uint16lbe     Volume sequence number
//   32              uint8         Length of file identifier (LEN_FI)
//   33-(32+LEN_FI)  char[LEN_FI]  File identifier
//   (33+LEN_FI)     uint8         Padding field (00 byte)
struct DirectoryRecord {
    uint8 recordSize;
    uint8 extAttrRecordSize;

    uint32 extentPos;
    uint32 dataSize;

    DateTime recordingDateTime;
    uint8 flags;

    uint8 fileUnitSize;
    uint8 interleaveGapSize;

    uint16 volSeqNumber;

    std::string fileID;
    uint16 fileVersion;

    DirectoryRecord() {
        Clear();
    }

    void Clear() {
        recordSize = 0;
        extAttrRecordSize = 0;

        extentPos = 0;
        dataSize = 0;

        recordingDateTime.Clear();
        flags = 0;

        fileUnitSize = 0;
        interleaveGapSize = 0;

        volSeqNumber = 0;

        fileID.clear();
        fileVersion = 0;
    }

    // Retrieves the directory record size at the start of the given input span.
    // 0 indicates the directory table record list terminator.
    // 0xFFFFFFFF means the size could not be read because the input span is empty.
    static uint32 ReadSize(std::span<uint8> input) {
        // Ensure there's enough data to read the static fields
        if (input.empty()) {
            return 0xFFFFFFFF;
        }
        return input[0];
    }

    // Fills in this record with data from the start of the given span.
    // Returns true if the record has been fully read.
    // If false, the record may have been partially updated or not modified.
    bool Read(std::span<uint8> input) {
        // Ensure there's enough data to read the static fields
        if (input.size() < 34) {
            // Not enough space for this record; could be the last in the sector
            recordSize = 0;
            return true;
        }

        recordSize = input[0];
        if (recordSize == 0) {
            // Blank record (reading past the end of the list)
            return true;
        }
        extAttrRecordSize = input[1];
        extentPos = util::ReadLE<uint32>(&input[2]);
        dataSize = util::ReadLE<uint32>(&input[10]);
        if (!recordingDateTime.ParseValues(input.subspan(18, 7))) {
            return false;
        }
        flags = input[25];
        fileUnitSize = input[26];
        interleaveGapSize = input[27];
        volSeqNumber = util::ReadLE<uint16>(&input[28]);
        const uint8 fileIDLength = input[32];
        if (input.size() < 33 + fileIDLength) {
            return false;
        }
        fileID.assign(&input[33], &input[33] + fileIDLength);
        if (fileID.size() == 1) {
            if (fileID[0] == 0x00) {
                fileID = ".";
            } else if (fileID[0] == 0x01) {
                fileID = "..";
            }
        } else {
            const auto sep2Pos = fileID.find_last_of(';');
            std::string verNum;
            if (sep2Pos != std::string::npos) {
                verNum = fileID.substr(sep2Pos + 1);
            } else {
                verNum = "";
            }
            if (!verNum.empty()) {
                fileVersion = std::stoi(verNum);
                fileID = fileID.substr(0, sep2Pos);
            } else {
                fileVersion = 0;
            }
        }
        return true;
    }
};

// -----------------------------------------------------------------------------
// Path table records

// Path table record structure:
//    0              uint8         Length of directory identifier (LEN_DI)
//    1              uint8         Extended attribute record length
//    2-5            uint32le      Location of extent
//    6-7            uint16le      Parent directory number
//    8-(7+LEN_DI)   char[LEN_DI]  Directory identifier
//    (8+LEN_DI)     -             Padding field (00 byte)
struct PathTableRecord {
    uint32 recordSize;
    uint8 extAttrRecordSize;

    uint32 extentPos;

    uint16 parentDirNumber;

    std::string directoryID;

    // Retrieves the path record size at the start of the given input span.
    // 0 indicates a path table record list terminator.
    // 0xFFFFFFFF means the size could not be read because the input span is empty.
    static uint32 ReadSize(std::span<uint8> input) {
        // Ensure there's enough data to read the static fields
        if (input.empty()) {
            return 0xFFFFFFFF;
        }

        const uint8 dirIDLength = input[0];
        if (dirIDLength == 0) {
            // Blank record (reading past the end of the list)
            return 0;
        }
        return (dirIDLength + 1 + 8) & ~1;
    }

    // Fills in this record with data from the start of the given span.
    // Returns true if the record has been fully read.
    // If false, the record may have been partially updated or not modified.
    bool Read(std::span<uint8> input) {
        // Ensure there's enough data to read the static fields
        if (input.size() < 9) {
            // Not enough space for this record; could be the last in the sector
            recordSize = 0;
            return true;
        }

        const uint8 dirIDLength = input[0];
        if (dirIDLength == 0) {
            // Blank record (reading past the end of the list)
            recordSize = 0;
            return true;
        }
        recordSize = (dirIDLength + 1 + 8) & ~1;
        extAttrRecordSize = input[1];
        extentPos = util::ReadLE<uint32>(&input[2]);
        parentDirNumber = util::ReadLE<uint16>(&input[6]);
        if (input.size() < 8 + dirIDLength) {
            return false;
        }
        directoryID.assign(&input[8], &input[8] + dirIDLength);
        if (directoryID.size() == 1) {
            if (directoryID[0] == 0x00) {
                directoryID = ".";
            } else if (directoryID[0] == 0x01) {
                directoryID = "..";
            }
        }
        // NOTE: one padding byte (00) if dirIDLength is odd
        return true;
    }
};

// -----------------------------------------------------------------------------
// Extended attribute records

// Extended attribute record structure:
//    0-3            uint16lbe      Owner identification
//    4-7            uint16lbe      Group identification
//    8-9            uint16le       Permissions
//                                     0: read by owner in system group: 0=allow, 1=deny
//                                     2: exec by owner in system group: 0=allow, 1=deny
//                                     4: read by owner: 0=allow, 1=deny
//                                     6: exec by owner: 0=allow, 1=deny
//                                     8: read by users in group: 0=all, 1=owner only
//                                    10: exec by users in group: 0=all, 1=owner only
//                                    12: read: 0=any user, 1=users in group only
//                                    14: exec: 0=any user, 1=users in group only
//                                    Odd bits are all 1
//   10-26           NDateTime      File creation date and time
//   27-43           NDateTime      File modification date and time
//   44-60           NDateTime      File expiration date and time
//   61-77           NDateTime      File effective date and time
//   78              uint8          Record format
//                                    0: not specified by this field
//                                    1: sequence of fixed-length records
//                                    2: sequence of variable-length records with little-endian RCW
//                                    3: sequence of variable-length records with big-endian RCW
//                                    The rest are reserved
//   79              uint8          Record attributes (for character displays)
//   80-83           uint16lbe      Record length
//   84-115          char[32]       System identifier
//  116-179          -              System use
//  180              uint8          Extended attribute record version
//  181              uint8          Length of escape sequences (LEN_ESC)
//  182-245          -              (reserved)
//  246-249          uint16lbe      Length of application use (LEN_AU)
//  250-(249+LEN_AU) char[LEN_AU]   Application use
//  (250+LEN_AU)-(249+LEN_ESC+LEN_AU)
//                   char[LEN_ESC]  Escape sequences
struct ExtendedAttributeRecord {
    uint16 ownerID;
    uint16 groupID;
    uint16 perms;

    DateTime creationDateTime;
    DateTime modificationDateTime;
    DateTime expirationDateTime;
    DateTime effectiveDateTime;

    uint8 format;
    uint8 attributes;
    uint16 length;

    std::string systemID;

    uint8 version;

    std::string escapeSequences;

    // Fills in this record with data from the start of the given span.
    // Returns true if the record has been fully read.
    // If false, the record may have been partially updated or not modified.
    bool Read(std::span<uint8> input) {
        // Ensure there's enough data to read the static fields
        if (input.size() < 250) {
            return false;
        }

        ownerID = util::ReadLE<uint16>(&input[0]);
        groupID = util::ReadLE<uint16>(&input[4]);
        perms = util::ReadLE<uint16>(&input[8]);
        creationDateTime.ParseNumeric(input.subspan(10, 17));
        modificationDateTime.ParseNumeric(input.subspan(27, 17));
        expirationDateTime.ParseNumeric(input.subspan(44, 17));
        effectiveDateTime.ParseNumeric(input.subspan(61, 17));
        format = input[78];
        attributes = input[79];
        length = util::ReadLE<uint16>(&input[80]);
        systemID.assign(&input[84], &input[116]);
        version = input[180];
        const uint8 escSeqLength = input[181];
        const uint16 appUseLength = util::ReadLE<uint16>(&input[246]);
        if (input.size() < 249 + escSeqLength + appUseLength) {
            return false;
        }
        escapeSequences.assign(&input[250] + appUseLength, &input[249] + appUseLength + escSeqLength);
        return true;
    }
};

// -----------------------------------------------------------------------------
// Volume descriptors
//
// Volume descriptors identify the volume, its partitions, creator(s), locations of additional descriptions, several
// specific attributes and the version of the standard. They can be one of the following types:
// - Boot record                       (type 0, version 1)
// - Primary volume descriptor         (type 1, version 1)
// - Supplementary volume descriptor   (type 2, version 1)
// - Enhanced volume descriptor        (type 2, version 2)
// - Volume partition descriptor       (type 3, version 1)
// - Volume descriptor set terminator  (type 4, version 1)
//
// The volume descriptor set contains a series of volume descriptors stored sequentially starting from logical sector
// number 16 (or 166 on the Sega Saturn). There must be one primary volume descriptor in the set (which may appear
// multiple times) and it must end with one or more volume descriptor set terminators. All other types may appear zero
// or more times.

enum class VolumeDescriptorType : uint8 {
    BootRecord = 0,    // Boot record
    Primary = 1,       // Primary volume descriptor
    Supplementary = 2, // Supplementary (version 1) or enhanced (version 2) volume descriptor
    Partition = 3,     // Volume partition descriptor
    Terminator = 255,  // Volume descriptor set terminator
};

// Every volume descriptor shares the same header:
//   0     uint8    Volume descriptor type
//                      0 = boot record
//                      1 = primary volume descriptor
//                      2 = supplementary or enhanced volume descriptor
//                      3 = volume partition descriptor
//                    255 = volume descriptor set terminator
//   1-5   char[5]  Standard identifier (must be "CD001")
//   6     uint8    Volume descriptor version (depends on type; typically 1)
// The remainder of the contents of the descriptor varies according to the type.
struct VolumeDescriptorHeader {
    VolumeDescriptorType type;
    std::array<char, 5> identifier;
    uint8 version;

    // Fills in this descriptor with data from the start of the given span.
    // Returns true if the descriptor has been fully read.
    // If false, the descriptor may have been partially updated or not modified.
    bool Read(std::span<uint8> input) {
        // Ensure there's enough data to read the static fields
        if (input.size() < 7) {
            return false;
        }

        type = static_cast<VolumeDescriptorType>(input[0]);
        std::copy_n(&input[1], identifier.size(), identifier.begin());
        version = input[6];
        return true;
    }

    // Determines if this header contains valid values
    bool Valid() const {
        static constexpr std::array<char, 5> kExpectedIdentifier = {'C', 'D', '0', '0', '1'};
        if (identifier != kExpectedIdentifier) {
            return false;
        }

        switch (type) {
        case VolumeDescriptorType::BootRecord: return version == 1;
        case VolumeDescriptorType::Primary: return version == 1;
        case VolumeDescriptorType::Supplementary: return version == 1 || version == 2;
        case VolumeDescriptorType::Partition: return version == 1;
        case VolumeDescriptorType::Terminator: return version == 1;
        }

        return false;
    }
};

// The data structures below comprise bytes 7-2047 from the descriptor sector.

// A volume descriptor set terminator has all bytes reserved and must be all zeros.

// Boot record structure:
//   7-38    char[32]    Boot system identifier
//  39-70    char[32]    Boot identifier
//  71-2047  -           Boot system use
struct BootRecord {
    std::string bootSystemID;
    std::string bootID;
    // std::array<char, 1977> bootSystemUse;

    // Fills in this descriptor with data from the start of the given span, which should point to the beginning of the
    // sector.
    // Returns true if the descriptor has been fully read.
    // If false, the descriptor may have been partially updated or not modified.
    bool Read(std::span<uint8> input) {
        // Ensure there's enough data to read the static fields
        if (input.size() < 2048) {
            return false;
        }

        bootSystemID.assign(&input[7], &input[39]);
        bootID.assign(&input[39], &input[71]);
        return true;
    }
};

// Primary/supplementary/enhanced volume descriptor structure:
//    7       uint8       Volume flags (supplementary/enhanced volume descriptors only)
//    8-39    char[32]    System identifier
//   40-71    char[32]    Volume identifier
//   72-79    -           (unused)
//   80-87    uint32lbe   Volume space size
//   88-119   char[32]    Escape sequences (supplementary/enhanced volume descriptors only)
//  120-123   uint16lbe   Volume set size
//  124-127   uint16lbe   Volume sequence number
//  128-131   uint16lbe   Logical block size
//  132-139   uint32lbe   Path table size
//  140-143   uint16lbe   Location of occurrence of type L path table
//  144-147   uint16lbe   Location of optional occurrence of type L path table
//  148-151   uint16lbe   Location of occurrence of type M path table
//  152-155   uint16lbe   Location of optional occurrence of type M path table
//  156-189   *           Directory record for root directory
//  190-317   char[128]   Volume set identifier
//  318-445   char[128]   Publisher identifier
//  446-573   char[128]   Data preparer identifier
//  574-701   char[128]   Application identifier
//  702-738   char[37]    Copyright file identifier
//  739-775   char[37]    Abstract file identifier
//  776-812   char[37]    Bibliographic file identifier
//  813-829   DDateTime   Volume creation date and time
//  830-846   DDateTime   Volume modification date and time
//  847-863   DDateTime   Volume expiration date and time
//  864-880   DDateTime   Volume effective date and time
//  881       uint8       File structure version
//  882       -           (reserved)
//  883-1394  -           Application use
// 1395-2047  -           (reserved)
// NOTE: the difference between type L and type M path tables is the endianness: (L)east/(M)ost significant byte first.
struct VolumeDescriptor {
    std::string systemID;
    std::string volumeID;

    uint32 flags;
    std::array<char, 32> escapeSequences;

    uint32 spaceSize;
    uint16 setSize;
    uint16 seqNumber;
    uint16 logicalBlockSize;

    uint32 pathTableSize;
    uint16 pathTableLPos;
    uint16 pathTableLOptPos;
    uint16 pathTableMPos;
    uint16 pathTableMOptPos;

    DirectoryRecord rootDirRecord;

    std::string volumeSetID;
    std::string publisherID;
    std::string dataPreparerID;
    std::string applicationID;

    std::string copyrightFileID;
    std::string abstractFileID;
    std::string bibliographicFileID;

    DateTime creationDateTime;
    DateTime modificationDateTime;
    DateTime expirationDateTime;
    DateTime effectiveDateTime;
    uint8 fileStructureVersion;

    // std::array<char, 512> applicationUse;
    VolumeDescriptor() {
        Clear();
    }

    void Clear() {
        systemID.clear();
        volumeID.clear();

        flags = 0;
        escapeSequences.fill('\0');

        spaceSize = 0;
        setSize = 0;
        seqNumber = 0;
        logicalBlockSize = 0;

        pathTableSize = 0;
        pathTableLPos = 0;
        pathTableLOptPos = 0;
        pathTableMPos = 0;
        pathTableMOptPos = 0;

        rootDirRecord.Clear();

        volumeSetID.clear();
        publisherID.clear();
        dataPreparerID.clear();
        applicationID.clear();

        copyrightFileID.clear();
        abstractFileID.clear();
        bibliographicFileID.clear();

        creationDateTime.Clear();
        modificationDateTime.Clear();
        expirationDateTime.Clear();
        effectiveDateTime.Clear();
        fileStructureVersion = 0;
    }

    // Fills in this descriptor with data from the start of the given span, which should point to the beginning of the
    // sector.
    // Returns true if the descriptor has been fully read.
    // If false, the descriptor may have been partially updated or not modified.
    bool Read(std::span<uint8> input) {
        // Ensure there's enough data to read the static fields
        if (input.size() < 2048) {
            return false;
        }

        flags = input[7];
        systemID.assign(&input[8], &input[40]);
        volumeID.assign(&input[40], &input[72]);
        spaceSize = util::ReadLE<uint32>(&input[80]);
        std::copy_n(&input[88], 32, escapeSequences.begin());
        setSize = util::ReadLE<uint16>(&input[120]);
        seqNumber = util::ReadLE<uint16>(&input[124]);
        logicalBlockSize = util::ReadLE<uint16>(&input[128]);
        pathTableSize = util::ReadLE<uint32>(&input[132]);
        pathTableLPos = util::ReadLE<uint16>(&input[140]);
        pathTableLOptPos = util::ReadLE<uint16>(&input[144]);
        pathTableMPos = util::ReadLE<uint16>(&input[148]);
        pathTableMOptPos = util::ReadLE<uint16>(&input[152]);
        rootDirRecord.Read(input.subspan(156));
        volumeSetID.assign(&input[190], &input[318]);
        publisherID.assign(&input[318], &input[446]);
        dataPreparerID.assign(&input[446], &input[574]);
        applicationID.assign(&input[574], &input[702]);
        copyrightFileID.assign(&input[702], &input[739]);
        abstractFileID.assign(&input[739], &input[776]);
        bibliographicFileID.assign(&input[776], &input[813]);
        creationDateTime.ParseNumeric(input.subspan(813));
        modificationDateTime.ParseNumeric(input.subspan(830));
        expirationDateTime.ParseNumeric(input.subspan(847));
        effectiveDateTime.ParseNumeric(input.subspan(864));
        fileStructureVersion = input[881];
        return true;
    }
};

// Volume partition descriptor structure:
//   7       -           (unused)
//   8-39    char[32]    System identifier
//  40-71    char[32]    Volume partition identifier
//  72-79    uint32lbe   Volume partition location
//  80-87    uint32lbe   Volume partition size
//  88-2047  -           System use
struct VolumePartitionDescriptor {
    std::string systemID;
    std::string partitionID;

    uint32 partitionPos;
    uint32 partitionSize;

    // std::array<char, 1960> applicationUse;

    // Fills in this descriptor with data from the start of the given span, which should point to the beginning of the
    // sector.
    // Returns true if the descriptor has been fully read.
    // If false, the descriptor may have been partially updated or not modified.
    bool Read(std::span<uint8> input) {
        // Ensure there's enough data to read the static fields
        if (input.size() < 2048) {
            return false;
        }

        systemID.assign(&input[8], &input[40]);
        partitionID.assign(&input[40], &input[72]);
        partitionPos = util::ReadLE<uint32>(&input[72]);
        partitionSize = util::ReadLE<uint32>(&input[80]);
        return true;
    }
};

} // namespace ymir::media::iso9660
