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
@param[in] condition the condition to check
*/

/**
@def YMIR_DEV_CHECK
@brief Breaks into the debugger immediately.
*/

#if Ymir_DEV_ASSERTIONS
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
#else
    #define YMIR_DEV_ASSERT(cond)
    #define YMIR_DEV_CHECK()
#endif
