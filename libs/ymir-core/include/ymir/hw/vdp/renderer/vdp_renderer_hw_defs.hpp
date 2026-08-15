#pragma once

namespace ymir::vdp {

/// @brief VDP1 VRAM write synchronization intervals.
enum class VDP1VRAMSyncInterval {
    Command, //< Synchronizes VRAM writes before running each VDP1 command
    Draw,    //< Synchronizes VRAM writes at the start of a VDP1 draw sequence
    Swap,    //< Synchronizes VRAM writes on VDP1 framebuffer swap
};

/// @brief VDP2 VRAM write synchronization intervals.
enum class VDP2VRAMSyncInterval {
    Scanline, //< Synchronizes VRAM writes after processing each VDP2 scanline
    Frame,    //< Synchronizes VRAM writes at the end of a VDP2 frame
};

} // namespace ymir::vdp
