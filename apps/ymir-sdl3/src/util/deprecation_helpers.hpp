// Common deprecation annotation helper.
#pragma once

#if defined(__clang__) || defined(__GNUC__)
    #define YMIR_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
    #define YMIR_DEPRECATED(msg) __declspec(deprecated(msg))
#else
    #define YMIR_DEPRECATED(msg)
#endif

