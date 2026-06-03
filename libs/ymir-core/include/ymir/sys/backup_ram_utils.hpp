#pragma once

#include <string>
#include <string_view>

namespace ymir::bup {

/// @brief Translates a string from backup RAM into a UTF-8 encoded string.
/// @param[in] str the string to translate
/// @return the string translated to UTF-8
std::string TranslateBackupString(std::string_view str);

} // namespace ymir::bup
