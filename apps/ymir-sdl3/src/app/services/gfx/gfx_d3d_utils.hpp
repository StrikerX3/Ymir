#pragma once

#include "gfx_types.hpp"

#include <ymir/core/types.hpp>

#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Forward declarations

struct IUnknown;

// -----------------------------------------------------------------------------
// Implementation

namespace app::gfx {

/// @brief Describes a graphics adapter in the system, enumerated with DXGI.
struct DXGIGraphicsAdapter {
    ~DXGIGraphicsAdapter();

    /// @brief Consistent unique identifier for this adapter.
    AdapterID id;

    /// @brief PCI device identifier.
    struct PCI {
        uint32 vendorID;
        uint32 deviceID;
        uint32 subsystemID;
        uint32 revision;
    } pci;

    /// @brief Device description, typically the GPU's name.
    std::wstring description;

    /// @brief Memory sizes.
    struct Memory {
        size_t dedicatedVideo;
        size_t dedicatedSystem;
        size_t sharedSystem;
    } memory;

    /// @brief Locally unique identifier for this adapter.
    /// Can be used with some Windows graphics APIs to query additional information about the adapter.
    /// DO NOT USE as a primary key for the device - it is not consistent across reboots.
    uint64 luid;

    /// @brief Pointer to DXGI adapter.
    IUnknown *adapter = nullptr;
};

/// @brief Enumerates graphics adapters in the system using DXGI. Excludes software devices.
void EnumerateDXGIGraphicsAdapters();

/// @brief Gets the graphics adapters present in the system enumerated with `EnumerateGraphicsAdapters()`.
/// @return a list of graphics adapters. The first device in the list is the default adapter.
const std::vector<DXGIGraphicsAdapter> &GetDXGIGraphicsAdapters();

/// @brief Retrieves a DXGI graphics adapter by its unique identifer.
/// @param[in] id the adapter's unique identifier
/// @return a pointer to the corresponding graphics adapter if found, `nullptr` otherwise
IUnknown *GetDXGIGraphicsAdapterByID(AdapterID id);

} // namespace app::gfx
