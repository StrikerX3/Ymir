#include "ymir/bus/busarb.hpp"

#include <algorithm>
#include <cassert>

namespace busarb {

Arbiter::Arbiter(TimingCallbacks callbacks, ArbiterConfig config) : callbacks_(callbacks), config_(config) {
    assert(callbacks_.access_cycles != nullptr && "TimingCallbacks.access_cycles must be non-null");
}

BusWaitResult Arbiter::query_wait(const BusRequest &req) const {
    if (req.now_tick >= bus_free_tick_) {
        return BusWaitResult{false, 0U};
    }
    const std::uint64_t delta = bus_free_tick_ - req.now_tick;
    return BusWaitResult{true, static_cast<std::uint32_t>(std::min<std::uint64_t>(delta, 0xFFFFFFFFULL))};
}

void Arbiter::commit_grant(const BusRequest &req, std::uint64_t tick_start, bool had_tie) {
    const std::uint64_t actual_start = std::max(tick_start, bus_free_tick_);
    std::uint64_t duration = service_cycles(req);
    if (has_last_granted_addr_ && req.addr == last_granted_addr_) {
        duration += config_.same_address_contention;
    }
    if (had_tie) {
        duration += config_.tie_turnaround;
    }
    bus_free_tick_ = actual_start + duration;
    has_last_granted_addr_ = true;
    last_granted_addr_ = req.addr;
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
    return bus_free_tick_;
}

std::uint32_t Arbiter::service_cycles(const BusRequest &req) const {
    const std::uint32_t cycles = callbacks_.access_cycles(callbacks_.ctx, req.addr, req.is_write, req.size_bytes);
    return std::max(1U, cycles);
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
