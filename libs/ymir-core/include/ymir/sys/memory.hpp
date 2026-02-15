#pragma once

/**
@file
@brief Definitions for Sega Saturn system memory components.

The Sega Saturn contains a 512 KiB IPL ROM (also called BIOS ROM), two Work RAM blocks (low and high) of 1 MiB each
comprising the main system memory and 32 KiB of internal backup memory for storing game save files.
*/

#include "memory_defs.hpp"

#include <ymir/sys/backup_ram.hpp>
#include <ymir/sys/bus.hpp>

#include <ymir/state/state_system.hpp>

#include <ymir/core/hash.hpp>
#include <ymir/core/types.hpp>

#include <array>
#include <iosfwd>
#include <span>

namespace ymir::sys {

/// @brief Contains the Sega Saturn system memory components: The IPL ROM, Work RAM (low and high), and the internal
/// backup memory.
struct SystemMemory {
    SystemMemory();

    /// @brief Performs a soft or hard reset of the system memory.
    ///
    /// On a hard reset, both Work RAMs are cleared.
    /// Nothing happens on a soft reset.
    ///
    /// @param[in] hard `true` to do a hard reset, `false` for a soft reset
    void Reset(bool hard);

    /// @brief Maps memory handlers into the specified `Bus`.
    /// @param[in] bus the `Bus` instance to map handlers into
    void MapMemory(SH2Bus &bus);

    /// @brief Loads the specified IPL ROM image.
    /// @param[in] ipl the contents of the IPL ROM image
    void LoadIPL(std::span<uint8, kIPLSize> ipl);

    /// @brief Retrieves the IPL ROM hash code.
    /// @return the hash code of the currently loaded IPL ROM image
    XXH128Hash GetIPLHash() const;

    /// @brief Loads the specified internal backup memory image.
    ///
    /// `error` will contain the filesystem error if the image failed to load.
    ///
    /// @param[in] path the path of the internal backup memory image to load
    /// @param[in] copyOnWrite whether to map the file in copy-on-write mode, preserving its contents on the disk
    /// @param[out] error receives the filesystem error in case the image fails to load
    void LoadInternalBackupMemoryImage(std::filesystem::path path, bool copyOnWrite, std::error_code &error);

    /// @brief Dumps the contents of low WRAM into the specified stream.
    /// @param[in] out the output stream to dump into
    void DumpWRAMLow(std::ostream &out) const;

    /// @brief Dumps the contents of high WRAM into the specified stream.
    /// @param[in] out the output stream to dump into
    void DumpWRAMHigh(std::ostream &out) const;

    /// @brief Retrieves the internal backup memory instance.
    /// @return the object representing the system's internal backup memory
    bup::IBackupMemory &GetInternalBackupRAM() {
        return m_internalBackupRAM;
    }

    /// @brief Replaces the internal backup memory object with the provided instance.
    ///
    /// The new backup memory must be 32 KiB in size.
    ///
    /// @param[in] bupMem the new backup memory instance
    /// @return `true` if the backup memory instance was moved, `false` if the size mismatched
    bool SetInternalBackupRAM(bup::BackupMemory &&bupMem) {
        if (bupMem.Size() == kInternalBackupRAMSizeAmount) {
            std::swap(m_internalBackupRAM, bupMem);
            return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Save states

    /// @brief Saves the system memory state into the given state object.
    ///
    /// @remark While it is possible to use this method directly to save partial states, it is recommended to use the
    /// `ymir::Saturn::SaveState` method that saves the entire system state.
    ///
    /// @param[out] state the state object to store into
    void SaveState(state::SystemState &state) const;

    /// @brief Validates the given state object.
    /// @param[in] state the state object to validate
    /// @param[in] skipROMChecks skip IPL block ROM validation
    /// @return `true` if the state is valid
    [[nodiscard]] bool ValidateState(const state::SystemState &state, bool skipROMChecks = false) const;

    /// @brief Loads the system memory state from the given state object.
    ///
    /// @remark While it is possible to use this method directly to load partial states, it is recommended to use the
    /// `ymir::Saturn::LoadState` method that loads and validates the entire system state.
    ///
    /// @param[in] state the state object to load from
    void LoadState(const state::SystemState &state);

    // -------------------------------------------------------------------------
    // Memory

    alignas(16) std::array<uint8, kIPLSize> IPL; ///< 512 KiB IPL ROM (aka BIOS ROM)

    alignas(16) std::array<uint8, kWRAMLowSize> WRAMLow;   ///< 1 MiB Low Work RAM (slow)
    alignas(16) std::array<uint8, kWRAMHighSize> WRAMHigh; ///< 1 MiB High Work RAM (fast)

private:
    bup::BackupMemory m_internalBackupRAM; ///< Internal backup memory

    XXH128Hash m_iplHash{}; ///< Cached IPL ROM hash
};

} // namespace ymir::sys
