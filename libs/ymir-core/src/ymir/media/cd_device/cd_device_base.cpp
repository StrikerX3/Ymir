#include <ymir/media/cd_device/cd_device_base.hpp>

#include <ymir/media/cd_utils.hpp>

#include <ymir/util/bit_ops.hpp>

namespace ymir::media {

bool ICDDevice::ReadSector(uint32 frameAddress, std::span<uint8, 2352> outSector) {
    const uint32 readSize = ReadSectorImpl(frameAddress, outSector);
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
        SynthesizeSectorData(outSector, readSize, frameAddress + 150, 0x41, mode2);
    }
    return true;
}

void ICDDevice::BeginSeekToFrameAddress(uint32 frameAddress) {
    m_seekTarget = frameAddress;
    BeginSeekToFrameAddressImpl(frameAddress);
}

void ICDDevice::BeginSeekToTrackIndex(uint8 track, uint8 index) {
    m_seekTarget = 0x80000000 | (track << 8u) | index;
    BeginSeekToTrackIndexImpl(track, index);
}

void ICDDevice::SaveState(savestate::CDInterfaceSaveState &state) const {
    state.seekTarget = m_seekTarget;
    state.seekDone = IsSeekDone();
    state.seekFAD = GetSeekFrameAddress();
}

bool ICDDevice::ValidateState(const savestate::CDInterfaceSaveState &state) const {
    return true;
}

void ICDDevice::LoadState(const savestate::CDInterfaceSaveState &state) {
    ReconcileSeekState(state);
}

void ICDDevice::ReconcileSeekState(const savestate::CDInterfaceSaveState &state) {
    m_seekTarget = state.seekTarget;

    // Don't request a seek if the device has completed one and its seek state agrees with the save state
    const bool currSeekDone = IsSeekDone();
    const bool seekFADMatches = GetSeekFrameAddress() == state.seekFAD;
    if (currSeekDone && state.seekDone && seekFADMatches) {
        return;
    }

    // At this point we have one of the following situations:
    // - The device has completed a seek, but the seek FADs disagree
    // - The device is currently seeking to an unknown address
    // Either way, we'll have to request the device to seek again.
    // Implementations may coalesce repeated seeks to the same target to reduce strain on hardware.

    const bool isTrackIndex = bit::test<31>(m_seekTarget);
    if (isTrackIndex) {
        const uint8 track = bit::extract<8, 15>(m_seekTarget);
        const uint8 index = bit::extract<0, 7>(m_seekTarget);
        BeginSeekToTrackIndexImpl(track, index);
    } else {
        const uint32 frameAddress = bit::extract<0, 23>(m_seekTarget);
        BeginSeekToFrameAddressImpl(frameAddress);
    }
}

} // namespace ymir::media
