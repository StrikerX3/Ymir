#pragma once

/**
@file
@brief Defines common result objects for GPU operations.
*/

#include <ymir/util/result.hpp>

#include <memory>
#include <string>

namespace ymir::gpu {

/// @brief Contains the error message received when attempting to perform an operation with a GPU object.
struct GPUOperationError {
    std::string message;
};

/// @brief Convenience wrapper for GPU operations that return an owning pointer to an object.
/// @tparam T the value type
template <typename T>
using GPUObjectResult = util::Result<std::unique_ptr<T>, GPUOperationError>;

/// @brief Convenience wrapper for GPU operations that return a non-owning pointer to an object.
/// @tparam T the value type
template <typename T>
using GPUPointerResult = util::Result<T *, GPUOperationError>;

/// @brief Convenience wrapper for GPU operations that return a value.
/// @tparam T the value type
template <typename T>
using GPUValueResult = util::Result<T, GPUOperationError>;

} // namespace ymir::gpu
