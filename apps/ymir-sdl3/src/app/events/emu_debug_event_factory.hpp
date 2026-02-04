#pragma once

#include <ymir/core/types.hpp>

#include <ymir/hw/vdp/vdp_defs.hpp>

#include "emu_event.hpp"

namespace app::events::emu::debug {

EmuEvent ExecuteSH2Division(bool master, bool div64);

EmuEvent WriteMainMemory(uint32 address, uint8 value, bool enableSideEffects);
EmuEvent WriteSH1Memory(uint32 address, uint8 value, bool enableSideEffects);
EmuEvent WriteSH2Memory(uint32 address, uint8 value, bool enableSideEffects, bool master, bool bypassCache);

EmuEvent DumpDisasmView(uint32 start, uint32 end, bool master, bool disasmDump, bool binaryDump);

EmuEvent AddSH2Breakpoint(bool master, uint32 address);
EmuEvent RemoveSH2Breakpoint(bool master, uint32 address);
EmuEvent ReplaceSH2Breakpoints(bool master, const std::set<uint32> &addresses);
EmuEvent ClearSH2Breakpoints(bool master);

EmuEvent SetLayerEnabled(ymir::vdp::Layer layer, bool enabled);

EmuEvent VDP2SetCRAMColor555(uint32 index, ymir::vdp::Color555 color);
EmuEvent VDP2SetCRAMColor888(uint32 index, ymir::vdp::Color888 color);

} // namespace app::events::emu::debug
