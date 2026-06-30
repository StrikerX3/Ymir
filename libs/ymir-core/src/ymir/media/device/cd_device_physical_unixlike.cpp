/**
@file
@brief Implementation of physical CD devices for Unix-like systems (Linux, FreeBSD and such, except macOS).
*/

#include "ymir/util/data_ops.hpp"

#include <ymir/media/device/cd_device_physical.hpp>

#include <ymir/util/dev_assert.hpp>

#include <fcntl.h>
#include <linux/cdrom.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>

#include <vector>

namespace ymir::media {

static constexpr uint8 kSCSIOperationGetConfiguration = 0x46;
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

    bool IsOpen() const {
        return fd >= 0;
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
        cdb[1] = 2;                                         // Request one features
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
        const uint32 bufferLength = util::ReadBE<uint32>(&buffer[0]);
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
        if (!IsOpen()) {
            return;
        }

        cdrom_tochdr header;
        if (ioctl(fd, CDROMREADTOCHDR, &header) < 0) {
            close(fd);
        }
    }
};

std::vector<std::string> PhysicalCDDevice::EnumerateDevices() {
    namespace fs = std::filesystem;

    // Iterate over /sys/block devices looking for devices with SCSI type 5 (ROM, as in CD-ROM or DVD-ROM)
    const fs::path sysBlockPath = "/sys/block";
    if (!fs::exists(sysBlockPath)) {
        // Can't find /sys/block, bail out
        YMIR_DEV_CHECK(); // really shouldn't happen, but wouldn't be surprised if it did happen
        return {};
    }

    std::vector<std::string> drives{};

    for (const auto &devEntry : fs::directory_iterator(sysBlockPath)) {
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

    // Make sure this is a CD drive
    int result = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_NONE);
    if (result < 0) {
        return CDDeviceOpenResult::Failed(fmt::format("Failed to query CD drive status: {}", std::strerror(errno)));
    }
    switch (result) {
    case CDS_NO_INFO: [[fallthrough]];
    case CDS_NO_DISC: [[fallthrough]];
    case CDS_TRAY_OPEN: [[fallthrough]];
    case CDS_DRIVE_NOT_READY: [[fallthrough]];
    case CDS_DISC_OK: break;
    default: return CDDeviceOpenResult::Failed(fmt::format("{} is not a CD drive", devicePath));
    }

    // TODO: check if the drive supports the SCSI READ CD command
    // TODO: read TOC (might have to run in a thread)
    if (m_context->IsOpen()) {
        close(m_context->fd);
    }
    m_context->fd = fd;
    m_context->UpdateSupportedFeatures();
    m_context->ReadTOC();

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
    // TODO: attempt to read sector; return actual number of bytes read, or 0 if failed
    return 0;
}

} // namespace ymir::media
