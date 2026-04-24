#pragma once

/**
@file
@brief Defines `ymir::debug::ISH2Tracer`, the SH2 tracer interface.
*/

#include <ymir/core/types.hpp>

#include <ymir/hw/sh2/sh2_intc.hpp>

#include "sh2_debug_defs.hpp"

namespace ymir::debug {

/// @brief Interface for SH2 tracers.
///
/// Must be implemented by users of the core library.
///
/// Attach to an instance of `ymir::sh2::SH2` with its `UseTracer(ISH2Tracer *)` method.
///
/// @note This tracer requires the emulator to execute in debug tracing mode.
struct ISH2Tracer {
    /// @brief Default virtual destructor. Required for inheritance.
    virtual ~ISH2Tracer() = default;

    /// @brief Invoked when the tracer is attached to an SH-2 instance.
    virtual void Attached() {}

    /// @brief Invoked when the tracer is detached from an SH-2 instance.
    virtual void Detached() {}

    /// @brief Invoked when the SH-2 is reset.
    /// @param[in] pc the reset vector program counter
    /// @param[in] sp the reset vector stack pointer
    /// @param[in] watchdogInitiated whether the reset was initiated by the WDT (`true`) or by NMI/cold boot (`false`)
    virtual void Reset(uint32 pc, uint32 sp, bool watchdogInitiated) {}

    /// @brief Invoked immediately before executing an instruction.
    /// @param[in] pc the current program counter
    /// @param[in] opcode the instruction opcode
    /// @param[in] delaySlot indicates if the instruction is executing in a delay slot
    virtual void ExecuteInstruction(uint32 pc, uint16 opcode, bool delaySlot) {}

    /// @brief Invoked when the CPU finishes a delayed branch, that is, executes an instruction in a delay slot.
    /// @param[in] pc the current program counter
    /// @param[in] target the target address
    virtual void DelaySlot(uint32 pc, uint32 target) {}

    /// @brief Invoked when the CPU executes a branch instruction without a delay slot.
    /// @param[in] pc the current program counter
    /// @param[in] target the target address
    virtual void Branch(uint32 pc, uint32 target) {}

    /// @brief Invoked when the CPU executes a branch instruction with a delay slot.
    /// @param[in] target the target address
    virtual void BranchDelay(uint32 target) {}

    /// @brief Invoked when the CPU executes a branch instruction that stores PC into PR.
    /// All instructions of this class have delay slots.
    /// @param[in] target the target address
    virtual void Call(uint32 target) {}

    /// @brief Invoked when the CPU executes a return from subroutine instruction (RTS).
    /// This always has a delay slot.
    /// @param[in] target the target address
    virtual void Return(uint32 target) {}

    /// @brief Invoked when the CPU executes a return from exception instruction (RTE).
    /// This always has a delay slot.
    /// @param[in] target the target address
    /// @param[in] newSP the updated stack pointer value
    virtual void ReturnFromException(uint32 target, uint32 newSP) {}

    /// @brief Invoked when the CPU handles an interrupt.
    /// @param[in] vecNum the interrupt vector number
    /// @param[in] level the interrupt level (priority)
    /// @param[in] source the interrupt source
    /// @param[in] pc the value of PC before the interrupt is handled
    virtual void Interrupt(uint8 vecNum, uint8 level, sh2::InterruptSource source, uint32 pc) {}

    /// @brief Invoked when the CPU handles an exception.
    /// @param[in] vecNum the exception vector number
    /// @param[in] oldPC the value of PC before the exception is handled
    /// @param[in] oldSR the value of SR before the exception is handled
    /// @param[in] oldSP the value of SP before the exception is handled
    /// @param[in] newPC the value of PC after the exception is handled
    virtual void Exception(uint8 vecNum, uint32 oldPC, uint32 oldSR, uint32 oldSP, uint32 newPC) {}

    /// @brief Invoked when the CPU executes a TRAPA instruction.
    /// @param[in] vecNum the trap vector number
    /// @param[in] oldPC the value of PC before the trap is handled
    /// @param[in] oldSP the value of SP before the trap is handled
    /// @param[in] newPC the value of PC after the trap is handled
    virtual void Trap(uint8 vecNum, uint32 oldPC, uint32 oldSP, uint32 newPC) {}

