#pragma once

#include <ymir/debug/vdp_tracer_base.hpp>

#include <mutex>

namespace app {

struct VDPTracer final : ymir::debug::IVDPTracer {
    void CopyLatestState(ymir::vdp::VDPState &out) const {
        std::unique_lock lock{m_mtxState};
        out = m_latestState;
    }

private:
    uint64 m_frameCounter = 0;

    ymir::vdp::VDPState m_latestState;
    mutable std::mutex m_mtxState;

    void BeginFrame(const ymir::vdp::VDPState &state) final;
};

} // namespace app
