#pragma once

#include <ymir/debug/sh2_debug_defs.hpp>

#include <ymir/core/types.hpp>

#include <concepts>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace app {

/// @brief Information about an entry in the SH2 stack.
struct SH2StackEntry {
    enum class Type : uint8 {
        Unknown,     // Uncategorized stack value or not part of the stack
        Local,       // Locals (stack space created with add/sub)
        Register,    // Saved R0 to R15
        GBR,         // Saved GBR
        VBR,         // Saved VBR
        SR,          // Saved SR
        MACH,        // Saved MACH
        MACL,        // Saved MACL
        PR,          // Saved PR (return address)
        TrapPC,      // Trap PC
        TrapSR,      // Trap SR
        ExceptionPC, // Exception PC
        ExceptionSR, // Exception SR
    };

    Type type = Type::Unknown;
    uint8 regNum; // Used with Type::Register
};

struct SH2CallStackEntry {
    enum class Type {
        Call,     // subroutine call
        Trap,     // TRAPA instruction
        Exception // exception vector
    };
    Type type;
    uint32 address; // Return address
    uint32 target;  // Target address
    uint8 vecNum;   // Vector number (Trap and Vector only)

    static SH2CallStackEntry Call(uint32 retAddr, uint32 target) {
        return SH2CallStackEntry{.type = Type::Call, .address = retAddr, .target = target};
    }

    static SH2CallStackEntry Trap(uint32 retAddr, uint32 target, uint8 vecNum) {
        return SH2CallStackEntry{.type = Type::Trap, .address = retAddr, .target = target, .vecNum = vecNum};
    }

    static SH2CallStackEntry Exception(uint32 retAddr, uint32 target, uint8 vecNum) {
        return SH2CallStackEntry{.type = Type::Exception, .address = retAddr, .target = target, .vecNum = vecNum};
    }
};

// TODO: historical execution trace similar to mednafen
// address...address          sequential execution
// address > address          branch
// address > address(x##)     loop (iterations)
// address >RESET> address    hard reset
// address >XVEC nn> address  exception vector nn (interrupt)
// address >TRAP nn> address  TRAPA nn
// address >RTS> address      RTS instruction
// address >RTE> address      RTE instruction

/// @brief An SH-2 stack, starting at a base address and containing a number of 32-bit entries.
struct SH2Stack {
    uint32 baseAddress;
    std::vector<SH2StackEntry> entries;
    std::vector<SH2CallStackEntry> callStack;

    sint32 ResizeEntries(uint32 newSP) {
        const sint32 stackSize = static_cast<sint32>(baseAddress - newSP + sizeof(uint32) - 1) / sizeof(uint32);
        entries.resize(std::max(0, stackSize));
        return stackSize;
    }

    bool ContainsAddress(uint32 address) const {
        address &= ~3u;
        return (baseAddress - address) / sizeof(uint32) <= entries.size();
    }

    const SH2StackEntry *GetEntry(uint32 address) const {
        address &= ~3u;
        if (address >= baseAddress) {
            return nullptr;
        }
        const size_t entryIndex = (baseAddress - address) / sizeof(uint32) - 1;
        if (entryIndex >= entries.size()) {
            return nullptr;
        }
        return &entries[entryIndex];
    }
};

/// @brief Analyzes SH-2 code flow and tracks stack contents.
struct SH2ExecAnalyst {
    void Clear();

    void Reset(uint32 pc, uint32 sp);
    void ChangeStack(uint32 newSP);
    void ResizeStack(uint32 oldSP, uint32 newSP);
    void PushRegisterToStack(uint8 rn, uint32 oldSP, uint32 newSP);
    void PushToStack(ymir::debug::SH2StackValueType type, uint32 newSP);
    void PopFromStack(uint32 newSP);
    void DelaySlot(uint32 pc, uint32 target);
    void Branch(uint32 pc, uint32 target);
    void BranchDelay(uint32 target);
    void Call(uint32 target);
    void Return(uint32 target);
    void ReturnFromException(uint32 target, uint32 newSP);
    void Exception(uint8 vecNum, uint32 oldPC, uint32 oldSP, uint32 newPC);
    void Trap(uint8 vecNum, uint32 oldPC, uint32 oldSP, uint32 newPC);

    /// @brief Retrieves stack information for the specified address range.
    ///
    /// `startAddress` must be less than or equal to `endAddress`, and the range is inclusive on both ends.
    /// Addresses are aligned to longword (32-bit) boundaries.
    /// The callback function is invoked for every address between `startAddress` and `endAddress` with the following
    /// arguments:
    ///
    /// - `uint32 entryAddress` (in): the address of the stack entry
    /// - `const SH2StackEntry *entry` (in): the stack entry; nullptr if not part of a stack
    /// - `uint32 baseAddress` (in): the base address of the stack; only valid if entry != nullptr
    ///
    /// @tparam TFnStackCallback type of callback function to invoke for each stack entry
    /// @param[in] startAddress the first address
    /// @param[in] endAddress the last address
    /// @param[in] fnStackCallback function to call on every stack entry
    template <typename TFnStackCallback>
        requires std::invocable<TFnStackCallback, uint32 /*entryAddress*/, const SH2StackEntry * /*entry*/,
                                uint32 /*baseAddress*/>
    void GetStackInfo(uint32 startAddress, uint32 endAddress, TFnStackCallback &&fnStackCallback) const {
        std::unique_lock lock{m_mtxStacks};

        startAddress &= ~3u;
        endAddress &= ~3u;

        if (endAddress < startAddress) {
            // Invalid range; do nothing
            return;
        }

        auto it = m_stacks.upper_bound(startAddress);
        for (uint32 address = startAddress; address <= endAddress; address += sizeof(uint32)) {
            if (it == m_stacks.end()) {
                fnStackCallback(address, nullptr, 0);
                continue;
            }
            if (address >= it->second.baseAddress) {
                // Gone past the current stack; find next
                ++it;
                if (it == m_stacks.end()) {
                    // No more stacks past this point
                    fnStackCallback(address, nullptr, 0);
                    continue;
                }
            }
            const SH2Stack &stack = it->second;
            fnStackCallback(address, stack.GetEntry(address), stack.baseAddress);
        }
    }

    /// @brief Returns a copy of the current call stack.
    /// @return a copy of the current call stack
    std::vector<SH2CallStackEntry> GetCurrentCallStack() const {
        std::unique_lock lock{m_mtxStacks};
        auto it = m_stacks.find(m_currStack);
        if (it == m_stacks.end()) {
            return {};
        }
        // TODO: use atomic shared pointer if this copy gets too expensive
        return it->second.callStack;
    }

    /// @brief Returns the current data stack base address, if it points to a valid stack.
    /// @return the current data stack base address, or `std::nullopt` if there is no traced data stack.
    std::optional<uint32> GetCurrentDataStackBase() const {
        std::unique_lock lock{m_mtxStacks};
        if (m_stacks.contains(m_currStack)) {
            return m_currStack;
        }
        return std::nullopt;
    }

private:
    mutable std::mutex m_mtxStacks;
    std::map<uint32, SH2Stack> m_stacks;
    uint32 m_currStack = 0;

    /// @brief Retrieves the current stack or creates a stack based at the specified address.
    /// Assumes the mutex is held.
    /// @param[in] sp the base stack pointer
    /// @return a reference to the current stack
    SH2Stack &GetOrCreateStack(uint32 sp);

    enum class DelaySlotEvent { None, Branch, Call, RTS, RTE };
    DelaySlotEvent m_delaySlotEvent = DelaySlotEvent::None;
};

} // namespace app
