#include <ymir/db/game_db.hpp>

#include <unordered_map>

namespace ymir::db {

// Table adapted from Mednafen
// https://github.com/libretro-mirrors/mednafen-git/blob/master/src/ss/db.cpp

// clang-format off
static const std::unordered_map<std::string_view, GameInfo> kGameInfosByCode = {
    {"MK-81088", {.cartridge = Cartridge::ROM_KOF95}},    // King of Fighters '95, The (Europe)
    {"T-3101G",  {.cartridge = Cartridge::ROM_KOF95}},    // King of Fighters '95, The (Japan)
    {"T-13308G", {.cartridge = Cartridge::ROM_Ultraman}}, // Ultraman - Hikari no Kyojin Densetsu (Japan)

    {"T-1521G",    {.cartridge = Cartridge::DRAM8Mbit}}, // Astra Superstars (Japan)
    {"T-9904G",    {.cartridge = Cartridge::DRAM8Mbit}}, // Cotton 2 (Japan)
    {"T-1217G",    {.cartridge = Cartridge::DRAM8Mbit}}, // Cyberbots (Japan)
    {"GS-9107",    {.cartridge = Cartridge::DRAM8Mbit}}, // Fighter's History Dynamite (Japan)
    {"T-20109G",   {.cartridge = Cartridge::DRAM8Mbit}}, // Friends (Japan)
    {"T-14411G",   {.cartridge = Cartridge::DRAM8Mbit}}, // Groove on Fight (Japan)
    {"T-7032H-50", {.cartridge = Cartridge::DRAM8Mbit}}, // Marvel Super Heroes (Europe)
    {"T-1215G",    {.cartridge = Cartridge::DRAM8Mbit}}, // Marvel Super Heroes (Japan)
    {"T-3111G",    {.cartridge = Cartridge::DRAM8Mbit}}, // Metal Slug (Japan)
    {"T-22205G",   {.cartridge = Cartridge::DRAM8Mbit}}, // NOël 3 (Japan)
    {"T-20114G",   {.cartridge = Cartridge::DRAM8Mbit}}, // Pia Carrot e Youkoso!! 2 (Japan)
    {"T-3105G",    {.cartridge = Cartridge::DRAM8Mbit}}, // Real Bout Garou Densetsu (Japan)
    {"T-99901G",   {.cartridge = Cartridge::DRAM8Mbit}}, // Real Bout Garou Densetsu Demo (Japan)
    {"T-3119G",    {.cartridge = Cartridge::DRAM8Mbit}}, // Real Bout Garou Densetsu Special (Japan)
    {"T-3116G",    {.cartridge = Cartridge::DRAM8Mbit}}, // Samurai Spirits - Amakusa Kourin (Japan)
    {"T-3104G",    {.cartridge = Cartridge::DRAM8Mbit}}, // Samurai Spirits - Zankurou Musouken (Japan)
    {"T-16509G",   {.cartridge = Cartridge::DRAM8Mbit}}, // Super Real Mahjong P7 (Japan)
    {"T-16510G",   {.cartridge = Cartridge::DRAM8Mbit}}, // Super Real Mahjong P7 (Japan)
    {"610636008",  {.cartridge = Cartridge::DRAM8Mbit}}, // "Tech Saturn 1997.6 (Japan)
    {"T-3108G",    {.cartridge = Cartridge::DRAM8Mbit}}, // The King of Fighters '96 (Japan)
    {"T-3121G",    {.cartridge = Cartridge::DRAM8Mbit}}, // The King of Fighters '97 (Japan)
    {"T-1515G",    {.cartridge = Cartridge::DRAM8Mbit}}, // Waku Waku 7 (Japan)

    {"T-1245G", {.cartridge = Cartridge::DRAM32Mbit}}, // Dungeons and Dragons Collection (Japan)
    {"T-1248G", {.cartridge = Cartridge::DRAM32Mbit}}, // Final Fight Revenge (Japan)
    {"T-1238G", {.cartridge = Cartridge::DRAM32Mbit}}, // Marvel Super Heroes vs. Street Fighter (Japan)
    {"T-1230G", {.cartridge = Cartridge::DRAM32Mbit}}, // Pocket Fighter (Japan)
    {"T-1246G", {.cartridge = Cartridge::DRAM32Mbit}}, // Street Fighter Zero 3 (Japan)
    {"T-1229G", {.cartridge = Cartridge::DRAM32Mbit}}, // Vampire Savior (Japan)
    {"6106881", {.cartridge = Cartridge::DRAM32Mbit}}, // Vampire Savior (Japan) (Demo)
    {"T-1226G", {.cartridge = Cartridge::DRAM32Mbit}}, // X-Men vs. Street Fighter (Japan)

    {"T-16804G", {.cartridge = Cartridge::BackupRAM, .cartReason = "Required for saving games"}},   // Dezaemon 2 (Japan)
    {"GS-9197",  {.cartridge = Cartridge::BackupRAM, .cartReason = "Required for saving replays"}}, // Sega Ages - Galaxy Force II (Japan)

    {"MK-81019", {.sh2Cache = true}}, // Astal (USA)
    {"GS-9019",  {.sh2Cache = true}}, // Astal (Japan)
    {"MK-81304", {.sh2Cache = true}}, // Dark Savior (USA)
    {"T-5013H",  {.sh2Cache = true}}, // Soviet Strike (Europe, France, Germany, USA)
    {"T-10621G", {.sh2Cache = true}}, // Soviet Strike (Japan)
};

static const std::unordered_map<XXH128Hash, GameInfo> kGameInfosByHash = {
    {MakeXXH128Hash(0xCFA7E24F43C986F7, 0x051DAF831876C5FD), {.cartridge = Cartridge::DRAM48Mbit}}, // Heart of Darkness (Japan) (Prototype)
};
// clang-format on

const GameInfo *GetGameInfo(std::string_view productCode, XXH128Hash hash) {
    if (kGameInfosByCode.contains(productCode)) {
        return &kGameInfosByCode.at(productCode);
    } else if (kGameInfosByHash.contains(hash)) {
        return &kGameInfosByHash.at(hash);
    } else {
        return nullptr;
    }
}

} // namespace ymir::db
