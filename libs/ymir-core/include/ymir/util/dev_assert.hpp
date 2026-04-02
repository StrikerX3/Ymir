#pragma once

/**
@file
@brief Development-time assertions.

Defines the following macros:
- `YMIR_DEV_ASSERT(bool)`: checks for a precondition, breaking into the debugger if it fails.
- `YMIR_DEV_CHECK()`: breaks into the debugger immediately.

These macros are useful to check for unexpected or unimplemented cases.

Development assertions must be enabled by defining the `Ymir_DEV_ASSERTIONS` macro with a truthy value.
*/

/**
@def YMIR_DEV_ASSERT
@brief Performs a development-time assertion, breaking into the debugger if the condition fails.
@param[in] cond the condition to check
*/

/**
@def YMIR_DEV_CHECK
@brief Breaks into the debugger immediately.
*/

#if Ymir_DEV_ASSERTIONS
    // Workaround for MSVC C4067 warning - MSVC does not have __has_builtin
    #if defined(__has_builtin)
        #if __has_builtin(__builtin_debugtrap)
            #define Ymir_DEV_ASSERT_USE_INTRINSIC
        #endif
    #endif
    #if defined(Ymir_DEV_ASSERT_USE_INTRINSIC)
        #undef Ymir_DEV_ASSERT_USE_INTRINSIC
        #define YMIR_DEV_ASSERT(cond)      \
            do {                           \
                if (!(cond)) {             \
                    __builtin_debugtrap(); \
                }                          \
            } while (false)

        #define YMIR_DEV_CHECK()       \
            do {                       \
                __builtin_debugtrap(); \
            } while (false)
    #elif defined(_MSC_VER)
        #define YMIR_DEV_ASSERT(cond) \
            do {                      \
                if (!(cond)) {        \
                    __debugbreak();   \
                }                     \
            } while (false)

        #define YMIR_DEV_CHECK() \
            do {                 \
                __debugbreak();  \
            } while (false)
    #endif
#else
    #define YMIR_DEV_ASSERT(cond)
    #define YMIR_DEV_CHECK()
#endif
