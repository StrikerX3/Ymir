#pragma once

/**
@file
@brief Defines `util::ScopeGuard`, an utility type that executes code on scope exit.
*/

#include <type_traits>
#include <utility>

namespace util {

/// @brief A type that performs an operation on scope exit.
///
/// This type can be used to clean up C-style resources in a RAII-like manner.
///
/// Create a local `ScopeGuard` variable within the scope you wish to clean up with the following idiom:
///
/// ```cpp
/// void *data = malloc(size);
/// ScopeGuard sgCleanupData{[&] { free(data); }};
/// ```
///
/// `ScopeGuard`s can be cancelled, which is useful in cases where resource cleanup is desired only in case of failure:
/// ```cpp
/// // Create resource 1
/// Resource *res1 = CreateResource(1);
/// if (!res1) {
///     Fail();
///     return;
/// }
/// ScopeGuard sgCleanupRes1{[&] { FreeResource(res1); };
///
/// // Create resource 2
/// Resource *res2 = CreateResource(2);
/// if (!res2) {
///     Fail();
///     return;
/// }
/// ScopeGuard sgCleanupRes2{[&] { FreeResource(res2); };
///
/// // ... and so on
///
/// // All resources have been created successfully; use them
/// UseResources(res1, res2, ...);
///
/// // Cancel the scope guards to prevent them from cleaning up the resources that are now in use
/// sgCleanupRes1.Cancel();
/// sgCleanupRes2.Cancel();
/// // ...
/// ```
///
/// @tparam Fn the type of the scope guard function
template <typename Fn>
class ScopeGuard {
public:
    /// @brief Creates a scope guard with the given function.
    /// @param[in] fn the function
    ScopeGuard(Fn &&fn) noexcept
        : fn(std::move(fn)) {}

    /// @brief Invokes the scope guard function if not cancelled.
    ~ScopeGuard() noexcept(noexcept(fn())) {
        if (!cancelled) {
            fn();
        }
    }

    /// @brief Cancels the scope guard.
    void Cancel() noexcept {
        cancelled = true;
    }

private:
    Fn fn;                  ///< The scope guard function
    bool cancelled = false; ///< Whether the scope guard has been cancelled
};

} // namespace util
