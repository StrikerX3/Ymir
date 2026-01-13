#include "emu_debug_event_factory.hpp"

#include "emu_event_factory.hpp"

#include <app/shared_context.hpp>

#include <ymir/hw/sh1/sh1.hpp>
#include <ymir/hw/sh2/sh2.hpp>
#include <ymir/hw/sh2/sh2_disasm.hpp>
#include <ymir/hw/vdp/vdp.hpp>
#include <ymir/sys/bus.hpp>
#include <ymir/util/dev_log.hpp>

#include <fmt/format.h>

#include <algorithm>

namespace app::events::emu::debug {

namespace grp {
    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "Emulator";
    };
} // namespace grp

EmuEvent ExecuteSH2Division(bool master, bool div64) {
    if (div64) {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = ctx.saturn.GetSH2(master);
            sh2.GetProbe().ExecuteDiv64();
        });
    } else {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = ctx.saturn.GetSH2(master);
            sh2.GetProbe().ExecuteDiv32();
        });
    }
}

EmuEvent WriteMainMemory(uint32 address, uint8 value, bool enableSideEffects) {
    if (enableSideEffects) {
        return RunFunction([=](SharedContext &ctx) { ctx.saturn.GetMainBus().Write<uint8>(address, value); });
    } else {
        return RunFunction([=](SharedContext &ctx) { ctx.saturn.GetMainBus().Poke<uint8>(address, value); });
    }
}

EmuEvent WriteSH1Memory(uint32 address, uint8 value, bool enableSideEffects) {
    if (enableSideEffects) {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh1 = ctx.saturn.GetSH1();
            sh1.GetProbe().MemWriteByte(address, value);
        });
    } else {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh1 = ctx.saturn.GetSH1();
            sh1.GetProbe().MemPokeByte(address, value);
        });
    }
}

EmuEvent WriteSH2Memory(uint32 address, uint8 value, bool enableSideEffects, bool master, bool bypassCache) {
    if (enableSideEffects) {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = ctx.saturn.GetSH2(master);
            sh2.GetProbe().MemWriteByte(address, value, bypassCache);
        });
    } else {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = ctx.saturn.GetSH2(master);
            sh2.GetProbe().MemPokeByte(address, value, bypassCache);
        });
    }
}

