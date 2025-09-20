#include <ymir/db/cdb_rom_db.hpp>

#include <unordered_map>

namespace ymir::db {

// clang-format off
static const std::unordered_map<XXH128Hash, CDBlockROMInfo> kCDBlockROMInfos = {
    {MakeXXH128Hash(0x56E1DBE90A499DA7,0x1BD11A845445188A), {"1.05"}},
    {MakeXXH128Hash(0xA2A824298D3ACFFC,0x3D1CEC215D8531F0), {"1.06"}},
    {MakeXXH128Hash(0x12D7086732B5CC54,0x146D5A7B5223C96B), {"1.06 (alt)"}},
};
// clang-format on

const CDBlockROMInfo *GetCDBlockROMInfo(XXH128Hash hash) {
    if (kCDBlockROMInfos.contains(hash)) {
        return &kCDBlockROMInfos.at(hash);
    } else {
        return nullptr;
    }
}

} // namespace ymir::db
