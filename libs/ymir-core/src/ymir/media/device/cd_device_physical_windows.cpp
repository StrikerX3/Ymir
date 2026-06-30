/**
@file
@brief Implementation of physical CD devices for Windows.
*/

#include <ymir/media/device/cd_device_physical.hpp>

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/inline.hpp>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

// Must come before the ntdd*.h headers
#include <winioctl.h>

#include <ntddcdrm.h>
#include <ntddmmc.h>
#include <ntddscsi.h>

#include <fmt/format.h>

#include <vector>

namespace ymir::media {

static constexpr uint8 kSCSIOperationReadCD = 0xBE;
static constexpr uint8 kSCSIOperationRead10 = 0x28;

struct SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER {
    SCSI_PASS_THROUGH_DIRECT sptd;
    alignas(8) std::array<UCHAR, 96> senseBuf;
};

struct PhysicalCDDevice::Context {
    HANDLE hDrive = INVALID_HANDLE_VALUE;

    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER scsiParams;
    CDROM_TOC rawTOCData;

    std::vector<TOCEntry> tocEntries;

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

    FORCE_INLINE bool IsOpen() const {
        return hDrive != INVALID_HANDLE_VALUE;
    }

    FLATTEN FORCE_INLINE size_t ReadRawSector(ULONG frameAddress, std::span<uint8, 2352> out) {
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

        GET_CONFIGURATION_IOCTL_INPUT input;
        input.Feature = FeatureCdRead;
        input.RequestType = SCSI_GET_CONFIGURATION_REQUEST_TYPE_CURRENT;
        input.Reserved[0] = 0;
        input.Reserved[1] = 0;

        std::array<BYTE, 32> buffer{};
        DWORD bytesReturned = 0;
        if (!DeviceIoControl(hDrive, IOCTL_CDROM_GET_CONFIGURATION, &input, sizeof(input), buffer.data(), buffer.size(),
                             &bytesReturned, NULL)) {
            return;
        }
        if (bytesReturned < sizeof(GET_CONFIGURATION_HEADER)) {
            return;
        }

        const auto header = reinterpret_cast<PGET_CONFIGURATION_HEADER>(buffer.data());
        const auto feature = reinterpret_cast<PFEATURE_HEADER>(buffer.data() + sizeof(GET_CONFIGURATION_HEADER));
        const USHORT featureCode = (feature->FeatureCode[0] << 8) | feature->FeatureCode[1];
        if (featureCode == FeatureCdRead) {
            features.readCDCommand = true;
        }
    }

