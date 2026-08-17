#pragma once

#include <string>
#include <string_view>

namespace util {

/// @brief Translates a Saturn string with Japanese characters into a UTF-8 encoded string.
/// @param[in] str the string to translate
/// @return the string translated to UTF-8
std::string TranslateSaturnString(std::string_view str);

/// @brief Trims leading and trailing whitespace characters from the string.
/// @param[in] str the string to trim
/// @return a new string with whitespace characters (0x20, ' ') removed from the front and back.
std::string TrimWhitespace(std::string str);

/// @brief Converts the given UTF-8-encoded string to a wide string.
/// @param[in] str the string to convert
/// @return the string converted to `std::wstring`
std::wstring StringToWString(std::string_view str);

/// @brief Converts the given wide string to a UTF-8-encoded string.
/// @param[in] wstr the wide string to convert
/// @return the wide string converted to `std::string`
std::string WStringToString(std::wstring_view wstr);

} // namespace util
