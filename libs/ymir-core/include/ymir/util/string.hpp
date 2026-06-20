#pragma once

#include <string>
#include <string_view>

namespace util {

/// @brief Translates a Saturn string with Japanese characters into a UTF-8 encoded string.
/// @param[in] str the string to translate
/// @return the string translated to UTF-8
std::string TranslateSaturnString(std::string_view str);

} // namespace util
