#include <ymir/media/cd_utils.hpp>

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/dev_assert.hpp>

#include <algorithm>
#include <array>

namespace ymir::media {

static constexpr auto kCRCTable = [] {
    std::array<uint32, 256> crcTable{};
    for (uint32 i = 0; i < 256; ++i) {
        uint32 c = i;
        for (uint32 j = 0; j < 8; ++j) {
            c = (c >> 1) ^ ((c & 0x1) ? 0xD8018001 : 0);
        }
        crcTable[i] = c;
    }
    return crcTable;
}();

uint32 CalcCRC(std::span<uint8, 2064> sector) {
    uint32 crc = 0;
    for (uint8 b : sector) {
        crc ^= b;
        crc = (crc >> 8) ^ kCRCTable[crc & 0xFF];
    }
    return crc;
}

void SynthesizeSectorData(std::span<uint8, 2352> sector, uint32 sectorSize, uint32 frameAddress, uint8 controlADR,
                          bool mode2) {
    const bool hasSyncBytes = sectorSize >= 2352;
    const bool hasHeader = sectorSize >= 2340;
    const bool hasECC = sectorSize >= 2336;

    if (!hasSyncBytes) {
        static constexpr std::array<uint8, 12> syncBytes = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
        std::copy(syncBytes.begin(), syncBytes.end(), sector.begin());
    }
    if (!hasHeader) {
        // Convert absolute frame address to min:sec:frac
        sector[0xC] = util::to_bcd(frameAddress / 75 / 60);
        sector[0xD] = util::to_bcd((frameAddress / 75) % 60);
        sector[0xE] = util::to_bcd(frameAddress % 75);

        // Determine mode based on track type and sector size
        if (controlADR == 0x41) {
            // Data track
            sector[0xF] = mode2 ? 0x02 : 0x01;
        } else {
            // Audio track
            sector[0xF] = 0x00;
        }
        // fmt::println("  Added header");
    } else {
        YMIR_DEV_ASSERT(sector[0xC] == util::to_bcd(frameAddress / 75 / 60));
        YMIR_DEV_ASSERT(sector[0xD] == util::to_bcd((frameAddress / 75) % 60));
        YMIR_DEV_ASSERT(sector[0xE] == util::to_bcd(frameAddress % 75));
    }
    if (!hasECC) {
        // Fill out EDC, Intermediate, P-Parity and Q-Parity fields
        // TODO: handle Mode 2 Form 1 and 2

        std::span<uint8> edcBuf{sector.subspan(2064, 4)};
        std::span<uint8> interBuf{sector.subspan(2068, 8)};
        std::span<uint8> pParityBuf{sector.subspan(2076, 172)};
        std::span<uint8> qParityBuf{sector.subspan(2248, 104)};

        const uint32 crc = CalcCRC(std::span<uint8, 2064>{sector.first(2064)});
        util::WriteLE<uint32>(&edcBuf[0], crc);

        std::fill(interBuf.begin(), interBuf.end(), 0x00);

        // TODO: compute ECC (P-Parity and Q-Parity)
        std::fill(pParityBuf.begin(), pParityBuf.end(), 0x00);
        std::fill(qParityBuf.begin(), qParityBuf.end(), 0x00);

        // fmt::println("  Added subheader");
    }
}

} // namespace ymir::media
