/**
@file
@brief Implementation of physical CD devices for Unix-like systems (Linux, FreeBSD and such, except macOS).
*/

#include "ymir/util/data_ops.hpp"

#include <ymir/media/device/cd_device_physical.hpp>

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/dev_assert.hpp>
#include <ymir/util/scope_guard.hpp>

#include <scsi/sg.h>
#include <sys/ioctl.h>

#include <vector>

namespace ymir::media {

static constexpr uint8 kSCSIOperationInquiry = 0x12;
static constexpr uint8 kSCSIOperationGetConfiguration = 0x46;
static constexpr uint8 kSCSIOperationReadTOC = 0x43;
static constexpr uint8 kSCSIOperationReadCD = 0xBE;
static constexpr uint8 kSCSIOperationRead10 = 0x28;

static constexpr uint16 kSCSIFeatureCDRead = 0x001E;

struct PhysicalCDDevice::Context {
    struct Features {
        Features() {
            Reset();
        }

        void Reset() {
            // The READ CD SCSI command is supported.
            // Some emulated drives don't support it, forcing us to use the READ 10 command as fallback which doesn't
            // read the full raw sector.
            readCDCommand = false;
        }

        bool readCDCommand;
    } features;

    int fd = -1;

    std::vector<TOCEntry> tocEntries;

    FORCE_INLINE bool IsOpen() const {
        return fd >= 0;
    }

    FLATTEN FORCE_INLINE size_t ReadRawSector(uint32 frameAddress, std::span<uint8, 2352> out) {
        if (features.readCDCommand) {
            return ReadRawSectorSCSIReadCD(frameAddress, out);
        } else {
            return ReadRawSectorSCSIRead10(frameAddress, out);
        }
    }

