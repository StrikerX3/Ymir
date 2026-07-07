#include <ymir/media/cd_interface/cd_interface_base.hpp>

#include <ymir/media/cd_utils.hpp>

namespace ymir::media {

bool ICDInterface::ReadSector(uint32 frameAddress, std::span<uint8, 2352> out) {
    const uint32 readSize = ReadSectorImpl(frameAddress, out);
    if (readSize < 2048) {
        // Could not read the bare minimum; fail
        return false;
    }

    // Some implementations may not always be able to fully read raw sector data from particular CD devices, typically
    // due to driver, firmware or emulation limitations. For instance, hypervisor software tend to not fully emulate
    // optical disc drives when reading from ISOs. This manifests as lack of support for the READ CD SCSI command,
    // forcing implementations to fall back to READ (10) command that are limited to the 2048-byte user data area only.
    // They can sometimes read full audio sectors (2352 bytes), but this is not guaranteed.
    //
    // If the read did not return a full raw sector, we'll have to synthesize the rest of the sector from whatever data
    // we could gather. Unfortunately, we can't determine if the track is Mode 1 or Mode 2 Form 1 from the size alone,
    // but we can at least know for sure that the track is Mode 2 Form 2 if the command has read 2336 bytes.
    //
    // This is only an issue with data tracks, so we can safely assume the Control/ADR bits are always 0x41.
    if (readSize < 2352) {
        const bool mode2 = readSize >= 2336;
        SynthesizeSectorData(out, readSize, frameAddress + 150, 0x41, mode2);
    }
    return true;
}

} // namespace ymir::media
