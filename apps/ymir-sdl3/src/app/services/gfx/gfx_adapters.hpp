#pragma once

#include "gfx_types.hpp"

#include <vector>

namespace app::gfx {

/// @brief Retrieves a list of graphics adapters for the specified backend.
/// @param[in] backend the graphics backend
/// @return a list of graphics adapters for the given backend
std::vector<Adapter> GetGraphicsAdapters(Backend backend);

/// @brief Refreshes the graphics adapters list for the specified backend
/// @param[in] backend the graphics backend
void RefreshGraphicsAdapters(Backend backend);

} // namespace app::gfx
