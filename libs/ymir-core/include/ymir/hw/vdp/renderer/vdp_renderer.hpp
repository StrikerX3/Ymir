#pragma once

#include "vdp_renderer_null.hpp"
#include "vdp_renderer_sw.hpp"

#include "vdp_renderer_hw_base.hpp"
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    #include "vdp_renderer_d3d11.hpp"
#endif
