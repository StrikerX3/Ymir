#pragma once

#include "vdp_renderer_null.hpp"
#include "vdp_renderer_sw.hpp"
#ifdef YMIR_HAS_D3D11
    #include "vdp_renderer_d3d11.hpp"
#endif
