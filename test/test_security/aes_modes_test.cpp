/**
 * @file aes_modes_test.cpp
 * @brief Tests of the portable AES-128 block and the shared helpers
 */

#include <gtest/gtest.h>

#include <vector>

#include "crypto_test_vectors.hpp"
#include "security/aes_modes.hpp"
#include "security/software_aes128.hpp"

namespace loramesher {
namespace security {
namespace {

using test_vectors::ArrayFromHex;
using test_vectors::FromHex;

TEST(SoftwareAes128Test, MatchesFips197Example) {
    SoftwareAes128 cipher(ArrayFromHex<Aes128Key>(test_vectors::kFips197Key));
    const auto plaintext =
        ArrayFromHex<AesBlock>(test_vectors::kFips197Plaintext);
    AesBlock ciphertext{};
    cipher.EncryptBlock(plaintext.data(), ciphertext.data());
    EXPECT_EQ(ArrayFromHex<AesBlock>(test_vectors::kFips197Ciphertext),
              ciphertext);
}

TEST(SoftwareAes128Test, EncryptsInPlace) {
    SoftwareAes128 cipher(ArrayFromHex<Aes128Key>(test_vectors::kFips197Key));
    AesBlock block = ArrayFromHex<AesBlock>(test_vectors::kFips197Plaintext);
    cipher.EncryptBlock(block.data(), block.data());
    EXPECT_EQ(ArrayFromHex<AesBlock>(test_vectors::kFips197Ciphertext), block);
}

TEST(AesModesTest, ConstantTimeEqualComparesContentAndSize) {
    const std::vector<uint8_t> a = FromHex("0011223344556677");
    std::vector<uint8_t> b = a;
    EXPECT_TRUE(ConstantTimeEqual(a, b));
    EXPECT_TRUE(ConstantTimeEqual({}, {}));

    for (size_t i = 0; i < b.size(); ++i) {
        b[i] ^= 0x80;
        EXPECT_FALSE(ConstantTimeEqual(a, b)) << i;
        b[i] ^= 0x80;
    }
    b.pop_back();
    EXPECT_FALSE(ConstantTimeEqual(a, b));
}

TEST(AesModesTest, SecureZeroClearsTheBuffer) {
    std::vector<uint8_t> buffer(33, 0xFF);
    SecureZero(buffer);
    EXPECT_EQ(std::vector<uint8_t>(33, 0), buffer);
}

}  // namespace
}  // namespace security
}  // namespace loramesher