    /// @brief Invoked when the stack is about to be switched by changing the value of R15.
    /// @param[in] newSP the new stack pointer value
    virtual void ChangeStack(uint32 newSP) {}

    /// @brief Invoked when the stack is about to be resized by incrementing or decrementing R15.
    /// @param[in] oldSP the previous stack pointer value
    /// @param[in] newSP the updated stack pointer value
    virtual void ResizeStack(uint32 oldSP, uint32 newSP) {}

    /// @brief Invoked when a general-purpose register is about to be pushed to the stack.
    /// @param[in] rn the register index
    /// @param[in] oldSP the previous stack pointer value
    /// @param[in] newSP the updated stack pointer value
    virtual void PushRegisterToStack(uint8 rn, uint32 oldSP, uint32 newSP) {}

    /// @brief Invoked when a known value is about to be pushed to the stack.
    /// @param[in] type the type of value to be pushed
    /// @param[in] newSP the updated stack pointer value
    virtual void PushToStack(SH2StackValueType type, uint32 newSP) {}

    /// @brief Invoked when a value is about to be popped from the stack.
    /// @param[in] newSP the updated stack pointer value
    virtual void PopFromStack(uint32 newSP) {}

    /// @brief Invoked when a 32-bit by 32-bit DIVU division begins.
    /// @param[in] dividend the value of the dividend (`DVDNTL`)
    /// @param[in] divisor the value of the divisor (`DVSR`)
    /// @param[in] overflowIntrEnable indicates if the division overflow interrupt is enabled (`DVCR.OVFIE`)
    virtual void Begin32x32Division(sint32 dividend, sint32 divisor, bool overflowIntrEnable) {}

    /// @brief Invoked when a 64-bit by 32-bit DIVU division begins.
    /// @param[in] dividend the value of the dividend (`DVDNTH:DVDNTL`)
    /// @param[in] divisor the value of the divisor (`DVSR`)
    /// @param[in] overflowIntrEnable indicates if the division overflow interrupt is enabled (`DVCR.OVFIE`)
    virtual void Begin64x32Division(sint64 dividend, sint32 divisor, bool overflowIntrEnable) {}

    /// @brief Invoked when a DIVU division ends.
    /// @param[in] quotient the resulting quotient (`DVDNTL`)
    /// @param[in] remainder the resulting remainder (`DVDNTH`)
    /// @param[in] overflow indicates if the division resulted in an overflow
    virtual void EndDivision(sint32 quotient, sint32 remainder, bool overflow) {}

    /// @brief Invoked when a DMA transfer begins.
    /// @param[in] channel the DMAC channel number, either 0 or 1
    /// @param[in] srcAddress the starting source address of the transfer
    /// @param[in] dstAddress the starting destination address of the transfer
    /// @param[in] count the number of transfer units to be performed. For 16-byte transfers, this number decrements
    ///            once per 32-bit transfer
    /// @param[in] unitSize the size of a single unit of transfer: 1, 2, 4 or 16
    /// @param[in] srcInc the source address increment per unit of transfer
    /// @param[in] dstInc the destination address increment per unit of transfer
    virtual void DMAXferBegin(uint32 channel, uint32 srcAddress, uint32 dstAddress, uint32 count, uint32 unitSize,
                              sint32 srcInc, sint32 dstInc) {}

    /// @brief Invoked when a DMA channel transfers one unit of data.
    ///
    /// For 16-byte transfers, this function is invoked once per 32-bit transfer with a `unitSize` of 4.
    ///
    /// @param[in] channel the DMAC channel number, either 0 or 1
    /// @param[in] srcAddress the source address of the transfer
    /// @param[in] dstAddress the destination address of the transfer
    /// @param[in] data the transferred data
    /// @param[in] unitSize the size of a single unit of transfer: 1, 2 or 4
    virtual void DMAXferData(uint32 channel, uint32 srcAddress, uint32 dstAddress, uint32 data, uint32 unitSize) {}

    /// @brief Invoked when a DMA transfer finishes.
    /// @param[in] channel the DMAC channel number, either 0 or 1
    /// @param[in] irqRaised indicates if the channel's transfer end interrupt signal was raised
    virtual void DMAXferEnd(uint32 channel, bool irqRaised) {}
};

} // namespace ymir::debug
