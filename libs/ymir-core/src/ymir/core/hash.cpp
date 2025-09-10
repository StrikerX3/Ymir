#include <ymir/core/hash.hpp>

#include <fmt/xchar.h>
#include <xxh3.h>

namespace ymir {

XXH128Hash CalcHash128(const void *input, size_t len, uint64_t seed) {
    const XXH128_hash_t hash = XXH128(input, len, seed);
    XXH128_canonical_t canonicalHash{};
    XXH128_canonicalFromHash(&canonicalHash, hash);

    XXH128Hash out{};
    std::copy_n(canonicalHash.digest, out.size(), out.begin());
    return out;
}

std::string ToString(const XXH128Hash &hash) {
    fmt::memory_buffer buf{};
    auto inserter = std::back_inserter(buf);
    for (uint8_t b : hash) {
        fmt::format_to(inserter, "{:02X}", b);
    }
    return fmt::to_string(buf);
}

XXH128Hash MakeXXH128Hash(uint64_t hi, uint64_t lo) {
    XXH128Hash out{};
    for (uint64_t byte = 0ull; byte < 8ull; ++byte) {
        out[byte + 0] = hi >> ((7ull - byte) * 8ull);
        out[byte + 8] = lo >> ((7ull - byte) * 8ull);
    }
    return out;
}

} // namespace ymir
