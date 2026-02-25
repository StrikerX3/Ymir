#include "ymir/bus/busarb.hpp"

#include <algorithm>
#include <cassert>

namespace busarb {

Arbiter::Arbiter(TimingCallbacks callbacks, ArbiterConfig config) : callbacks_(callbacks), config_(config) {
    assert(callbacks_.access_cycles != nullptr && "TimingCallbacks.access_cycles must be non-null");
}

BusWaitResult Arbiter::query_wait(const BusRequest &req) const {
    const DomainState &state = domain_state(req.addr);
    if (req.now_tick >= state.bus_free_tick) {
        return BusWaitResult{false, 0U};
    }
    const std::uint64_t delta = state.bus_free_tick - req.now_tick;
    return BusWaitResult{true, static_cast<std::uint32_t>(std::min<std::uint64_t>(delta, 0xFFFFFFFFULL))};
}

void Arbiter::commit_grant(const BusRequest &req, std::uint64_t tick_start, bool had_tie) {
    DomainState &state = domain_state(req.addr);
    const std::uint64_t actual_start = std::max(tick_start, state.bus_free_tick);
    std::uint64_t duration = service_cycles(req);
    if (state.has_last_granted_addr && req.addr == state.last_granted_addr && state.last_granted_master.has_value() &&
        *state.last_granted_master != req.master_id) {
        duration += config_.same_address_contention;
    }
    if (had_tie) {
        duration += config_.tie_turnaround;
    }
    state.bus_free_tick = actual_start + duration;
    state.has_last_granted_addr = true;
    state.last_granted_addr = req.addr;
    state.last_granted_master = req.master_id;
    if (req.master_id == BusMasterId::SH2_A || req.master_id == BusMasterId::SH2_B) {
        last_granted_cpu_ = req.master_id;
    }
}

std::optional<std::size_t> Arbiter::pick_winner(const std::vector<BusRequest> &same_tick_requests) const {
    if (same_tick_requests.empty()) {
        return std::nullopt;
    }

    std::size_t best = 0U;
    for (std::size_t i = 1; i < same_tick_requests.size(); ++i) {
        const auto &cand = same_tick_requests[i];
        const auto &cur = same_tick_requests[best];

        const int cprio = priority(cand.master_id);
        const int bprio = priority(cur.master_id);
        if (cprio > bprio) {
            best = i;
            continue;
        }
        if (cprio < bprio) {
            continue;
        }

        if (cand.master_id != BusMasterId::DMA && cur.master_id != BusMasterId::DMA && cand.master_id != cur.master_id) {
            BusMasterId preferred = BusMasterId::SH2_A;
            if (last_granted_cpu_.has_value()) {
                preferred = (*last_granted_cpu_ == BusMasterId::SH2_A) ? BusMasterId::SH2_B : BusMasterId::SH2_A;
            }
            if (cand.master_id == preferred) {
                best = i;
            }
            continue;
        }

        if (static_cast<int>(cand.master_id) < static_cast<int>(cur.master_id)) {
            best = i;
            continue;
        }
        if (cand.master_id != cur.master_id) {
            continue;
        }

        if (cand.addr < cur.addr) {
            best = i;
            continue;
        }
        if (cand.addr > cur.addr) {
            continue;
        }

        if (cand.is_write != cur.is_write && cand.is_write) {
            best = i;
            continue;
        }

        if (cand.size_bytes < cur.size_bytes) {
            best = i;
        }
    }
    return best;
}

std::uint64_t Arbiter::bus_free_tick() const {
    std::uint64_t max_tick = 0;
    for (const DomainState &state : domain_states_) {
        max_tick = std::max(max_tick, state.bus_free_tick);
    }
    return max_tick;
}

std::uint64_t Arbiter::bus_free_tick(std::uint32_t addr) const {
    return domain_state(addr).bus_free_tick;
}

std::uint32_t Arbiter::service_cycles(const BusRequest &req) const {
    const std::uint32_t cycles = callbacks_.access_cycles(callbacks_.ctx, req.addr, req.is_write, req.size_bytes);
    return std::max(1U, cycles);
}

std::size_t Arbiter::domain_index(std::uint32_t addr) {
    if (addr <= 0x00F'FFFF) {
        return 0; // BIOS ROM
    }
    if (addr >= 0x020'0000 && addr <= 0x02F'FFFF) {
        return 1; // WRAM-L
    }
    if (addr >= 0x200'0000 && addr <= 0x4FF'FFFF) {
        return 2; // A-Bus CS0/CS1
    }
    if (addr >= 0x600'0000 && addr <= 0x7FF'FFFF) {
        return 3; // WRAM-H
    }
    return 4; // fallback/unmanaged
}

Arbiter::DomainState &Arbiter::domain_state(std::uint32_t addr) {
    return domain_states_[domain_index(addr)];
}

const Arbiter::DomainState &Arbiter::domain_state(std::uint32_t addr) const {
    return domain_states_[domain_index(addr)];
}

int Arbiter::priority(BusMasterId id) {
    switch (id) {
    case BusMasterId::DMA: return 2;
    case BusMasterId::SH2_A: return 1;
    case BusMasterId::SH2_B: return 1;
    }
    return 0;
}

} // namespace busarb
