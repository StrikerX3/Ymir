#pragma once

/**
@file
@brief Defines the contents of `app::gfx::Direct3D12GraphicsContextSpec`, which depends on Direct3D 12 headers.

This separation limits DirectX and Windows headers inclusion scope on the code base.
*/

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <d3dcommon.h>

#include <Windows.h>

namespace app::gfx {

struct Direct3D12GraphicsContextSpec {
    /// @brief (Required) Target feature level.
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

    /// @brief (Required) Pointer to window
    HWND hwnd = nullptr;

    /// @brief (Optional) Target adapter. Defaults to the primary display adapter on the system if not specified.
    IUnknown *adapter = nullptr;

    /// @brief Debug parameters.
    struct Debug {
        /// @brief Enables debug diagnostics.
        bool enabled = false;

        /// @brief Enters the debugger on all DirectX warnings.
        bool breakOnWarnings = false;
    } debug;
};

} // namespace app::gfx
