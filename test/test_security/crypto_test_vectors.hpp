/**
 * @file crypto_test_vectors.hpp
 * @brief Published known-answer vectors for AES-128, CTR and CMAC
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "security/i_crypto_backend.hpp"

namespace loramesher {
namespace security {
namespace test_vectors {

/// Decode a string of hex digits
inline std::vector<uint8_t> FromHex(const char* hex) {
    const auto nibble = [](char digit) -> uint8_t {
        if (digit >= '0' && digit <= '9') {
            return static_cast<uint8_t>(digit - '0');
        }
        return static_cast<uint8_t>((digit | 0x20) - 'a' + 10);
    };
    std::vector<uint8_t> bytes;
    for (size_t i = 0; hex[i] != '\0' && hex[i + 1] != '\0'; i += 2) {
        bytes.push_back(
            static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return bytes;
}

/// Decode hex digits into a fixed-size array such as a key or a nonce
template <typename Array>
Array ArrayFromHex(const char* hex) {
    const std::vector<uint8_t> bytes = FromHex(hex);
    Array array{};
    if (bytes.size() == array.size()) {
        std::memcpy(array.data(), bytes.data(), array.size());
    }
    return array;
}

/// One CMAC known answer
struct CmacVector {
    size_t message_size;  ///< Leading bytes of kRfc4493Message
    const char* mac;
};

/// RFC 4493 section 4 key, message and expected MACs
constexpr char kRfc4493Key[] = "2b7e151628aed2a6abf7158809cf4f3c";
constexpr char kRfc4493Message[] =
    "6bc1bee22e409f96e93d7e117393172a"
    "ae2d8a571e03ac9c9eb76fac45af8e51"
    "30c81c46a35ce411e5fbc1191a0a52ef"
    "f69f2445df4f9b17ad2b417be66c3710";
constexpr CmacVector kRfc4493Vectors[] = {
    {0, "bb1d6929e95937287fa37d129b756746"},
    {16, "070a16b46b4d4144f79bdd9dd04a287c"},
    {40, "dfa66747de9ae63030ca32611497c827"},
    {64, "51f0bebf7e3b9d92fc49741779363cfe"},
};

/// NIST SP 800-38A F.5.1 CTR-AES128. The key and the plaintext are those of
/// RFC 4493. F.1.1 gives the ECB encryption of the first plaintext block.
constexpr char kSp80038aCounter[] = "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";
constexpr char kSp80038aCtrCiphertext[] =
    "874d6191b620e3261bef6864990db6ce"
    "9806f66b7970fdff8617187bb9fffdff"
    "5ae4df3edbd5d35e5b4f09020db03eab"
    "1e031dda2fbe03d1792170a0f3009cee";
constexpr char kSp80038aEcbFirstBlock[] = "3ad77bb40d7a3660a89ecaf32466ef97";

/// FIPS 197 appendix C.1 AES-128 block example
constexpr char kFips197Key[] = "000102030405060708090a0b0c0d0e0f";
constexpr char kFips197Plaintext[] = "00112233445566778899aabbccddeeff";
constexpr char kFips197Ciphertext[] = "69c4e0d86a7b0430d8cdb78070b4c55a";

}  // namespace test_vectors
}  // namespace security
}  // namespace loramesher
