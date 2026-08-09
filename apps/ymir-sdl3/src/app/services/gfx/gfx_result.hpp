#pragma once

/**
@file
@brief Defines common result objects for graphics operations.
*/

#include <ymir/util/result.hpp>

#include <memory>
#include <string>

namespace app::gfx {

/// @brief Contains the error message received when attempting to perform an operation with a graphics object.
struct GfxOperationError {
    std::string message;
};

/// @brief Convenience wrapper for graphics operations that may return an owning pointer to an object or produce an
/// error.
/// @tparam T the value type
template <typename T>
using GfxObjectResult = util::Result<std::unique_ptr<T>, GfxOperationError>;

/// @brief Convenience wrapper for graphics operations that may return a non-owning pointer to an object or produce an
/// error.
/// @tparam T the value type
template <typename T>
using GfxPointerResult = util::Result<T *, GfxOperationError>;

/// @brief Convenience wrapper for graphics operations that may return a value or produce an error.
/// @tparam T the value type
template <typename T>
using GfxValueResult = util::Result<T, GfxOperationError>;

/// @brief Convenience wrapper for graphics operations that don't return a value, but may generate errors.
using GfxResult = util::Result<void, GfxOperationError>;

} // namespace app::gfx
