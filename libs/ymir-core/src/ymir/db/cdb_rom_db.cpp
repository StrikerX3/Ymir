#include <ymir/db/cdb_rom_db.hpp>

#include <unordered_map>

namespace ymir::db {

// clang-format off
static const std::unordered_map<XXH128Hash, CDBlockROMInfo> kCDBlockROMInfos = {
    {MakeXXH128Hash(0x0000000000000000,0x0000000000000001), {"1.05"}},
    {MakeXXH128Hash(0x0000000000000000,0x0000000000000002), {"1.06"}},
    {MakeXXH128Hash(0x0000000000000000,0x0000000000000003), {"1.06 (alt)"}},
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
