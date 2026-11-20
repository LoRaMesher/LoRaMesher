/**
 * @file crypto_backend_test.cpp
 * @brief Known-answer and negative tests of every available crypto backend
 */

#include <gtest/gtest.h>

#include <vector>

#include "crypto_test_vectors.hpp"
#include "security/crypto_backend.hpp"

namespace loramesher {
namespace security {
namespace {

using test_vectors::ArrayFromHex;
using test_vectors::FromHex;

#if LORAMESHER_SECURITY_USE_MBEDTLS
using BackendTypes =
    ::testing::Types<PortableCryptoBackend, MbedTlsCryptoBackend>;
#else
using BackendTypes = ::testing::Types<PortableCryptoBackend>;
#endif

template <typename Backend>
class CryptoBackendTest : public ::testing::Test {
   protected:
    Backend backend_;
};

TYPED_TEST_SUITE(CryptoBackendTest, BackendTypes);

TYPED_TEST(CryptoBackendTest, CmacMatchesRfc4493Vectors) {
    const auto key = ArrayFromHex<Aes128Key>(test_vectors::kRfc4493Key);
    const auto message = FromHex(test_vectors::kRfc4493Message);
    for (const auto& vector : test_vectors::kRfc4493Vectors) {
        SCOPED_TRACE(vector.message_size);
        AesBlock mac{};
        ASSERT_EQ(CryptoStatus::kOk,
                  this->backend_.Cmac(key,
                                      std::span<const uint8_t>(
                                          message.data(), vector.message_size),
                                      mac));
        EXPECT_EQ(ArrayFromHex<AesBlock>(vector.mac), mac);
    }
}

TYPED_TEST(CryptoBackendTest, CmacDependsOnKeyAndMessage) {
    const auto key = ArrayFromHex<Aes128Key>(test_vectors::kRfc4493Key);
    std::vector<uint8_t> message = FromHex(test_vectors::kRfc4493Message);
    AesBlock reference{};
    ASSERT_EQ(CryptoStatus::kOk, this->backend_.Cmac(key, message, reference));

    AesBlock mac{};
    message[17] ^= 0x01;
    ASSERT_EQ(CryptoStatus::kOk, this->backend_.Cmac(key, message, mac));
    EXPECT_NE(reference, mac);
    message[17] ^= 0x01;

    Aes128Key other_key = key;
    other_key[0] ^= 0x80;
    ASSERT_EQ(CryptoStatus::kOk, this->backend_.Cmac(other_key, message, mac));
    EXPECT_NE(reference, mac);
}

TYPED_TEST(CryptoBackendTest, CmacTruncatesToEveryTagLength) {
    const auto key = ArrayFromHex<Aes128Key>(test_vectors::kRfc4493Key);
    const auto message = FromHex(test_vectors::kRfc4493Message);
    AesBlock full{};
    ASSERT_EQ(CryptoStatus::kOk, this->backend_.Cmac(key, message, full));

    for (size_t tag_size = kCmacMinTagSize; tag_size <= kCmacMaxTagSize;
         ++tag_size) {
        SCOPED_TRACE(tag_size);
        std::vector<uint8_t> tag(tag_size);
        ASSERT_EQ(CryptoStatus::kOk, this->backend_.Cmac(key, message, tag));
        EXPECT_EQ(std::vector<uint8_t>(full.data(), full.data() + tag_size),
                  tag);
        EXPECT_EQ(CryptoStatus::kOk,
                  this->backend_.CmacVerify(key, message, tag));
    }
}

TYPED_TEST(CryptoBackendTest, CmacVerifyRejectsEveryFlippedTagBit) {
    const auto key = ArrayFromHex<Aes128Key>(test_vectors::kRfc4493Key);
    const auto message = FromHex(test_vectors::kRfc4493Message);
    for (size_t tag_size = kCmacMinTagSize; tag_size <= kCmacMaxTagSize;
         ++tag_size) {
        SCOPED_TRACE(tag_size);
        std::vector<uint8_t> tag(tag_size);
        ASSERT_EQ(CryptoStatus::kOk, this->backend_.Cmac(key, message, tag));
        for (size_t bit = 0; bit < 8 * tag_size; ++bit) {
            tag[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));
            EXPECT_EQ(CryptoStatus::kAuthFailed,
                      this->backend_.CmacVerify(key, message, tag))
                << bit;
            tag[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));
        }
    }
}

TYPED_TEST(CryptoBackendTest, CmacVerifyRejectsChangedMessageAndKey) {
    const auto key = ArrayFromHex<Aes128Key>(test_vectors::kRfc4493Key);
    std::vector<uint8_t> message = FromHex(test_vectors::kRfc4493Message);
    for (const size_t tag_size :
         {kCmacMinTagSize, size_t{4}, kCmacMaxTagSize}) {
        SCOPED_TRACE(tag_size);
        std::vector<uint8_t> tag(tag_size);
        ASSERT_EQ(CryptoStatus::kOk, this->backend_.Cmac(key, message, tag));

        for (size_t i = 0; i < message.size(); ++i) {
            message[i] ^= 0x04;
            EXPECT_EQ(CryptoStatus::kAuthFailed,
                      this->backend_.CmacVerify(key, message, tag))
                << i;
            message[i] ^= 0x04;
        }

        std::vector<uint8_t> shorter = message;
        shorter.pop_back();
        EXPECT_EQ(CryptoStatus::kAuthFailed,
                  this->backend_.CmacVerify(key, shorter, tag));

        Aes128Key other_key = key;
        other_key[15] ^= 0x01;
        EXPECT_EQ(CryptoStatus::kAuthFailed,
                  this->backend_.CmacVerify(other_key, message, tag));
    }
}

TYPED_TEST(CryptoBackendTest, CmacRejectsUnsupportedTagLengths) {
    const Aes128Key key{};
    const std::vector<uint8_t> message = FromHex("0011223344");
    for (const size_t tag_size : {0u, 1u, 2u, 17u, 32u}) {
        SCOPED_TRACE(tag_size);
        std::vector<uint8_t> tag(tag_size, 0xEE);
        EXPECT_EQ(CryptoStatus::kInvalidArgument,
                  this->backend_.Cmac(key, message, tag));
        EXPECT_EQ(std::vector<uint8_t>(tag_size, 0xEE), tag);
        EXPECT_EQ(CryptoStatus::kInvalidArgument,
                  this->backend_.CmacVerify(key, message, tag));
    }
}

TYPED_TEST(CryptoBackendTest, EncryptBlockMatchesFips197AndSp80038a) {
    AesBlock output{};
    ASSERT_EQ(
        CryptoStatus::kOk,
        this->backend_.EncryptBlock(
            ArrayFromHex<Aes128Key>(test_vectors::kFips197Key),
            ArrayFromHex<AesBlock>(test_vectors::kFips197Plaintext), output));
    EXPECT_EQ(ArrayFromHex<AesBlock>(test_vectors::kFips197Ciphertext), output);

    // Second vector, encrypted in place
    AesBlock block = ArrayFromHex<AesBlock>("6bc1bee22e409f96e93d7e117393172a");
    ASSERT_EQ(
        CryptoStatus::kOk,
        this->backend_.EncryptBlock(
            ArrayFromHex<Aes128Key>(test_vectors::kRfc4493Key), block, block));
    EXPECT_EQ(ArrayFromHex<AesBlock>(test_vectors::kSp80038aEcbFirstBlock),
              block);
}

TYPED_TEST(CryptoBackendTest, CtrMatchesSp80038aVector) {
    const auto key = ArrayFromHex<Aes128Key>(test_vectors::kRfc4493Key);
    const auto counter = ArrayFromHex<AesBlock>(test_vectors::kSp80038aCounter);
    const auto plaintext = FromHex(test_vectors::kRfc4493Message);
    const auto ciphertext = FromHex(test_vectors::kSp80038aCtrCiphertext);

    // F.5.1 encrypt
    std::vector<uint8_t> data = plaintext;
    ASSERT_EQ(CryptoStatus::kOk, this->backend_.CtrCrypt(key, counter, data));
    EXPECT_EQ(ciphertext, data);
    // F.5.2 decrypt
    ASSERT_EQ(CryptoStatus::kOk, this->backend_.CtrCrypt(key, counter, data));
    EXPECT_EQ(plaintext, data);

    // Every shorter length yields a prefix of the same key stream
    for (size_t size = 0; size < plaintext.size(); ++size) {
        std::vector<uint8_t> partial(plaintext.data(), plaintext.data() + size);
        ASSERT_EQ(CryptoStatus::kOk,
                  this->backend_.CtrCrypt(key, counter, partial));
        EXPECT_EQ(
            std::vector<uint8_t>(ciphertext.data(), ciphertext.data() + size),
            partial)
            << size;
    }
}

TYPED_TEST(CryptoBackendTest, CtrCounterCarriesAcrossTheWholeBlock) {
    const auto key = ArrayFromHex<Aes128Key>(test_vectors::kFips197Key);
    // Key stream blocks expected for three counters around each carry
    const char* const counters[][3] = {
        {"000000000000000000000000ffffffff", "00000000000000000000000100000000",
         "00000000000000000000000100000001"},
        {"00000000000000ffffffffffffffffff", "00000000000001000000000000000000",
         "00000000000001000000000000000001"},
        {"ffffffffffffffffffffffffffffffff", "00000000000000000000000000000000",
         "00000000000000000000000000000001"},
    };
    for (const auto& sequence : counters) {
        SCOPED_TRACE(sequence[0]);
        std::vector<uint8_t> expected;
        for (const char* counter : sequence) {
            AesBlock key_stream{};
            ASSERT_EQ(CryptoStatus::kOk,
                      this->backend_.EncryptBlock(
                          key, ArrayFromHex<AesBlock>(counter), key_stream));
            expected.insert(expected.end(), key_stream.begin(),
                            key_stream.end());
        }
        std::vector<uint8_t> data(3 * kAesBlockSize, 0);
        ASSERT_EQ(CryptoStatus::kOk,
                  this->backend_.CtrCrypt(
                      key, ArrayFromHex<AesBlock>(sequence[0]), data));
        EXPECT_EQ(expected, data);
    }
}

}  // namespace
}  // namespace security
}  // namespace loramesher
