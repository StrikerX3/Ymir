#include <ymir/db/game_db.hpp>

#include <unordered_map>

namespace ymir::db {

using enum GameInfo::Flags;

// Table adapted from Mednafen
// https://github.com/libretro-mirrors/mednafen-git/blob/master/src/ss/db.cpp

// clang-format off
static const std::unordered_map<std::string_view, GameInfo> kGameInfosByCode = {
    {"MK-81088", {.flags = Cart_ROM_KOF95}},    // King of Fighters '95, The (Europe)
    {"T-3101G",  {.flags = Cart_ROM_KOF95}},    // King of Fighters '95, The (Japan)
    {"T-13308G", {.flags = Cart_ROM_Ultraman}}, // Ultraman - Hikari no Kyojin Densetsu (Japan)

    {"T-1521G",    {.flags = Cart_DRAM8Mbit}}, // Astra Superstars (Japan)
    {"T-9904G",    {.flags = Cart_DRAM8Mbit}}, // Cotton 2 (Japan)
    {"T-1217G",    {.flags = Cart_DRAM8Mbit}}, // Cyberbots (Japan)
    {"GS-9107",    {.flags = Cart_DRAM8Mbit}}, // Fighter's History Dynamite (Japan)
    {"T-20109G",   {.flags = Cart_DRAM8Mbit}}, // Friends (Japan)
    {"T-14411G",   {.flags = Cart_DRAM8Mbit}}, // Groove on Fight (Japan)
    {"T-7032H-50", {.flags = Cart_DRAM8Mbit}}, // Marvel Super Heroes (Europe)
    {"T-1215G",    {.flags = Cart_DRAM8Mbit}}, // Marvel Super Heroes (Japan)
    {"T-3111G",    {.flags = Cart_DRAM8Mbit}}, // Metal Slug (Japan)
    {"T-22205G",   {.flags = Cart_DRAM8Mbit}}, // NOel 3 (Japan)
    {"T-20114G",   {.flags = Cart_DRAM8Mbit}}, // Pia Carrot e Youkoso!! 2 (Japan)
    {"T-3105G",    {.flags = Cart_DRAM8Mbit}}, // Real Bout Garou Densetsu (Japan)
    {"T-99901G",   {.flags = Cart_DRAM8Mbit}}, // Real Bout Garou Densetsu Demo (Japan)
    {"T-3119G",    {.flags = Cart_DRAM8Mbit}}, // Real Bout Garou Densetsu Special (Japan)
    {"T-3116G",    {.flags = Cart_DRAM8Mbit}}, // Samurai Spirits - Amakusa Kourin (Japan)
    {"T-3104G",    {.flags = Cart_DRAM8Mbit}}, // Samurai Spirits - Zankurou Musouken (Japan)
    {"T-16509G",   {.flags = Cart_DRAM8Mbit}}, // Super Real Mahjong P7 (Japan)
    {"T-16510G",   {.flags = Cart_DRAM8Mbit}}, // Super Real Mahjong P7 (Japan)
    {"610636008",  {.flags = Cart_DRAM8Mbit}}, // "Tech Saturn 1997.6 (Japan)
    {"T-3108G",    {.flags = Cart_DRAM8Mbit}}, // The King of Fighters '96 (Japan)
    {"T-3121G",    {.flags = Cart_DRAM8Mbit}}, // The King of Fighters '97 (Japan)
    {"T-1515G",    {.flags = Cart_DRAM8Mbit}}, // Waku Waku 7 (Japan)

    {"T-1245G", {.flags = Cart_DRAM32Mbit}}, // Dungeons and Dragons Collection (Japan)
    {"T-1248G", {.flags = Cart_DRAM32Mbit}}, // Final Fight Revenge (Japan)
    {"T-1238G", {.flags = Cart_DRAM32Mbit | FastBusTimings}}, // Marvel Super Heroes vs. Street Fighter (Japan)
    {"T-1230G", {.flags = Cart_DRAM32Mbit}}, // Pocket Fighter (Japan)
    {"T-1246G", {.flags = Cart_DRAM32Mbit}}, // Street Fighter Zero 3 (Japan)
    {"T-1229G", {.flags = Cart_DRAM32Mbit | FastBusTimings | FastMC68EC000}}, // Vampire Savior (Japan)
    {"6106881", {.flags = Cart_DRAM32Mbit | FastBusTimings | FastMC68EC000}}, // Vampire Savior (Japan) (Demo)
    {"T-1226G", {.flags = Cart_DRAM32Mbit | FastBusTimings}}, // X-Men vs. Street Fighter (Japan)

    {"T-16804G", {.flags = Cart_BackupRAM, .cartReason = "Required for saving games"}},   // Dezaemon 2 (Japan)
    {"GS-9197",  {.flags = Cart_BackupRAM, .cartReason = "Required for saving replays"}}, // Sega Ages - Galaxy Force II (Japan)

    {"GS-9019",  {.flags = ForceSH2Cache}}, // Astal (Japan)
    {"MK-81019", {.flags = ForceSH2Cache}}, // Astal (USA)
    {"MK-81304", {.flags = ForceSH2Cache}}, // Dark Savior (USA)
    {"T-5013H",  {.flags = ForceSH2Cache}}, // Soviet Strike (Europe, France, Germany, USA)
    {"T-10621G", {.flags = ForceSH2Cache}}, // Soviet Strike (Japan)
    
    {"MK-81804", {.flags = FastBusTimings}}, // Deep Fear (Europe, USA)
    {"GS-9189",  {.flags = FastBusTimings}}, // Deep Fear (Japan)

    {"T-7029H-50", {.flags = StallVDP1OnVRAMWrites}}, // Mega Man X3 (Europe)
    {"T-1210G",    {.flags = StallVDP1OnVRAMWrites}}, // Rockman X3 (Japan)
};

static const std::unordered_map<XXH128Hash, GameInfo> kGameInfosByHash = {
    {MakeXXH128Hash(0xCFA7E24F43C986F7, 0x051DAF831876C5FD), {.flags = Cart_DRAM48Mbit}}, // Heart of Darkness (Japan) (Prototype)
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