// event to output specified disasm view into a formatted output
EmuEvent DumpDisasmView(uint32 start, uint32 end, bool master) {
    return RunFunction([=](SharedContext &ctx) {
        // adress space ranges
        constexpr uint32 kAddrMin = 0x00000000u;
        constexpr uint32 kAddrMax = 0x07FFFFFEu;

        // align addresses
        uint32 rangeStart = start & ~1u;
        uint32 rangeEnd = end & ~1u;
        // ensure range order
        if (rangeEnd < rangeStart) {
            std::swap(rangeStart, rangeEnd);
        }
        // clamp bounds
        rangeStart = std::clamp(rangeStart, kAddrMin, kAddrMax);
        rangeEnd = std::clamp(rangeEnd, kAddrMin, kAddrMax);
        if (rangeStart > rangeEnd) {
            // should never happen, handle anyway
            ctx.DisplayMessage("Invalid disassembly range");
            return;
        }

        // fetch dump path, try creating dir
        const auto dumpPath = ctx.profile.GetPath(ProfilePath::Dumps);
        std::error_code ec{};
        std::filesystem::create_directories(dumpPath, ec);
        if (ec) {
            devlog::warn<grp::base>("Could not create dump directory {}: {}", dumpPath, ec.message());
            ctx.DisplayMessage("Failed to create dump directory");
            return;
        }

        // add prefix for msh-2/ssh-2 
        const char sh2Prefix = master ? 'm' : 's';
        const auto outPath =
            dumpPath / fmt::format("{}sh2-disasm_{:08X}_{:08X}.txt", sh2Prefix, rangeStart, rangeEnd);

        // fetch output file stream to write to
        std::ofstream out{outPath, std::ios::trunc};
        if (!out) {
            devlog::warn<grp::base>("Failed to open disassembly dump file {}", outPath);
            ctx.DisplayMessage("Failed to open disassembly dump file");
            return;
        }

        // switch mnemonic to string view
        auto mnemonicToString = [](ymir::sh2::Mnemonic m) -> std::string_view {
            using enum ymir::sh2::Mnemonic;
            switch (m) {
            case NOP: return "nop";
            case SLEEP: return "sleep";
            case MOV: return "mov";
            case MOVA: return "mova";
            case MOVT: return "movt";
            case CLRT: return "clrt";
            case SETT: return "sett";
            case EXTU: return "extu";
            case EXTS: return "exts";
            case SWAP: return "swap";
            case XTRCT: return "xtrct";
            case LDC: return "ldc";
            case LDS: return "lds";
            case STC: return "stc";
            case STS: return "sts";
            case ADD: return "add";
            case ADDC: return "addc";
            case ADDV: return "addv";
            case AND: return "and";
            case NEG: return "neg";
            case NEGC: return "negc";
            case NOT: return "not";
            case OR: return "or";
            case ROTCL: return "rotcl";
            case ROTCR: return "rotcr";
            case ROTL: return "rotl";
            case ROTR: return "rotr";
            case SHAL: return "shal";
            case SHAR: return "shar";
            case SHLL: return "shll";
            case SHLL2: return "shll2";
            case SHLL8: return "shll8";
            case SHLL16: return "shll16";
            case SHLR: return "shlr";
            case SHLR2: return "shlr2";
            case SHLR8: return "shlr8";
            case SHLR16: return "shlr16";
            case SUB: return "sub";
            case SUBC: return "subc";
            case SUBV: return "subv";
            case XOR: return "xor";
            case DT: return "dt";
            case CLRMAC: return "clrmac";
            case MAC: return "mac";
            case MUL: return "mul";
            case MULS: return "muls";
            case MULU: return "mulu";
            case DMULS: return "dmuls";
            case DMULU: return "dmulu";
            case DIV0S: return "div0s";
            case DIV0U: return "div0u";
            case DIV1: return "div1";
            case CMP_EQ: return "cmp/eq";
            case CMP_GE: return "cmp/ge";
            case CMP_GT: return "cmp/gt";
            case CMP_HI: return "cmp/hi";
            case CMP_HS: return "cmp/hs";
            case CMP_PL: return "cmp/pl";
            case CMP_PZ: return "cmp/pz";
            case CMP_STR: return "cmp/str";
            case TAS: return "tas";
            case TST: return "tst";
            case BF: return "bf";
            case BFS: return "bfs";
            case BT: return "bt";
            case BTS: return "bts";
            case BRA: return "bra";
            case BRAF: return "braf";
            case BSR: return "bsr";
            case BSRF: return "bsrf";
            case JMP: return "jmp";
            case JSR: return "jsr";
            case TRAPA: return "trapa";
            case RTE: return "rte";
            case RTS: return "rts";
            case Illegal: return "(illegal)";
            default: return "(?)";
            }
        };

        // switch operand to string (more of the same)
        auto opToString = [](uint32 address, const ymir::sh2::Operand &op) -> std::string {
            using ymir::sh2::Operand;
            using ymir::sh2::OperandSize;
            switch (op.type) {
            case Operand::Type::Imm: 
                return fmt::format("#0x{:X}", static_cast<uint32>(op.immDisp));
            case Operand::Type::Rn: 
                return fmt::format("r{}", op.reg);
            case Operand::Type::AtRn: 
                return fmt::format("@r{}", op.reg);
            case Operand::Type::AtRnPlus: 
                return fmt::format("@r{}+", op.reg);
            case Operand::Type::AtMinusRn: 
                return fmt::format("@-r{}", op.reg);
            case Operand::Type::AtDispRn: 
                return fmt::format("@(0x{:X}, r{})", static_cast<uint32>(op.immDisp), op.reg);
            case Operand::Type::AtR0Rn: 
                return fmt::format("@(r0, r{})", op.reg);
            case Operand::Type::AtDispGBR:
                return fmt::format("@(0x{:X}, gbr)", static_cast<uint32>(op.immDisp));
            case Operand::Type::AtR0GBR: 
                return "@(r0, gbr)";
            case Operand::Type::AtDispPC:
                return fmt::format("@(0x{:X})", static_cast<uint32>(address + op.immDisp));
            case Operand::Type::AtDispPCWordAlign:
                return fmt::format("@(0x{:X})", static_cast<uint32>((address & ~3u) + op.immDisp));
            case Operand::Type::AtRnPC: 
                return fmt::format("@r{}+pc", op.reg);
            case Operand::Type::DispPC: 
                return fmt::format("0x{:X}", static_cast<uint32>(address + op.immDisp));
            case Operand::Type::RnPC: 
                return fmt::format("r{}+pc", op.reg);
            case Operand::Type::SR: 
                return "sr";
            case Operand::Type::GBR: 
                return "gbr";
            case Operand::Type::VBR: 
                return "vbr";
            case Operand::Type::MACH: 
                return "mach";
            case Operand::Type::MACL: 
                return "macl";
            case Operand::Type::PR: 
                return "pr";
            default: return {};
            }
        };

        // switch instruction to string (...more of the same)
        auto instrToString = [&](uint32 address, uint16 opcode, const ymir::sh2::DisassembledInstruction &instr) {
            std::string line = fmt::format("{:08X}: {:04X} {}", address, opcode, mnemonicToString(instr.mnemonic));
            // operand size 
            switch (instr.opSize) {
            case ymir::sh2::OperandSize::Byte: line += ".b"; break;
            case ymir::sh2::OperandSize::Word: line += ".w"; break;
            case ymir::sh2::OperandSize::Long: line += ".l"; break;
            default: break;
            }
            
            // handle op1 and op2 -> string
            std::string op1Str = opToString(address, instr.op1);
            std::string op2Str = opToString(address, instr.op2);
            if (!op1Str.empty()) {
                line += " ";
                line += op1Str;
            }
            if (!op2Str.empty()) {
                line += op1Str.empty() ? " " : ", ";
                line += op2Str;
            }
            return line;
        };

        // fetch saturn bus
        auto &bus = ctx.saturn.GetMainBus();
        // iterate over address range
        for (uint32 addr = rangeStart;; addr += sizeof(uint16)) {
            // fetch opcode from bus, then disasm
            const uint16 opcode = bus.Peek<uint16>(addr);
            const auto &disasm = ymir::sh2::Disassemble(opcode);
            // write to output with newline
            out << instrToString(addr, opcode, disasm) << "\n";
            if (addr >= rangeEnd) {
                break;
            }
        }

        ctx.DisplayMessage(fmt::format("{}SH2 disassembly written to {}", (master ? "M" : "S"), outPath));
    });
}

