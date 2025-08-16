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

} // namespace ymir