    void ReadTOC() {
        RtlZeroMemory(&rawTOCData, sizeof(rawTOCData));
        tocEntries.clear();

        CDROM_READ_TOC_EX tocRequest = {0};
        tocRequest.Format = CDROM_READ_TOC_EX_FORMAT_TOC;
        tocRequest.SessionTrack = 1;
        tocRequest.Msf = 0;

        DWORD bytesReturned = 0;
        if (DeviceIoControl(hDrive, IOCTL_CDROM_READ_TOC_EX, &tocRequest, sizeof(tocRequest), &rawTOCData,
                            sizeof(rawTOCData), &bytesReturned, NULL)) {
            // Convert to TOCEntry list.
            // Order and format:
            //   point  TNO ctl ADR  M  S  F  zero  PMin             PSec       PFrame
            //      A0  00  4/6   1  00 00 00  00   first track num  disc type  00
            //      A1  00  4/6   1  00 00 00  00   last track num   00         00
            //      A2  00  4/6   1  00 00 00  00   --- start FAD of lead-out area ---
            //   01-99* 00  4/6   1  rel.FAD   00   ------- start FAD of track -------
            //   (other entries exist but are not needed)
            // * binary-coded decimal format

            const uint32 numTracks = rawTOCData.LastTrack - rawTOCData.FirstTrack + 1;

            // Point A0 - first data track
            {
                auto &tocEntry = tocEntries.emplace_back();
                tocEntry.controlADR = 0x41;
                tocEntry.trackNum = 0x00;
                tocEntry.pointOrIndex = 0xA0;
                tocEntry.min = util::to_bcd(150 / 75 / 60);
                tocEntry.sec = util::to_bcd(150 / 75 % 60);
                tocEntry.frac = util::to_bcd(150 % 75);
                tocEntry.zero = 0x00;
                tocEntry.amin = util::to_bcd(rawTOCData.FirstTrack);
                tocEntry.asec = 0x00;
                tocEntry.afrac = 0x00;
            }

            // Point A1 - last data track
            {
                auto &tocEntry = tocEntries.emplace_back();
                tocEntry.controlADR = 0x41;
                tocEntry.trackNum = 0x00;
                tocEntry.pointOrIndex = 0xA1;
                tocEntry.min = util::to_bcd(150 / 75 / 60);
                tocEntry.sec = util::to_bcd(150 / 75 % 60);
                tocEntry.frac = util::to_bcd(150 % 75);
                tocEntry.zero = 0x00;
                tocEntry.amin = util::to_bcd(rawTOCData.LastTrack);
                tocEntry.asec = 0x00;
                tocEntry.afrac = 0x00;
            }

            // Point A2 - start of leadout track
            {
                // Last track in TrackData is the leadout track
                const uint32 leadOutFAD = util::ReadBE<uint32>(rawTOCData.TrackData[numTracks].Address);
                auto &tocEntry = tocEntries.emplace_back();
                tocEntry.controlADR = 0x41;
                tocEntry.trackNum = 0x00;
                tocEntry.pointOrIndex = 0xA2;
                tocEntry.min = util::to_bcd(150 / 75 / 60);
                tocEntry.sec = util::to_bcd(150 / 75 % 60);
                tocEntry.frac = util::to_bcd(150 % 75);
                tocEntry.zero = 0x00;
                tocEntry.amin = util::to_bcd(leadOutFAD / 75 / 60);
                tocEntry.asec = util::to_bcd(leadOutFAD / 75 % 60);
                tocEntry.afrac = util::to_bcd(leadOutFAD % 75);
            }

            // Tracks
            for (int i = 0; i < numTracks; i++) {
                auto &track = rawTOCData.TrackData[i];

                const uint32 fad = util::ReadBE<uint32>(track.Address);
                const uint32 relFAD = fad - 150;
                auto &entry = tocEntries.emplace_back();
                entry.controlADR = (track.Control << 4u) | track.Adr;
                entry.trackNum = 0x00;
                entry.pointOrIndex = util::to_bcd(i + 1);
                entry.min = util::to_bcd(relFAD / 75 / 60);
                entry.sec = util::to_bcd(relFAD / 75 % 60);
                entry.frac = util::to_bcd(relFAD % 75);
                entry.zero = 0x00;
                entry.amin = util::to_bcd(fad / 75 / 60);
                entry.asec = util::to_bcd(fad / 75 % 60);
                entry.afrac = util::to_bcd(fad % 75);
            }
        }
    }

private:
    // Main sector read function using READ CD
    FORCE_INLINE size_t ReadRawSectorSCSIReadCD(ULONG frameAddress, std::span<uint8, 2352> out) {
        assert(IsOpen());
        assert(out.size() >= 2352);
        std::fill(out.begin(), out.end(), 0u);

        RtlZeroMemory(&scsiParams, sizeof(scsiParams));

        scsiParams.sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
        scsiParams.sptd.CdbLength = 12;
        scsiParams.sptd.DataIn = SCSI_IOCTL_DATA_IN;
        scsiParams.sptd.DataTransferLength = out.size();
        scsiParams.sptd.TimeOutValue = 5;
        scsiParams.sptd.DataBuffer = out.data();
        scsiParams.sptd.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, senseBuf);
        scsiParams.sptd.SenseInfoLength = scsiParams.senseBuf.size();

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
        scsiParams.sptd.Cdb[0] = kSCSIOperationReadCD;
        scsiParams.sptd.Cdb[1] = 0x00;
        scsiParams.sptd.Cdb[2] = frameAddress >> 24u;
        scsiParams.sptd.Cdb[3] = frameAddress >> 16u;
        scsiParams.sptd.Cdb[4] = frameAddress >> 8u;
        scsiParams.sptd.Cdb[5] = frameAddress >> 0u;
        scsiParams.sptd.Cdb[6] = 0x00;
        scsiParams.sptd.Cdb[7] = 0x00;
        scsiParams.sptd.Cdb[8] = 0x01;
        scsiParams.sptd.Cdb[9] = 0xF8;
        scsiParams.sptd.Cdb[10] = 0x00;
        scsiParams.sptd.Cdb[11] = 0x00;

        DWORD bytesReturned = 0;
        if (!DeviceIoControl(hDrive, IOCTL_SCSI_PASS_THROUGH_DIRECT, &scsiParams, sizeof(scsiParams), &scsiParams,
                             sizeof(scsiParams), &bytesReturned, NULL)) {
            return 0;
        }
        if (scsiParams.sptd.ScsiStatus != 0x00) {
            return 0;
        }

        assert(scsiParams.sptd.DataTransferLength == 2352);
        return scsiParams.sptd.DataTransferLength;
    }

