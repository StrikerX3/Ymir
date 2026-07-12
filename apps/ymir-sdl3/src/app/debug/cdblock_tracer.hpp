#pragma once

#include <ymir/debug/cdblock_tracer_base.hpp>

#include <ymir/hw/cdblock/cdblock_defs.hpp>

#include <util/ring_buffer.hpp>

#include <array>
#include <deque>
#include <mutex>

namespace app {

struct CDBlockTracer final : ymir::debug::ICDBlockTracer {
    struct CommandInfo {
        uint32 index;
        std::array<uint16, 4> request;
        std::array<uint16, 4> response;
        bool processed;
    };

    void ClearCommands();

    bool traceCommands = false;
    util::RingBuffer<CommandInfo, 1024> commands;

    using Partition = std::deque<ymir::cdblock::Buffer>;
    std::array<Partition, ymir::cdblock::kNumPartitions> partitions;
    std::mutex mtxPartitions;

private:
    uint32 m_commandCounter = 0;

    // -------------------------------------------------------------------------
    // ICDBlockTracer implementation

    void ProcessCommand(uint16 cr1, uint16 cr2, uint16 cr3, uint16 cr4) final;
    void ProcessCommandResponse(uint16 cr1, uint16 cr2, uint16 cr3, uint16 cr4) final;
    void PartitionSync(uint8 index, const std::deque<ymir::cdblock::Buffer> &buffers) final;
    void PartitionInsertHead(uint8 index, const ymir::cdblock::Buffer &buffer) final;
    void PartitionRemoveTail(uint8 index, uint8 offset) final;
    void PartitionDeleteSectors(uint8 index, uint16 start, uint16 end) final;
    void PartitionClear(uint8 index) final;
    void Reset() final;
    void Detach() final;
};

} // namespace app
