#include <ymir/sys/backup_ram_utils.hpp>

#include <array>

namespace ymir::bup {

std::string TranslateBackupString(std::string_view str, bool halfWidth) {
    static constexpr std::array<const char *, 256> kFullWidthTable = {
#include "bup_char_table_full.inc"
    };
    static constexpr std::array<const char *, 256> kHalfWidthTable = {
#include "bup_char_table_half.inc"
    };

    auto &table = halfWidth ? kHalfWidthTable : kFullWidthTable;

    std::string output{};
    output.reserve(str.size());
    for (char ch : str) {
        output += table[static_cast<unsigned char>(ch)];
    }
    return output;
}

} // namespace ymir::bup
