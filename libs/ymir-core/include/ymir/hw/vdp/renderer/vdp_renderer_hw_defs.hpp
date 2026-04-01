#pragma once

namespace ymir::vdp {

/// @brief VDP1 VRAM write synchronization modes.
enum class VDP1VRAMSyncMode {
    Command, //< Synchronizes before running each VDP1 command
    Draw,    //< Synchronizes at the start of a VDP1 draw sequence
    Swap,    //< Synchronizes on VDP1 framebuffer swap
};

/// @brief VDP2 VRAM write synchronization modes.
enum class VDP2VRAMSyncMode {
    Scanline, //< Synchronizes after processing each VDP2 scanline
    Frame,    //< Synchronizes at the end of a VDP2 frame
};

} // namespace ymir::vdp
