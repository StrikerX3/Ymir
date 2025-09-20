#pragma once

/**
@file
@brief Structured C-style callback classes.

This file defines two primary callback types: `RequiredCallback` and `OptionalCallback`. They both implement C-style
callback functions with a context (or user data) `void` pointer. The difference between the *required* and *optional*
variants is that the former omits a `nullptr` check for performance. When attempting to assign a `nullptr` function to a
required callback, the class automatically assigns a default no-op function that returns the default value instead. This
requires the return type to have an accessible default constructor.

The callback function signature is encoded as a template type parameter. The context pointer is passed as the last
argument to the callback function.

Callbacks can be used with any C-style function pointer. The header provides helper functions to construct callbacks
from class member function pointers.

Usage example:

```cpp
void ReceiveCallback(int x, void *ctx) {
    // ctx has the user data pointer
    // do something with x
}

// Construct an optional callback from a freestanding function with no context pointer
util::OptionalCallback<void(int)> myCallback{ReceiveCallback};

struct MyType {
    void ReceiveCallback(int x) {
        // do something with x
    }
};

// Construct a required callback from a member function pointer bound to a particular instance of MyType
MyType instance{};
util::RequiredCallback<void(int)> myRequiredCallback =
    util::MakeClassMemberRequiredCallback<&MyType::ReceiveCallback>(&instance);

// Reassign the required callback to another function
myRequiredCallback = {ReceiveCallback};
```
*/

#include <cassert>
#include <functional>
#include <type_traits>
#include <utility>

#include "inline.hpp"

namespace util {

namespace detail {

    /// @brief A type-dependent always false value to use in static assertions.
    /// @tparam T the type
    template <typename T>
    inline constexpr bool alwaysFalse = false;

    /// @brief The callback implementation.
    /// @tparam TReturn the return type of the callback function
    /// @tparam ...TArgs the argument types of the callback function
    /// @tparam skipNullCheck whether to skip the `nullptr` check for performance
    template <bool skipNullCheck, typename TReturn, typename... TArgs>
    struct FuncClass {
        /// @brief The function pointer type, constructed from the class's template types.
        using FnType = TReturn (*)(TArgs... args, void *context);

        /// @brief Constructs an unbound callback.
        FuncClass() {
            Rebind(nullptr);
        }

        /// @brief Constructs a callback bound to the given function and context pointers.
        /// @param[in] context the context (user data) pointer
        /// @param[in] fn the function pointer
        FuncClass(void *context, FnType fn) {
            Rebind(context, fn);
        }

        FuncClass(const FuncClass &) = default; ///< Default copy constructor
        FuncClass(FuncClass &&) = default;      ///< Default move constructor

        FuncClass &operator=(const FuncClass &) = default; ///< Default copy assignment operator
        FuncClass &operator=(FuncClass &&) = default;      ///< Default move assignment operator

        /// @brief Rebinds the callback to the given function pointer and sets the context pointer to `nullptr`.
        /// @param[in] fn the function pointer
        void Rebind(FnType fn) {
            Rebind(nullptr, fn);
        }

        /// @brief Rebinds the callback to the given context and function pointers.
        /// @param[in] context the context (user data) pointer
        /// @param[in] fn the function pointer
        void Rebind(void *context, FnType fn) {
            m_context = context;
            if (skipNullCheck && fn == nullptr) {
                m_fn = [](TArgs..., void *) -> TReturn {
                    if constexpr (!std::is_void_v<TReturn>) {
                        return TReturn{};
                    }
                };
            } else {
                m_fn = fn;
            }
        }

        /// @brief Invokes the callback function with the specified arguments.
        /// @param[in,out] ...args the arguments to pass to the callback function
        /// @return the return value of the callback function
        FLATTEN FORCE_INLINE TReturn operator()(TArgs... args) {
            if constexpr (!skipNullCheck) {
                if (m_fn == nullptr) [[unlikely]] {
                    if constexpr (std::is_void_v<TReturn>) {
                        return;
                    } else {
                        return {};
                    }
                }
            } else {
                assert(m_fn != nullptr);
            }
            return m_fn(std::forward<TArgs>(args)..., m_context);
        }

        /// @brief Invokes the callback function with the specified arguments.
        /// @param[in,out] ...args the arguments to pass to the callback function
        /// @return the return value of the callback function
        FLATTEN FORCE_INLINE TReturn operator()(TArgs... args) const {
            if constexpr (!skipNullCheck) {
                if (m_fn == nullptr) [[unlikely]] {
                    if constexpr (std::is_void_v<TReturn>) {
                        return;
                    } else {
                        return {};
                    }
                }
            } else {
                assert(m_fn != nullptr);
            }
            return m_fn(std::forward<TArgs>(args)..., m_context);
        }

    private:
        void *m_context; ///< The context (user data) pointer
        FnType m_fn;     ///< The callback function pointer
    };

    /// @brief The default case for a function helper type that rejects `TFunc` if it's not a function type.
    /// @tparam TFunc the (not-)function type
    /// @tparam skipNullCheck whether to skip the `nullptr` check for performance
    template <bool skipNullCheck, typename TFunc>
    struct FuncHelper {
        static_assert(alwaysFalse<TFunc>, "Callback requires a function argument");
    };

