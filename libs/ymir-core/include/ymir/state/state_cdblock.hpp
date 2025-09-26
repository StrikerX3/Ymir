#pragma once

#include <ymir/hw/cdblock/cdblock_defs.hpp>

#include <ymir/core/hash.hpp>
#include <ymir/core/types.hpp>

#include <array>

namespace ymir::state {

struct CDBlockState {
    alignas(16) std::array<uint16, 4> CR;
    uint16 HIRQ;
    uint16 HIRQMASK;

    struct StatusState {
        uint8 statusCode;
        uint32 frameAddress;
        uint8 flags;
        uint8 repeatCount;
        uint8 controlADR;
        uint8 track;
        uint8 index;
    } status;

    bool readyForPeriodicReports;

    uint32 currDriveCycles;
    uint32 targetDriveCycles;
    uint32 seekTicks;

    uint32 playStartParam;
    uint32 playEndParam;
    uint8 playRepeatParam;
    bool scanDirection;
    uint8 scanCounter;

    uint32 playStartPos;
    uint32 playEndPos;
    uint8 playMaxRepeat;
    bool playFile;
    bool bufferFullPause;
    bool playEndPending;

    uint8 readSpeed;

    uint8 discAuthStatus;
    uint8 mpegAuthStatus;

    enum class TransferType {
        None = 0,
        TOC = 1,
        GetSector = 2,
        GetThenDeleteSector = 3,
        PutSector = 6,
        FileInfo = 4,
        Subcode = 5
    };
    TransferType xferType;
    uint32 xferPos;
    uint32 xferLength;
    uint32 xferCount;
    std::array<uint16, 2352 / sizeof(uint16)> xferBuffer;
    uint32 xferBufferPos;

    uint32 xferSectorPos;
    uint32 xferSectorEnd;
    uint8 xferPartition;
    uint32 xferGetLength;
    uint32 xferDelStart;
    uint32 xferDelCount;

    uint32 xferSubcodeFrameAddress;
    uint32 xferSubcodeGroup;

    uint32 xferExtraCount;

    struct BufferState {
        alignas(16) std::array<uint8, 2352> data;
        uint16 size;
        uint32 frameAddress;
        uint8 fileNum;
        uint8 chanNum;
        uint8 submode;
        uint8 codingInfo;

        // 0 to kNumPartitions-1 = that partition
        // 255 = scratch buffer or not used
        // All buffers are stored sequentially
        uint8 partitionIndex;
    };
    alignas(16) std::array<BufferState, cdblock::kNumBuffers + 1> buffers; // 200 buffers + 1 scratch buffer
    uint32 scratchBufferPutIndex;
    uint32 reservedBuffers;

    struct FilterState {
        uint32 startFrameAddress;
        uint32 frameAddressCount;

        uint8 mode;

        uint8 fileNum;
        uint8 chanNum;

        uint8 submodeMask;
        uint8 submodeValue;

        uint8 codingInfoMask;
        uint8 codingInfoValue;

        uint8 passOutput;
        uint8 failOutput;
    };
    alignas(16) std::array<FilterState, cdblock::kNumFilters> filters;

    uint8 cdDeviceConnection;
    uint8 lastCDWritePartition;

    uint32 calculatedPartitionSize;

    uint32 getSectorLength;
    uint32 putSectorLength;

    bool processingCommand;

    struct FilesystemState {
        uint32 currDirectory;
        uint32 currFileOffset;
    } fs;
};

} // namespace ymir::state