EmuEvent AddSH2Breakpoint(bool master, uint32 address) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = ctx.saturn.GetSH2(master);
        std::unique_lock lock{ctx.locks.breakpoints};
        /* TODO: const bool added = */ sh2.AddBreakpoint(address);
    });
}

EmuEvent RemoveSH2Breakpoint(bool master, uint32 address) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = ctx.saturn.GetSH2(master);
        std::unique_lock lock{ctx.locks.breakpoints};
        /* TODO: const bool removed = */ sh2.RemoveBreakpoint(address);
    });
}

EmuEvent ReplaceSH2Breakpoints(bool master, const std::set<uint32> &addresses) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = ctx.saturn.GetSH2(master);
        std::unique_lock lock{ctx.locks.breakpoints};
        sh2.ReplaceBreakpoints(addresses);
    });
}

EmuEvent ClearSH2Breakpoints(bool master) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = ctx.saturn.GetSH2(master);
        std::unique_lock lock{ctx.locks.breakpoints};
        sh2.ClearBreakpoints();
    });
}

EmuEvent SetLayerEnabled(ymir::vdp::Layer layer, bool enabled) {
    return RunFunction([=](SharedContext &ctx) {
        auto &vdp = ctx.saturn.GetVDP();
        vdp.SetLayerEnabled(layer, enabled);
    });
}

EmuEvent VDP2SetCRAMColor555(uint32 index, ymir::vdp::Color555 color) {
    return RunFunction([=](SharedContext &ctx) {
        auto &vdp = ctx.saturn.GetVDP();
        vdp.GetProbe().VDP2SetCRAMColor555(index, color);
    });
}

EmuEvent VDP2SetCRAMColor888(uint32 index, ymir::vdp::Color888 color) {
    return RunFunction([=](SharedContext &ctx) {
        auto &vdp = ctx.saturn.GetVDP();
        vdp.GetProbe().VDP2SetCRAMColor888(index, color);
    });
}

} // namespace app::events::emu::debug
