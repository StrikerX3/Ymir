#pragma once

#include "vdp_renderer.hpp"
#include "vdp_state.hpp"

namespace ymir::vdp {

/// @brief Renders a VDP1+2 frame from an initial state and a series of modifications.
class VDPDebugRenderer {
public:
    VDPState State; ///< The initial VDP1+2 state

    void SetRenderCallback(CBFrameComplete callback);
    void SetVDP1Callback(CBVDP1FrameComplete callback);

    void Render();

private:
    VDPState m_state;
    VDPRenderer m_renderer{m_state};
};

} // namespace ymir::vdp
