#pragma once

/**
@file
@brief Ymir library version definitions.
*/

#if Ymir_DEV_BUILD
    #define Ymir_FULL_VERSION Ymir_VERSION "-dev"
    #define Ymir_DEV_BUILD 1
    #ifdef Ymir_BUILD_TIMESTAMP
        #define Ymir_NIGHTLY_BUILD 1
    #else
        #define Ymir_NIGHTLY_BUILD 0
    #endif
#else
    #define Ymir_FULL_VERSION Ymir_VERSION
    #define Ymir_DEV_BUILD 0
    #define Ymir_NIGHTLY_BUILD 0
#endif

#define Ymir_STABLE_BUILD !Ymir_DEV_BUILD
#define Ymir_LOCAL_BUILD (Ymir_DEV_BUILD && !Ymir_NIGHTLY_BUILD)

namespace ymir::version {

/// @brief The library version string in the format "<major>.<minor>.<patch>[-<prerelease>][+<build>]".
///
/// The string follows the Semantic Versioning system, with mandatory major, minor and patch numbers and optional
/// prerelease and build components.
inline constexpr auto string = Ymir_VERSION;

/// @brief The library version string in the format "<major>.<minor>.<patch>[-<prerelease>][+<build>][-dev]".
///
/// The string mostly follows the Semantic Versioning system, with mandatory major, minor and patch numbers and optional
/// prerelease and build components. It also adds a `-dev` suffix for development builds.
inline constexpr auto fullstring = Ymir_FULL_VERSION;

inline constexpr auto major = static_cast<unsigned>(Ymir_VERSION_MAJOR); ///< The library's major version
inline constexpr auto minor = static_cast<unsigned>(Ymir_VERSION_MINOR); ///< The library's minor version
inline constexpr auto patch = static_cast<unsigned>(Ymir_VERSION_PATCH); ///< The library's patch version
inline constexpr auto prerelease = Ymir_VERSION_PRERELEASE;              ///< The library's prerelease version
inline constexpr auto build = Ymir_VERSION_BUILD;                        ///< The library's build version

/// @brief Whether this is a development build.
/// This is only ever `false` for stable releases.
inline constexpr bool is_dev_build = Ymir_DEV_BUILD;

/// @brief Whether this is a nightly build.
/// `false` means it's either a stable build or a local build.
inline constexpr bool is_nightly_build = Ymir_NIGHTLY_BUILD;

/// @brief Whether this is a stable build.
inline constexpr bool is_stable_build = Ymir_STABLE_BUILD;

/// @brief Whether this is a local build (neither stable nor nightly).
inline constexpr bool is_local_build = Ymir_LOCAL_BUILD;

} // namespace ymir::version
