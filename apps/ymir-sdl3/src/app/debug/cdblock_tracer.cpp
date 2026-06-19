#include "cdblock_tracer.hpp"

namespace app {

void CDBlockTracer::ClearCommands() {
    commands.Clear();
    m_commandCounter = 0;
}

void CDBlockTracer::ProcessCommand(uint16 cr1, uint16 cr2, uint16 cr3, uint16 cr4) {
    if (!traceCommands) {
        return;
    }

    commands.Write({.index = m_commandCounter++, .request = {cr1, cr2, cr3, cr4}, .processed = false});
}

void CDBlockTracer::ProcessCommandResponse(uint16 cr1, uint16 cr2, uint16 cr3, uint16 cr4) {
    if (!traceCommands) {
        return;
    }

    auto &cmd = commands.GetLast();
    cmd.response[0] = cr1;
    cmd.response[1] = cr2;
    cmd.response[2] = cr3;
    cmd.response[3] = cr4;
    cmd.processed = true;
}

void CDBlockTracer::PartitionResetAll() {
    for (auto &partition : partitions) {
        partition.clear();
    }
}

void CDBlockTracer::PartitionSync(uint8 index, const std::deque<ymir::cdblock::Buffer> &buffers) {
    std::unique_lock lock{mtxPartitions};
    partitions[index] = buffers;
}

void CDBlockTracer::PartitionInsertHead(uint8 index, const ymir::cdblock::Buffer &buffer) {
    std::unique_lock lock{mtxPartitions};
    partitions[index].push_back(buffer);
}

void CDBlockTracer::PartitionRemoveTail(uint8 index, uint8 offset) {
    std::unique_lock lock{mtxPartitions};
    auto &partition = partitions[index];
    partition.erase(partition.begin() + offset);
}

void CDBlockTracer::PartitionDeleteSectors(uint8 index, uint16 start, uint16 end) {
    std::unique_lock lock{mtxPartitions};
    auto &partition = partitions[index];
    partition.erase(partition.begin() + start, partition.begin() + end + 1);
}

void CDBlockTracer::PartitionClear(uint8 index) {
    std::unique_lock lock{mtxPartitions};
    partitions[index].clear();
}

void CDBlockTracer::Detach() {
    PartitionResetAll();
}

} // namespace app
