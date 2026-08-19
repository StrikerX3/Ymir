#pragma once

#include "gfx_types.hpp"

#include <vector>

// -----------------------------------------------------------------------------
// Implementation

namespace app::gfx {

/// @brief Describes a graphics adapter in the system, enumerated with Metal.
struct MetalGraphicsAdapter {
    /// @brief Unique identifier for this adapter.
    AdapterID id;

    /// @brief Device description, typically the GPU's name.
    std::string name;

    /// @brief Pointer to the Metal device (id<MTLDevice>).
    void *device = nullptr;
};

/// @brief Enumerates graphics adapters in the system using Metal.
void EnumerateMetalGraphicsAdapters();

/// @brief Gets the graphics adapters present in the system enumerated with `EnumerateMetalGraphicsAdapters()`.
/// @return a list of graphics adapters. The first device in the list is the default adapter.
const std::vector<MetalGraphicsAdapter> &GetMetalGraphicsAdapters();

/// @brief Retrieves a Metal graphics adapter device pointer by its unique identifier.
/// @param[in] id the adapter's unique identifier
/// @return a pointer to the corresponding graphics adapter device (id<MTLDevice>) if found, `nullptr` otherwise
void *GetMetalDeviceByID(AdapterID id);

} // namespace app::gfx