    void UpdateSupportedFeatures() {
        features.Reset();

        if (!IsOpen()) {
            return;
        }

        std::vector<uint8> buffer{};
        buffer.resize(8);

        std::array<uint8, 96> senseBuffer{};

        std::array<uint8, 10> cdb{};
        cdb[0] = kSCSIOperationGetConfiguration;
        cdb[1] = 2;                                         // Request one feature
        util::WriteBE<uint16>(&cdb[2], kSCSIFeatureCDRead); // CD Read feature
        util::WriteBE<uint16>(&cdb[7], buffer.size());      // Allocation length

        // Execute command to get buffer size
        sg_io_hdr_t ioHdr{};
        ioHdr.interface_id = 'S';
        ioHdr.dxfer_direction = SG_DXFER_FROM_DEV;
        ioHdr.cmd_len = sizeof(cdb);
        ioHdr.mx_sb_len = sizeof(senseBuffer);
        ioHdr.dxfer_len = buffer.size();
        ioHdr.dxferp = buffer.data();
        ioHdr.cmdp = cdb.data();
        ioHdr.sbp = senseBuffer.data();
        ioHdr.timeout = 3000;
        if (ioctl(fd, SG_IO, &ioHdr) < 0) {
            return;
        }
        if ((ioHdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
            return;
        }

        // Redo the request with a buffer large enough to fit the data
        const auto bufferLength = util::ReadBE<uint32>(&buffer[0]);
        buffer.resize(bufferLength + 4);
        util::WriteBE<uint16>(&cdb[7], buffer.size());
        ioHdr.dxfer_len = buffer.size();
        ioHdr.dxferp = buffer.data();
        if (ioctl(fd, SG_IO, &ioHdr) < 0) {
            return;
        }
        if ((ioHdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
            return;
        }

        // Check if CD Read feature is supported
        size_t pos = 8;
        while (pos < buffer.size() && pos < bufferLength + 4) {
            const auto featureCode = util::ReadBE<uint16>(&buffer[pos]);
            const auto additionalLength = buffer[pos + 3];

            if (featureCode == kSCSIFeatureCDRead) {
                features.readCDCommand = true;
                break;
            }

            pos += 4 + additionalLength;
        }
    }

    void ReadTOC() {
        tocEntries.clear();

        if (!IsOpen()) {
            return;
        }

        std::vector<uint8> buffer{};
        buffer.resize(8);

        // Build Read TOC command
        std::array<uint8, 10> cdb{};
        cdb[0] = kSCSIOperationReadTOC;
        cdb[1] = 0; // Return LBA addresses
        cdb[2] = 0; // Standard TOC
        cdb[6] = 0; // Start from the first track
        util::WriteBE<uint16>(&cdb[7], buffer.size());
        cdb[9] = 0;

        // Execute once to get required buffer size
        sg_io_hdr_t ioHdr{};
        std::array<uint8, 96> senseBuffer{};
        ioHdr.interface_id = 'S';
        ioHdr.dxfer_direction = SG_DXFER_FROM_DEV;
        ioHdr.cmd_len = sizeof(cdb);
        ioHdr.mx_sb_len = sizeof(senseBuffer);
        ioHdr.dxfer_len = buffer.size();
        ioHdr.dxferp = buffer.data();
        ioHdr.cmdp = cdb.data();
        ioHdr.sbp = senseBuffer.data();
        ioHdr.timeout = 3000;
        if (ioctl(fd, SG_IO, &ioHdr) < 0) {
            return;
        }
        if ((ioHdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
            return;
        }

        // Redo the request with a buffer large enough to fit the data
        const auto bufferLength = util::ReadBE<uint16>(&buffer[0]);
        buffer.resize(bufferLength + 2);
        util::WriteBE<uint16>(&cdb[7], buffer.size());
        ioHdr.dxfer_len = buffer.size();
        ioHdr.dxferp = buffer.data();
        if (ioctl(fd, SG_IO, &ioHdr) < 0) {
            return;
        }
        if ((ioHdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
            return;
        }

        const uint8 firstTrackNum = buffer[2];
        const uint8 lastTrackNum = buffer[3];

        // Convert to TOCEntry list

        // Point A0 - first data track
        {
            auto &tocEntry = tocEntries.emplace_back();
            tocEntry.controlADR = 0x41;
            tocEntry.trackNum = 0x00;
            tocEntry.pointOrIndex = 0xA0;
            tocEntry.min = 0x00;
            tocEntry.sec = 0x00;
            tocEntry.frac = 0x00;
            tocEntry.zero = 0x00;
            tocEntry.amin = util::to_bcd(firstTrackNum);
            tocEntry.asec = 0x00;
            tocEntry.afrac = 0x00;
        }

        // Point A1 - last data track
        {
            auto &tocEntry = tocEntries.emplace_back();
            tocEntry.controlADR = 0x41;
            tocEntry.trackNum = 0x00;
            tocEntry.pointOrIndex = 0xA1;
            tocEntry.min = 0x00;
            tocEntry.sec = 0x00;
            tocEntry.frac = 0x00;
            tocEntry.zero = 0x00;
            tocEntry.amin = util::to_bcd(lastTrackNum);
            tocEntry.asec = 0x00;
            tocEntry.afrac = 0x00;
        }

        // Point A2 - start of leadout track
        // Filled in the loop below
        tocEntries.emplace_back();

        // Tracks
        size_t pos = 4;
        const size_t totalSize = bufferLength + 2;
        while (pos + 8 <= totalSize) {
            const uint8 *trackData = &buffer[pos];

            const uint8 control = bit::extract<0, 3>(trackData[1]);
            const uint8 adr = bit::extract<4, 7>(trackData[1]);
            const uint8 trackNum = trackData[2];
            const uint32 fad = util::ReadBE<uint32>(&trackData[4]) + 150;

            if (trackNum == 0xAA) {
                auto &leadoutEntry = tocEntries[2];
                leadoutEntry.controlADR = 0x41;
                leadoutEntry.trackNum = 0x00;
                leadoutEntry.pointOrIndex = 0xA2;
                leadoutEntry.min = 0x00;
                leadoutEntry.sec = 0x00;
                leadoutEntry.frac = 0x00;
                leadoutEntry.zero = 0x00;
                leadoutEntry.amin = util::to_bcd(fad / 75 / 60);
                leadoutEntry.asec = util::to_bcd(fad / 75 % 60);
                leadoutEntry.afrac = util::to_bcd(fad % 75);
            } else {
                const uint32 relFAD = 0; // TODO: find in image
                auto &tocEntry = tocEntries.emplace_back();
                tocEntry.controlADR = (control << 4u) | adr;
                tocEntry.trackNum = 0x00;
                tocEntry.pointOrIndex = util::to_bcd(trackNum);
                tocEntry.min = util::to_bcd(relFAD / 75 / 60);
                tocEntry.sec = util::to_bcd(relFAD / 75 % 60);
                tocEntry.frac = util::to_bcd(relFAD % 75);
                tocEntry.zero = 0x00;
                tocEntry.amin = util::to_bcd(fad / 75 / 60);
                tocEntry.asec = util::to_bcd(fad / 75 % 60);
                tocEntry.afrac = util::to_bcd(fad % 75);
            }
            pos += 8;
        }
    }

private:
    // Main sector read function using READ CD
    FORCE_INLINE size_t ReadRawSectorSCSIReadCD(uint32 frameAddress, std::span<uint8, 2352> out) {
        assert(IsOpen());
        assert(out.size() >= 2352);
        std::fill(out.begin(), out.end(), 0u);

        // READ CD CDB structure:
        // bytes  description
        //     0  Command operation code (=0xBE)
        //     1  Flags
        //          bits  description
        //             0  Relative addressing
        //             1  (reserved)
        //           2-4  Expected sector type
        //                  000 (0) = Any type
        //                  001 (1) = CDDA
        //                  010 (2) = Mode 1
        //                  011 (3) = Mode 2
        //                  100 (4) = Mode 2 Form 1
        //                  101 (5) = Mode 2 Form 2
        //                  110 (6) = (reserved)
        //                  111 (7) = (reserved)
        //           5-7  LUN
        //   2-5  Starting LBA
        //   6-8  Transfer length in sectors
        //     9  Expected sector type and format
        //          bits  description
        //             0  (reserved)
        //           1-2  Error flags
        //                  00 (0) = None
        //                  01 (1) = C2 Error Flag data
        //                  10 (2) = C2 and Block Error Flags
        //                  11 (3) = (reserved)
        //             3  EDC and ECC
        //             4  User data
        //           5-6  Header(s) code
        //                  00 (0) = None
        //                  01 (1) = Mode 1 or Mode 2 Form 1 4-byte header only
        //                  10 (2) = Mode 2 Form 1 or Form 2 subheader only
        //                  11 (3) = Header and subheader
        //             7  Sync field
        //    10  Subchannel selection (bits 0-2 only; others are reserved)
        //          000 (0) = None
        //          001 (1) = Raw subchannel data
        //          010 (2) = Q subchannel
        //          011 (3) = (reserved)
        //          100 (4) = R to W subchannels
        //          101 (5) = (reserved)
        //          110 (6) = (reserved)
        //          111 (7) = (reserved)
        std::array<uint8, 12> cdb{};
        cdb[0] = kSCSIOperationReadCD;
        cdb[1] = 0x00;
        cdb[2] = frameAddress >> 24u;
        cdb[3] = frameAddress >> 16u;
        cdb[4] = frameAddress >> 8u;
        cdb[5] = frameAddress >> 0u;
        cdb[6] = 0x00;
        cdb[7] = 0x00;
        cdb[8] = 0x01;
        cdb[9] = 0xF8;
        cdb[10] = 0x00;
        cdb[11] = 0x00;

        sg_io_hdr_t ioHdr{};
        std::array<uint8, 96> senseBuffer{};
        ioHdr.interface_id = 'S';
        ioHdr.dxfer_direction = SG_DXFER_FROM_DEV;
        ioHdr.cmd_len = sizeof(cdb);
        ioHdr.mx_sb_len = sizeof(senseBuffer);
        ioHdr.dxfer_len = out.size();
        ioHdr.dxferp = out.data();
        ioHdr.cmdp = cdb.data();
        ioHdr.sbp = senseBuffer.data();
        ioHdr.timeout = 5000;
        if (ioctl(fd, SG_IO, &ioHdr) < 0) {
            return 0;
        }
        if ((ioHdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
            return 0;
        }

        return ioHdr.dxfer_len - ioHdr.resid;
    }

    // Fallback sector read function relying on READ (10). Can't read raw data sectors, only the user data area.
    FORCE_INLINE size_t ReadRawSectorSCSIRead10(uint32 frameAddress, std::span<uint8, 2352> out) {
        assert(IsOpen());
        assert(out.size() >= 2352);
        std::fill(out.begin(), out.end(), 0u);

        // READ (10) CDB structure:
        // bytes  description
        //     0  Command operation code (=0x28)
        //     1  Flags
        //   2-5  Starting LBA
        //     6  Group number
        //   7-8  Transfer length in sectors
        //     9  Control byte
        std::array<uint8, 10> cdb{};
        cdb[0] = kSCSIOperationRead10;
        cdb[1] = 0x00;
        cdb[2] = frameAddress >> 24u;
        cdb[3] = frameAddress >> 16u;
        cdb[4] = frameAddress >> 8u;
        cdb[5] = frameAddress >> 0u;
        cdb[6] = 0x00;
        cdb[7] = 0x00;
        cdb[8] = 0x01;
        cdb[9] = 0x00;

        sg_io_hdr_t ioHdr{};
        std::array<uint8, 96> senseBuffer{};
        ioHdr.interface_id = 'S';
        ioHdr.dxfer_direction = SG_DXFER_FROM_DEV;
        ioHdr.cmd_len = sizeof(cdb);
        ioHdr.mx_sb_len = sizeof(senseBuffer);
        ioHdr.dxfer_len = out.size();
        ioHdr.dxferp = out.data();
        ioHdr.cmdp = cdb.data();
        ioHdr.sbp = senseBuffer.data();
        ioHdr.timeout = 5000;
        if (ioctl(fd, SG_IO, &ioHdr) < 0) {
            return 0;
        }
        if ((ioHdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
            return 0;
        }

        return ioHdr.dxfer_len - ioHdr.resid;
    }
};

std::vector<std::string> PhysicalCDDevice::EnumerateDevices() {
    namespace fs = std::filesystem;

    // Iterate over generic SCSI devices looking for type 5 (ROM, as in CD-ROM or DVD-ROM)
    const fs::path sgPath = "/sys/class/scsi_generic/";
    if (!fs::exists(sgPath)) {
        // Can't find path, bail out
        YMIR_DEV_CHECK(); // really shouldn't happen, but wouldn't be surprised if it did happen
        return {};
    }

    std::vector<std::string> drives{};

    for (const auto &devEntry : fs::directory_iterator(sgPath)) {
        std::string devName = devEntry.path().filename().string();

        fs::path typePath = devEntry.path() / "device/type";
        if (!fs::exists(typePath)) {
            continue;
        }

        std::ifstream typeFile(typePath);
        int deviceType = -1;
        if (typeFile >> deviceType) {
            if (deviceType == 5) {
                drives.push_back("/dev/" + devName);
            }
        }
    }

    return drives;
}

PhysicalCDDevice::PhysicalCDDevice()
    : m_context(std::make_unique<Context>()) {}

PhysicalCDDevice::~PhysicalCDDevice() {
    if (m_context->IsOpen()) {
        close(m_context->fd);
    }
}

CDDeviceOpenResult PhysicalCDDevice::Open(std::string devicePath) {
    // Try opening the device
    int fd = open(devicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return CDDeviceOpenResult::Failed(fmt::format("Failed to open device: {}", std::strerror(errno)));
    }
    util::ScopeGuard sgCloseFD{[&] { close(fd); }};

    // Make sure this is a CD drive by sending the SCSI INQUIRY command
    std::array<uint8, 36> resultBuffer{};
    std::array<uint8, 96> senseBuffer{};

    std::array<uint8, 6> cdb{};
    cdb[0] = kSCSIOperationInquiry;
    cdb[1] = 0x00;
    cdb[2] = 0x00;
    cdb[3] = resultBuffer.size() >> 8u;
    cdb[4] = resultBuffer.size() >> 0u;
    cdb[5] = 0x00;

    sg_io_hdr_t ioHdr{};
    ioHdr.interface_id = 'S';
    ioHdr.dxfer_direction = SG_DXFER_FROM_DEV;
    ioHdr.cmd_len = sizeof(cdb);
    ioHdr.mx_sb_len = sizeof(senseBuffer);
    ioHdr.dxfer_len = resultBuffer.size();
    ioHdr.dxferp = resultBuffer.data();
    ioHdr.cmdp = cdb.data();
    ioHdr.sbp = senseBuffer.data();
    ioHdr.timeout = 3000;
    if (ioctl(fd, SG_IO, &ioHdr) < 0) {
        return CDDeviceOpenResult::Failed(fmt::format("Failed to query CD drive status: {}", std::strerror(errno)));
    }
    if ((ioHdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
        return CDDeviceOpenResult::Failed(
            fmt::format("Failed to query CD drive status. Status codes: host={}, driver={}", ioHdr.host_status,
                        ioHdr.driver_status));
    }
    if ((resultBuffer[0] & 0x1F) != 5) {
        return CDDeviceOpenResult::Failed(fmt::format("{} is not a CD drive", devicePath));
    }

    // All good at this point, no longer need to clean up the file descriptor
    sgCloseFD.Cancel();

    if (m_context->IsOpen()) {
        close(m_context->fd);
    }
    m_context->fd = fd;
    m_context->UpdateSupportedFeatures();
    m_context->ReadTOC(); // TODO: might have to move to a thread

    return CDDeviceOpenResult::Succeeded();
}

void PhysicalCDDevice::Close() {
    if (m_context->IsOpen()) {
        close(m_context->fd);
        m_context->fd = -1;
    }
}

std::span<const TOCEntry> PhysicalCDDevice::GetTOC() {
    if (!m_context->IsOpen()) {
        return {};
    }
    return m_context->tocEntries;
}

size_t PhysicalCDDevice::ReadRawSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) {
    return m_context->ReadRawSector(frameAddress, out);
}

} // namespace ymir::media
