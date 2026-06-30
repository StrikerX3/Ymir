#include <ymir/media/device/cd_device_base.hpp>

#include <ymir/media/cd_utils.hpp>

namespace ymir::media {

bool ICDDevice::ReadRawSector(uint32 frameAddress, std::span<uint8, 2352> out) {
    // Ensure the buffer is large enough
    if (out.size() < 2352) {
        return false;
    }

    const size_t readSize = ReadRawSectorImpl(frameAddress, out);
    if (readSize < 2048) {
        // Could not read the bare minimum; fail
        return false;
    }

    // Some implementations may not always be able to fully read raw sector data from particular CD devices, typically
    // due to driver, firmware or emulation limitations. For instance, hypervisor software tend to not fully emulate
    // optical disc drives when reading from ISOs. This manifests as lack of support for the READ CD SCSI command,
    // forcing implementations to fall back to READ (10) or READ (12) commands that are limited to the user data area
    // only -- 2048 bytes on Mode 1 and Mode 2 Form 1 tracks, 2336 bytes on Mode 2 Form 2 tracks, and 2352 bytes for
    // audio tracks, if these can be read at all.
    //
    // If the read did not return a full raw sector, we'll have to synthesize the rest of the sector from whatever data
    // we could gather. Unfortunately, we can't determine if the track is Mode 1 or Mode 2 Form 1 from the size alone,
    // but we can at least know for sure that the track is Mode 2 Form 2 if the command has read 2336 bytes.
    if (readSize < 2352) {
        const bool mode2 = readSize >= 2336;
        SynthesizeSectorData(out, readSize, frameAddress + 150, 0x41, mode2);
    }

    return true;
}

} // namespace ymir::media
