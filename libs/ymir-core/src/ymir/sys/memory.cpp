#include <ymir/sys/memory.hpp>

#include "null_program.hpp"

namespace ymir::sys {

SystemMemory::SystemMemory() {
    nullprog::CopyNullProgram(IPL);
    Reset(true);
}

void SystemMemory::Reset(bool hard) {
    if (hard) {
        WRAMLow.fill(0);
        WRAMHigh.fill(0);
    }
}

void SystemMemory::MapMemory(SH2Bus &bus) {
    bus.MapArray(0x000'0000, 0x00F'FFFF, IPL, false);
    m_internalBackupRAM.MapMemory(bus, 0x018'0000, 0x01F'FFFF);
    bus.MapArray(0x020'0000, 0x02F'FFFF, WRAMLow, true);
    bus.MapArray(0x600'0000, 0x7FF'FFFF, WRAMHigh, true);

    // TODO: make this configurable
    // VA0/VA1: 030'0000 is unmapped; reads return all ones
    //     VA2: 030'0000 is a read/write mirror of 020'0000
    bus.MapBoth(
        0x030'0000, 0x03F'FFFF, nullptr, [](uint32, void *) -> uint8 { return 0xFF; },
        [](uint32, void *) -> uint16 { return 0xFFFF; }, [](uint32, void *) -> uint32 { return 0xFFFFFFFF; });

    bus.SetAccessCycles(0x000'0000, 0x00F'FFFF, 8, 8);
    bus.SetAccessCycles(0x018'0000, 0x01F'FFFF, 8, 8);
    bus.SetAccessCycles(0x020'0000, 0x02F'FFFF, 7, 7);
    bus.SetAccessCycles(0x600'0000, 0x7FF'FFFF, 7, 2);
}

void SystemMemory::LoadIPL(std::span<uint8, kIPLSize> ipl) {
    std::copy(ipl.begin(), ipl.end(), IPL.begin());
    m_iplHash = CalcHash128(IPL.data(), IPL.size(), kIPLHashSeed);
}

XXH128Hash SystemMemory::GetIPLHash() const {
    return m_iplHash;
}

void SystemMemory::LoadInternalBackupMemoryImage(std::filesystem::path path, std::error_code &error) {
    m_internalBackupRAM.CreateFrom(path, kInternalBackupRAMSize, error);
}

void SystemMemory::DumpWRAMLow(std::ostream &out) const {
    out.write((const char *)WRAMLow.data(), WRAMLow.size());
}

void SystemMemory::DumpWRAMHigh(std::ostream &out) const {
    out.write((const char *)WRAMHigh.data(), WRAMHigh.size());
}

void SystemMemory::SaveState(state::SystemState &state) const {
    state.iplRomHash = m_iplHash;
    state.WRAMLow = WRAMLow;
    state.WRAMHigh = WRAMHigh;
}

bool SystemMemory::ValidateState(const state::SystemState &state, bool skipROMChecks) const {
    if (!skipROMChecks && state.iplRomHash != m_iplHash) {
        return false;
    }
    return true;
}

void SystemMemory::LoadState(const state::SystemState &state) {
    WRAMLow = state.WRAMLow;
    WRAMHigh = state.WRAMHigh;
}

} // namespace ymir::sys
