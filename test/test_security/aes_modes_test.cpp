/**
 * @file aes_modes_test.cpp
 * @brief Tests of the portable AES-128 block and the generic CCM code
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

// The SP 800-38C examples use nonce and tag lengths that ICryptoBackend does
// not offer, so they run on the generic CCM code directly.
TEST(AesModesTest, CcmMatchesSp80038cExamples) {
    for (const auto& vector : test_vectors::kSp80038cVectors) {
        SCOPED_TRACE(vector.name);
        SoftwareAes128 cipher(ArrayFromHex<Aes128Key>(vector.key));
        const auto nonce = FromHex(vector.nonce);
        const auto associated_data = FromHex(vector.associated_data);
        const auto plaintext = FromHex(vector.plaintext);
        const auto expected_tag = FromHex(vector.tag);

        std::vector<uint8_t> data = plaintext;
        std::vector<uint8_t> tag(expected_tag.size());
        ASSERT_EQ(CryptoStatus::kOk,
                  CcmSeal(cipher, nonce, associated_data, data, tag));
        EXPECT_EQ(FromHex(vector.ciphertext), data);
        EXPECT_EQ(expected_tag, tag);

        ASSERT_EQ(CryptoStatus::kOk,
                  CcmOpen(cipher, nonce, associated_data, data, tag));
        EXPECT_EQ(plaintext, data);

        tag[0] ^= 0x01;
        EXPECT_EQ(CryptoStatus::kAuthFailed,
                  CcmOpen(cipher, nonce, associated_data, data, tag));
        EXPECT_EQ(std::vector<uint8_t>(plaintext.size(), 0), data);
    }
}

TEST(AesModesTest, CcmRejectsInvalidNonceAndTagLengths) {
    SoftwareAes128 cipher(Aes128Key{});
    std::vector<uint8_t> data(8, 0x11);
    const std::vector<uint8_t> original = data;
    std::vector<uint8_t> tag(8);

    for (const size_t nonce_size : {0u, 6u, 14u, 16u}) {
        const std::vector<uint8_t> nonce(nonce_size);
        EXPECT_EQ(CryptoStatus::kInvalidArgument,
                  CcmSeal(cipher, nonce, {}, data, tag));
        EXPECT_EQ(CryptoStatus::kInvalidArgument,
                  CcmOpen(cipher, nonce, {}, data, tag));
    }
    const std::vector<uint8_t> nonce(kCcmNonceSize);
    for (const size_t tag_size : {0u, 2u, 5u, 18u}) {
        std::vector<uint8_t> bad_tag(tag_size);
        EXPECT_EQ(CryptoStatus::kInvalidArgument,
                  CcmSeal(cipher, nonce, {}, data, bad_tag));
        EXPECT_EQ(CryptoStatus::kInvalidArgument,
                  CcmOpen(cipher, nonce, {}, data, bad_tag));
    }
    EXPECT_EQ(original, data);
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
