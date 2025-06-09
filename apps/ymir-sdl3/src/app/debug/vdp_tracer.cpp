#include "vdp_tracer.hpp"

namespace app {

void VDPTracer::BeginFrame(const ymir::vdp::VDPState &state) {
    {
        std::unique_lock lock{m_mtxState};
        m_latestState = state;
    }
    ++m_frameCounter;
}

} // namespace app
