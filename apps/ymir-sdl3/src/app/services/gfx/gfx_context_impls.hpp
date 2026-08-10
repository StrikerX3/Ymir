#pragma once

/**
@file
@brief Includes all graphics context implementations present on the current platform.
*/

#include "gfx_context_impl_null.hpp"
#include "gfx_context_impl_sdl_renderer.hpp"

#if YMIR_PLATFORM_HAS_DIRECT3D
    #include "gfx_context_impl_d3d11.hpp"
    #include "gfx_context_impl_d3d12.hpp"
#endif

#if YMIR_PLATFORM_HAS_VULKAN
    #include "gfx_context_impl_vulkan.hpp"
#endif

#if YMIR_PLATFORM_HAS_METAL
    #include "gfx_context_impl_metal.hpp"
#endif