    /// @brief A function helper type that extracts the return and argument types from a function type.
    /// @tparam TReturn the return type of the function
    /// @tparam ...TArgs the arguments of the function
    /// @tparam skipNullCheck whether to skip the `nullptr` check for performance
    template <bool skipNullCheck, typename TReturn, typename... TArgs>
    struct FuncHelper<skipNullCheck, TReturn(TArgs...)> {
        using type = FuncClass<skipNullCheck, TReturn, TArgs...>;
    };

    /// @brief A C-style callback containing a function pointer and a context/user data pointer.
    /// @tparam TFunc the function type of the form `ReturnType(Args...)`
    /// @tparam skipNullCheck whether to skip the `nullptr` check when invoking the function pointer
    template <typename TFunc, bool skipNullCheck>
    class Callback : public detail::FuncHelper<skipNullCheck, TFunc>::type {
        using FnType = typename detail::FuncHelper<skipNullCheck, TFunc>::type::FnType;

    public:
        /// @brief Constructs an empty (no-op) callback.
        Callback() = default;

        /// @brief Constructs a callback from the given function pointer with a `nullptr` context.
        /// @param[in] fn the function pointer
        Callback(FnType fn)
            : detail::FuncHelper<skipNullCheck, TFunc>::type(nullptr, fn) {}

        /// @brief Constructs a callback from the given function and context pointers.
        /// @param[in] context the context (user data) pointer
        /// @param[in] fn the function pointer
        Callback(void *context, FnType fn)
            : detail::FuncHelper<skipNullCheck, TFunc>::type(context, fn) {}

        Callback(const Callback &) = default; ///< Default copy constructor
        Callback(Callback &&) = default;      ///< Default move constructor

        Callback &operator=(const Callback &) = default; ///< Default copy assignment operator
        Callback &operator=(Callback &&) = default;      ///< Default move assignment operator
    };

} // namespace detail

/// @brief Defines a *required* callback - one that is guaranteed to be always set to a valid function at runtime.
///
/// Callbacks include a context pointer (also called user data pointer) that is passed as the last argument to the
/// specified function call.
///
/// Setting the callback to `nullptr` uses an automatically defined no-op function that returns a default-constructed
/// value of the return type.
///
/// @note This callback omits the `nullptr` check for performance.
///
/// @tparam TFunc the function type of the form `ReturnType(Args...)`
template <typename TFunc>
using RequiredCallback = detail::Callback<TFunc, true>;

/// @brief Defines an *optional* callback which may be set to `nullptr` to disable it, or a valid function pointer to
/// enable it.
///
/// Callbacks include a context pointer (also called user data pointer) that is passed as the last argument to the
/// specified function call.
///
/// @tparam TFunc the function type of the form `ReturnType(Args...)`
template <typename TFunc>
using OptionalCallback = detail::Callback<TFunc, false>;

// -------------------------------------------------------------------------------------------------

namespace detail {

    /// @brief Helper type that constructs a `Callback` object from a member function pointer.
    template <typename>
    struct MFPCallbackMaker;

    /// @brief Helper type that constructs a `Callback` object from a member function pointer.
    /// @tparam Return the return type of the function
    /// @tparam Object the type of the object
    /// @tparam ...Args the types of the function's arguments
    template <typename Return, typename Object, typename... Args>
    struct MFPCallbackMaker<Return (Object::*)(Args...)> {
        using class_type = Object;

        template <Return (Object::*mfp)(Args...), bool skipNullCheck>
        static auto GetCallback(Object *context) {
            return Callback<Return(Args...), skipNullCheck>{context, [](Args... args, void *context) {
                                                                auto &obj = *static_cast<Object *>(context);
                                                                return (obj.*mfp)(std::forward<Args>(args)...);
                                                            }};
        }
    };

    /// @brief Helper type that constructs a `Callback` object from a `const`-qualified member function pointer.
    /// @tparam Return the return type of the function
    /// @tparam Object the type of the object
    /// @tparam ...Args the types of the function's arguments
    template <typename Return, typename Object, typename... Args>
    struct MFPCallbackMaker<Return (Object::*)(Args...) const> {
        using class_type = Object;

        template <Return (Object::*mfp)(Args...), bool skipNullCheck>
        static auto GetCallback(Object *context) {
            return Callback<Return(Args...), skipNullCheck>{context, [](Args... args, void *context) {
                                                                auto &obj = *static_cast<Object *>(context);
                                                                return (obj.*mfp)(std::forward<Args>(args)...);
                                                            }};
        }
    };

} // namespace detail

/// @brief Creates a required callback to a member function pointer that will be invoked on the given instance of the
/// class.
/// @tparam mfp the member function pointer
/// @param[in] context the object instance to bind to
/// @return a `RequiredCallback` bound to the object instance and member function pointer
template <auto mfp>
    requires std::is_member_function_pointer_v<decltype(mfp)>
auto MakeClassMemberRequiredCallback(typename detail::MFPCallbackMaker<decltype(mfp)>::class_type *context) {
    return detail::MFPCallbackMaker<decltype(mfp)>::template GetCallback<mfp, true>(context);
}

/// @brief Creates an optional callback to a member function pointer that will be invoked on the given instance of the
/// class.
/// @tparam mfp the member function pointer
/// @param[in] context the object instance to bind to
/// @return an `OptionalCallback` bound to the object instance and member function pointer
template <auto mfp>
    requires std::is_member_function_pointer_v<decltype(mfp)>
auto MakeClassMemberOptionalCallback(typename detail::MFPCallbackMaker<decltype(mfp)>::class_type *context) {
    return detail::MFPCallbackMaker<decltype(mfp)>::template GetCallback<mfp, false>(context);
}

} // namespace util
