#pragma once

/**
@file
@brief Defines `ymir::debug::ICDBlockTracer`, the CD Block tracer interface.
*/

#include <ymir/hw/cdblock/cdblock_buffer.hpp>

#include <ymir/core/types.hpp>

#include <deque>

namespace ymir::debug {

/// @brief Interface for CD Block tracers.
///
/// Must be implemented by users of the core library.
///
/// Attach to an instance of `ymir::cdblock::CDBlock` with its `UseTracer(ICDBlockTracer *)` method.
struct ICDBlockTracer {
    /// @brief Default virtual destructor. Required for inheritance.
    virtual ~ICDBlockTracer() = default;

    /// @brief Invoked when the CD Block processes a command.
    /// @param[in] cr1 the value of CR1
    /// @param[in] cr2 the value of CR2
    /// @param[in] cr3 the value of CR3
    /// @param[in] cr4 the value of CR4
    virtual void ProcessCommand(uint16 cr1, uint16 cr2, uint16 cr3, uint16 cr4) {}

    /// @brief Invoked when the CD Block sends a command response.
    /// @param[in] cr1 the value of CR1
    /// @param[in] cr2 the value of CR2
    /// @param[in] cr3 the value of CR3
    /// @param[in] cr4 the value of CR4
    virtual void ProcessCommandResponse(uint16 cr1, uint16 cr2, uint16 cr3, uint16 cr4) {}

    /// @brief Invoked when all partitions and buffers are reset to the default state (no buffers allocated).
    virtual void PartitionResetAll() {}

    /// @brief Invoked when the tracer is attached to copy the current state of a partition.
    /// @param[in] index the partition number
    /// @param[in] buffers the buffer queue
    virtual void PartitionSync(uint8 index, const std::deque<cdblock::Buffer> &buffers) {}

    /// @brief Invoked when a buffer is inserted into a partition's head.
    /// @param[in] index the partition number
    /// @param[in] buffer the buffer
    virtual void PartitionInsertHead(uint8 index, const cdblock::Buffer &buffer) {}

    /// @brief Invoked when a buffer is removed from a partition's tail.
    /// @param[in] index the partition number
    /// @param[in] offset offset from the tail
    virtual void PartitionRemoveTail(uint8 index, uint8 offset) {}

    /// @brief Invoked when buffers are removed from a partition.
    /// @param[in] index the partition number
    /// @param[in] start the first sector to remove
    /// @param[in] end the last sector to remove
    virtual void PartitionDeleteSectors(uint8 index, uint16 start, uint16 end) {}

    /// @brief Invoked when a partition is cleared (all buffers freed).
    /// @param[in] index the partition number
    virtual void PartitionClear(uint8 index) {}

    /// @brief Invoked when the tracer is detached.
    virtual void Detach() {}
};

} // namespace ymir::debug