    // Fallback sector read function relying on READ (10). Can't read raw data sectors, only the user data area.
    FORCE_INLINE size_t ReadRawSectorSCSIRead10(ULONG frameAddress, std::span<uint8, 2352> out) {
        assert(IsOpen());
        assert(out.size() >= 2352);
        std::fill(out.begin(), out.end(), 0u);

        RtlZeroMemory(&scsiParams, sizeof(scsiParams));

        scsiParams.sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
        scsiParams.sptd.CdbLength = 10;
        scsiParams.sptd.DataIn = SCSI_IOCTL_DATA_IN;
        scsiParams.sptd.DataTransferLength = out.size();
        scsiParams.sptd.TimeOutValue = 5;
        scsiParams.sptd.DataBuffer = out.data() + 0x10; // write to user data area
        scsiParams.sptd.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, senseBuf);
        scsiParams.sptd.SenseInfoLength = scsiParams.senseBuf.size();

        // READ (10) CDB structure:
        // bytes  description
        //     0  Command operation code (=0x28)
        //     1  Flags
        //   2-5  Starting LBA
        //     6  Group number
        //   7-8  Transfer length in sectors
        //     9  Control byte
        scsiParams.sptd.Cdb[0] = kSCSIOperationRead10;
        scsiParams.sptd.Cdb[1] = 0x00;
        scsiParams.sptd.Cdb[2] = frameAddress >> 24u;
        scsiParams.sptd.Cdb[3] = frameAddress >> 16u;
        scsiParams.sptd.Cdb[4] = frameAddress >> 8u;
        scsiParams.sptd.Cdb[5] = frameAddress >> 0u;
        scsiParams.sptd.Cdb[6] = 0x00;
        scsiParams.sptd.Cdb[7] = 0x00;
        scsiParams.sptd.Cdb[8] = 0x01;
        scsiParams.sptd.Cdb[9] = 0x00;

        DWORD bytesReturned = 0;
        if (!DeviceIoControl(hDrive, IOCTL_SCSI_PASS_THROUGH_DIRECT, &scsiParams, sizeof(scsiParams), &scsiParams,
                             sizeof(scsiParams), &bytesReturned, NULL)) {
            return 0;
        }
        if (scsiParams.sptd.ScsiStatus != 0x00) {
            return 0;
        }

        return scsiParams.sptd.DataTransferLength;
    }
};

std::vector<std::string> ymir::media::PhysicalCDDevice::EnumerateDevices() {
    std::vector<std::string> drives{};

    const DWORD driveMask = GetLogicalDrives();

    char driveRoot[] = {'_', ':', '\0'};
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        const int index = letter - 'A';
        if (driveMask & (1 << index)) {
            driveRoot[0] = letter;
            const UINT driveType = GetDriveTypeA(driveRoot);
            if (driveType == DRIVE_CDROM) {
                drives.push_back(driveRoot);
            }
        }
    }

    return drives;
}

PhysicalCDDevice::PhysicalCDDevice()
    : m_context(std::make_unique<Context>()) {}

PhysicalCDDevice::~PhysicalCDDevice() {
    if (m_context->IsOpen()) {
        CloseHandle(m_context->hDrive);
    }
}

CDDeviceOpenResult PhysicalCDDevice::Open(std::string devicePath) {
    // Try to sanitize device path ("D:\" -> "D:")
    if (devicePath.ends_with("\\")) {
        devicePath = devicePath.substr(0, devicePath.size() - 1);
    }

    // Check device path format; must be drive letter followed by colon and nothing else
    if (devicePath.size() != 2 || devicePath[0] < 'A' || devicePath[0] > 'Z' || devicePath[1] != ':') {
        return CDDeviceOpenResult::Failed(fmt::format("Invalid device path: {}", devicePath));
    }

    // Check that this is actually a CD drive
    const UINT driveType = GetDriveTypeA(devicePath.c_str());
    if (driveType != DRIVE_CDROM) {
        return CDDeviceOpenResult::Failed(fmt::format("{} is not a CD drive", devicePath));
    }

    // Attempt to open the device
    std::string fullDevicePath = "\\\\.\\" + devicePath;
    HANDLE hDrive = CreateFileA(fullDevicePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDrive == INVALID_HANDLE_VALUE) {
        const DWORD errorCode = GetLastError();

        LPSTR messageBuffer = nullptr;
        const size_t size =
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

        std::string message(messageBuffer, size);

        LocalFree(messageBuffer);

        return CDDeviceOpenResult::Failed(message);
    }

    // Success; close current device and use new device
    if (m_context->IsOpen()) {
        CloseHandle(m_context->hDrive);
    }
    m_context->hDrive = hDrive;
    m_context->UpdateSupportedFeatures();
    m_context->ReadTOC(); // TODO: might have to move to a thread

    return CDDeviceOpenResult::Succeeded();
}

void PhysicalCDDevice::Close() {
    if (m_context->IsOpen()) {
        CloseHandle(m_context->hDrive);
        m_context->hDrive = INVALID_HANDLE_VALUE;
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
