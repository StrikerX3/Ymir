#include "sh2_exec_analyst.hpp"

#include <ymir/hw/sh2/sh2.hpp>
#include <ymir/hw/sh2/sh2_decode.hpp>
#include <ymir/hw/sh2/sh2_disasm.hpp>

// Instruction and their effects:
// - stack management
//   - change stack base
//       (rn) mov           Rm, Rn
//       (rn) mov.<sz>      @Rm, Rn
//       (rn) mov.<sz>      @(R0,Rm), Rn
//       (rn) mov.l         @(disp,Rm), Rn
//       (rn) mov           #imm, Rn
//       (rn) mov.<sz>      @(disp,PC), Rn
//       (rn) movt          Rn
//       (rn) extu.<sz>     Rm, Rn
//       (rn) exts.<sz>     Rm, Rn
//       (rn) swap.<sz>     Rm, Rn
//       (rn) xtrct         Rm, Rn
//       (rn) stc           GBR|SR|VBR, Rn
//       (rn) sts           MACH|MACL|PR, Rn
//       (rn) and|or|xor    Rm, Rn
//       (rn) neg|negc|not  Rm, Rn
//       (rn) rotcl|rotcr   Rn
//       (rn) rotl|rotr     Rn
//       (rn) shal|shar     Rn
//       (rn) shll[<n>]     Rn
//       (rn) shlr[<n>]     Rn
//       (rn) div1          Rm, Rn
//   - push
//       (rn) mov.<sz>      Rm, @-Rn
//       (rn) stc.l         GBR|SR|VBR, @-Rn
//       (rn) sts.l         MACH|MACL|PR, @-Rn
//       (pc,sr) trapa      #imm
//       (pc,sr) <exceptions>
//   - pop
//       (rm) mov.<sz>      @Rm+, Rn
//       (rm) ldc.l         @Rm+, GBR|SR|VBR
//       (rm) lds.l         @Rm+, MACH|MACL|PR
//       (rm,rn) mac.<sz>   @Rm+, @Rn+
//       (pc,sr) rte
//   - resize
//       (rn) add[c|v]      Rm, Rn
//       (rn) add           #imm, Rn
//       (rn) sub[c|v]      Rm, Rn
//       (rn) dt            Rn
// - code flow
//   - direct branch (loop if offset is negative)
//       b[f|t][/s]    <label>
//       bra           <label>
//       braf          Rm
//       jmp           @Rm
//   - branch and set PR
//       bsr           <label>
//       bsrf          Rm
//       jsr           @Rm
//   - trap
//       trapa         #imm
//   - return to PR
//       rts
//   - return from exception
//       rte

using namespace ymir;

