#pragma once

/**
@file
@brief Macros for managing function inlining and flattening.

This header defines the following macros:
- `FORCE_INLINE` and `FORCE_INLINE_EX`: Forces function inlining and marks the function `inline`
- `NO_INLINE`: Prevents function inlining
- `FLATTEN` and `FLATTEN_EX`: Flattens the function

The `FORCE_INLINE_EX` and `FLATTEN_EX` are meant to be used in functions that heavily slow down compilation if inlined
or flattened. If `Ymir_EXTRA_INLINING` is defined, the macros behave exactly like their non-`EX` counterparts, otherwise
they do nothing.

In Debug builds, these macros have no effect in order to not disrupt the debugging experience. The inline macros can be
disabled by defining `Ymir_DISABLE_FORCE_INLINE`. `Ymir_EXTRA_INLINING` has no effect in debug builds.

Note that `FORCE_INLINE` always marks the function `inline` even when disabled.

The macros use appropriate attributes for MSVC, Clang and GCC. For any other compiler, these macros do nothing.
*/

/**
@def FORCE_INLINE
@brief Forces function inlining and marks the function `inline`.
*/

/**
@def NO_INLINE
@brief Prevents function inlining.
*/

/**
@def FLATTEN
@brief Flattens the function.

Essentially inlines all functions called by the flattened function.
*/

/**
@def FORCE_INLINE_EX
@brief If `Ymir_EXTRA_INLINING` is defined, forces function inlining and marks the function `inline`.

For use with functions that heavily impact build times.
*/

/**
@def FLATTEN
@brief If `Ymir_EXTRA_INLINING` is defined, flattens the function.

Essentially inlines all functions called by the flattened function.

For use with functions that heavily impact build times.
*/

#if !defined(NDEBUG) || defined(Ymir_DISABLE_FORCE_INLINE)
    #define FORCE_INLINE inline
    #define NO_INLINE
    #define FLATTEN
#elif defined(__clang__)
    #define FORCE_INLINE [[gnu::always_inline]] inline
    #define NO_INLINE [[gnu::noinline]]
    #define FLATTEN [[gnu::flatten]]
#elif (defined(__GNUC__) || defined(__GNUG__))
    #define FORCE_INLINE [[gnu::always_inline]] inline
    #define NO_INLINE [[gnu::noinline]]
    #define FLATTEN // GCC dies when [[gnu::flatten]] is (ab)used
#elif defined(_MSC_VER)
    #define FORCE_INLINE [[msvc::forceinline]] inline
    #define NO_INLINE [[msvc::noinline]]
    #define FLATTEN // MSVC dies when [[msvc::flatten]] is (ab)used
#else
    #define FORCE_INLINE inline
    #define NO_INLINE
    #define FLATTEN
#endif

#if Ymir_EXTRA_INLINING
    #define FORCE_INLINE_EX FORCE_INLINE
    #define FLATTEN_EX FLATTEN
#else
    #define FORCE_INLINE_EX inline
    #define FLATTEN_EX
#endif
