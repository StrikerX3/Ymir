#pragma once

/**
@file
@brief Defines callbacks used by `CDInterface`.
*/

#include <ymir/util/callback.hpp>

namespace ymir::media {

/// @brief Type of callback invoked when the media is changed.
using CBOnMediaChanged = util::RequiredCallback<void()>;

} // namespace ymir::media