namespace app {

void SH2ExecAnalyst::Clear() {
    std::unique_lock lock{m_mtxStacks};
    m_stacks.clear();
}

void SH2ExecAnalyst::Reset(uint32 pc, uint32 sp) {
    std::unique_lock lock{m_mtxStacks};
    m_stacks.clear();
    m_stacks[sp] = {.baseAddress = sp};
    m_currStack = sp;
    // TODO: trace RESET to pc
}

void SH2ExecAnalyst::ChangeStack(uint32 newSP) {
    std::unique_lock lock{m_mtxStacks};
    if (m_stacks.contains(m_currStack) && m_stacks[m_currStack].entries.empty()) {
        m_stacks.erase(m_currStack);
    }
    if (!m_stacks.contains(newSP)) {
        m_stacks[newSP] = {.baseAddress = newSP};
    }
    m_currStack = newSP;
}

void SH2ExecAnalyst::ResizeStack(uint32 oldSP, uint32 newSP) {
    std::unique_lock lock{m_mtxStacks};
    auto &stack = GetOrCreateStack(oldSP);
    const uint32 currSize = stack.entries.size();
    const sint32 stackSize = stack.ResizeEntries(newSP);
    if (stackSize >= 0) {
        if (currSize < static_cast<uint32>(stackSize)) {
            std::fill(stack.entries.begin() + currSize, stack.entries.end(),
                      SH2StackEntry{.type = SH2StackEntry::Type::Local});
        }
    } else {
        m_stacks.erase(m_currStack);
        m_currStack = newSP;
    }
}

void SH2ExecAnalyst::PushRegisterToStack(uint8 rn, uint32 oldSP, uint32 newSP) {
    std::unique_lock lock{m_mtxStacks};
    auto &stack = GetOrCreateStack(oldSP);
    if (stack.ResizeEntries(newSP) < 0) {
        m_stacks.erase(m_currStack);
        m_currStack = newSP;
        return;
    }
    auto &entry = stack.entries.back();
    entry.type = SH2StackEntry::Type::Register;
    entry.regNum = rn;
}

void SH2ExecAnalyst::PushToStack(debug::SH2StackValueType type, uint32 newSP) {
    std::unique_lock lock{m_mtxStacks};
    auto &stack = GetOrCreateStack(newSP + 4);
    stack.ResizeEntries(newSP);
    auto &entry = stack.entries.back();
    switch (type) {
    case debug::SH2StackValueType::GBR: entry.type = SH2StackEntry::Type::GBR; break;
    case debug::SH2StackValueType::VBR: entry.type = SH2StackEntry::Type::VBR; break;
    case debug::SH2StackValueType::SR: entry.type = SH2StackEntry::Type::SR; break;
    case debug::SH2StackValueType::MACH: entry.type = SH2StackEntry::Type::MACH; break;
    case debug::SH2StackValueType::MACL: entry.type = SH2StackEntry::Type::MACL; break;
    case debug::SH2StackValueType::PR: entry.type = SH2StackEntry::Type::PR; break;
    }
}

void SH2ExecAnalyst::PopFromStack(uint32 newSP) {
    if (!m_stacks.contains(m_currStack)) {
        return;
    }

    std::unique_lock lock{m_mtxStacks};
    auto &stack = m_stacks[m_currStack];
    const sint32 stackSize = stack.ResizeEntries(newSP);
    if (stackSize < 0) {
        m_stacks.erase(m_currStack);
        m_currStack = newSP;
    }
}

void SH2ExecAnalyst::DelaySlot(uint32 pc, uint32 target) {
    if (!m_stacks.contains(m_currStack)) {
        return;
    }

    std::unique_lock lock{m_mtxStacks};
    auto &stack = m_stacks[m_currStack];
    switch (m_delaySlotEvent) {
    case DelaySlotEvent::None: break;
    case DelaySlotEvent::Branch:
        // TODO: trace branch/loop
        break;
    case DelaySlotEvent::Call:
        // TODO: trace call
        stack.callStack.push_back(SH2CallStackEntry::Call(pc + 2, target));
        break;
    case DelaySlotEvent::RTS:
        // TODO: trace RTS
        if (!stack.callStack.empty()) {
            stack.callStack.pop_back();
        }
        break;
    case DelaySlotEvent::RTE:
        // TODO: trace RTE
        if (!stack.callStack.empty()) {
            stack.callStack.pop_back();
        }
        break;
    }
}

void SH2ExecAnalyst::Branch(uint32 pc, uint32 target) {
    // TODO: trace branch from pc to target
}

void SH2ExecAnalyst::BranchDelay(uint32 target) {
    m_delaySlotEvent = DelaySlotEvent::Branch;
}

void SH2ExecAnalyst::Call(uint32 target) {
    m_delaySlotEvent = DelaySlotEvent::Call;
}

void SH2ExecAnalyst::Return(uint32 target) {
    m_delaySlotEvent = DelaySlotEvent::RTS;
}

void SH2ExecAnalyst::ReturnFromException(uint32 target) {
    m_delaySlotEvent = DelaySlotEvent::RTE;
}

void SH2ExecAnalyst::Exception(uint8 vecNum, uint32 oldPC, uint32 oldSP, uint32 newPC) {
    std::unique_lock lock{m_mtxStacks};
    auto &stack = GetOrCreateStack(oldSP);
    stack.ResizeEntries(oldSP - 8);
    stack.entries[stack.entries.size() - 2].type = SH2StackEntry::Type::ExceptionSR;
    stack.entries[stack.entries.size() - 1].type = SH2StackEntry::Type::ExceptionPC;
    stack.callStack.push_back(SH2CallStackEntry::Exception(oldPC, newPC, vecNum));
    // TODO: trace XVEC <vecNum>
}

void SH2ExecAnalyst::Trap(uint8 vecNum, uint32 oldPC, uint32 oldSP, uint32 newPC) {
    std::unique_lock lock{m_mtxStacks};
    auto &stack = GetOrCreateStack(oldSP);
    stack.ResizeEntries(oldSP - 8);
    stack.entries[stack.entries.size() - 2].type = SH2StackEntry::Type::TrapSR;
    stack.entries[stack.entries.size() - 1].type = SH2StackEntry::Type::TrapPC;
    stack.callStack.push_back(SH2CallStackEntry::Trap(oldPC + 2, newPC, vecNum));
    // TODO: trace TRAP <vecNum>
}

FORCE_INLINE SH2Stack &SH2ExecAnalyst::GetOrCreateStack(uint32 sp) {
    if (!m_stacks.contains(m_currStack)) {
        m_stacks[sp] = {.baseAddress = sp};
        m_currStack = sp;
    }
    return m_stacks[m_currStack];
}

} // namespace app
