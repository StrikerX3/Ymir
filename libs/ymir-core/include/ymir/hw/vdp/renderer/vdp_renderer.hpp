#pragma once

#include "vdp_renderer_null.hpp"
#include "vdp_renderer_sw.hpp"
#if YMIR_PLATFORM_HAS_DIRECT3D
    #include "vdp_renderer_hw_d3d12.hpp"
#endif
