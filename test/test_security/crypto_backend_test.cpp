/**
 * @file crypto_backend_test.cpp
 * @brief Known-answer and negative tests of every available crypto backend
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "crypto_test_vectors.hpp"
#include "security/crypto_backend.hpp"

namespace loramesher {
namespace security {
namespace {

using test_vectors::ArrayFromHex;
using test_vectors::CcmVector;
using test_vectors::FromHex;

#if LORAMESHER_SECURITY_USE_MBEDTLS
using BackendTypes =
    ::testing::Types<PortableCryptoBackend, MbedTlsCryptoBackend>;
#else
using BackendTypes = ::testing::Types<PortableCryptoBackend>;
#endif

bool IsAllZero(const std::vector<uint8_t>& bytes) {
    return std::all_of(bytes.begin(), bytes.end(),
                       [](uint8_t byte) { return byte == 0; });
}

/// A sealed frame and everything needed to open it
struct SealedFrame {
    Aes128Key key;
    CcmNonce nonce;
    std::vector<uint8_t> associated_data;
    std::vector<uint8_t> plaintext;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;
};

template <typename Backend>
class CryptoBackendTest : public ::testing::Test {
   protected:
    /// Seal a 40-byte payload with 6 bytes of associated data
    SealedFrame MakeSealedFrame(size_t tag_size) {
        SealedFrame frame;
        frame.key = ArrayFromHex<Aes128Key>("000102030405060708090a0b0c0d0e0f");
        frame.nonce = ArrayFromHex<CcmNonce>("a0a1a2a3a4a5a6a7a8a9aaabac");
        frame.associated_data = FromHex("10200001042a");
        for (size_t i = 0; i < 40; ++i) {
            frame.plaintext.push_back(static_cast<uint8_t>(0x30 + i));
        }
        frame.ciphertext = frame.plaintext;
        frame.tag.resize(tag_size);
        EXPECT_EQ(
            CryptoStatus::kOk,
            backend_.CcmSeal(frame.key, frame.nonce, frame.associated_data,
                             frame.ciphertext, frame.tag));
        return frame;
    }

    /// Open a frame and check that a rejected one releases nothing
    CryptoStatus Open(const SealedFrame& frame) {
        std::vector<uint8_t> data = frame.ciphertext;
        const CryptoStatus status = backend_.CcmOpen(
            frame.key, frame.nonce, frame.associated_data, data, frame.tag);
        if (status == CryptoStatus::kOk) {
            EXPECT_EQ(frame.plaintext, data);
        } else {
            EXPECT_TRUE(IsAllZero(data));
        }
        return status;
    }

    void ExpectKnownAnswer(const CcmVector& vector) {
        SCOPED_TRACE(vector.name);
        const auto key = ArrayFromHex<Aes128Key>(vector.key);
        const auto nonce = ArrayFromHex<CcmNonce>(vector.nonce);
        const auto associated_data = FromHex(vector.associated_data);
        const auto plaintext = FromHex(vector.plaintext);
        const auto ciphertext = FromHex(vector.ciphertext);
        const auto expected_tag = FromHex(vector.tag);

        std::vector<uint8_t> data = plaintext;
        std::vector<uint8_t> tag(expected_tag.size());
        ASSERT_EQ(CryptoStatus::kOk,
                  backend_.CcmSeal(key, nonce, associated_data, data, tag));
        EXPECT_EQ(ciphertext, data);
        EXPECT_EQ(expected_tag, tag);

        ASSERT_EQ(
            CryptoStatus::kOk,
            backend_.CcmOpen(key, nonce, associated_data, data, expected_tag));
        EXPECT_EQ(plaintext, data);
    }

    Backend backend_;
};

TYPED_TEST_SUITE(CryptoBackendTest, BackendTypes);

TYPED_TEST(CryptoBackendTest, CcmMatchesRfc3610Vectors) {
    for (const CcmVector& vector : test_vectors::kRfc3610Vectors) {
        this->ExpectKnownAnswer(vector);
    }
}

TYPED_TEST(CryptoBackendTest, CcmMatchesNistCavpVectors) {
    for (const CcmVector& vector : test_vectors::kCavpVectors) {
        this->ExpectKnownAnswer(vector);
    }
}

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

TYPED_TEST(CryptoBackendTest, RoundTripsEveryFrameLength) {
    const auto key =
        ArrayFromHex<Aes128Key>("5a0f1e2d3c4b5a69788796a5b4c3d2e1");
    CcmNonce nonce{};
    for (const size_t tag_size : {kCcmShortTagSize, kCcmLongTagSize}) {
        for (size_t data_size = 0; data_size <= 130; ++data_size) {
            SCOPED_TRACE(data_size);
            nonce[12] = static_cast<uint8_t>(data_size);
            nonce[0] = static_cast<uint8_t>(tag_size);
            // Associated data length sweeps across the block boundaries
            const std::vector<uint8_t> associated_data(data_size % 35, 0xA5);
            std::vector<uint8_t> plaintext(data_size);
            for (size_t i = 0; i < data_size; ++i) {
                plaintext[i] = static_cast<uint8_t>(i * 7 + data_size);
            }

            std::vector<uint8_t> data = plaintext;
            std::vector<uint8_t> tag(tag_size);
            ASSERT_EQ(
                CryptoStatus::kOk,
                this->backend_.CcmSeal(key, nonce, associated_data, data, tag));
            if (data_size >= 8) {
                EXPECT_NE(plaintext, data);
            }
            ASSERT_EQ(
                CryptoStatus::kOk,
                this->backend_.CcmOpen(key, nonce, associated_data, data, tag));
            EXPECT_EQ(plaintext, data);
        }
    }
}

TYPED_TEST(CryptoBackendTest, OpenRejectsFlippedCiphertextBit) {
    for (const size_t tag_size : {kCcmShortTagSize, kCcmLongTagSize}) {
        const SealedFrame sealed = this->MakeSealedFrame(tag_size);
        ASSERT_EQ(CryptoStatus::kOk, this->Open(sealed));
        for (size_t i = 0; i < sealed.ciphertext.size(); ++i) {
            SealedFrame frame = sealed;
            frame.ciphertext[i] ^= static_cast<uint8_t>(1u << (i % 8));
            EXPECT_EQ(CryptoStatus::kAuthFailed, this->Open(frame)) << i;
        }
    }
}

TYPED_TEST(CryptoBackendTest, OpenRejectsFlippedTagBit) {
    for (const size_t tag_size : {kCcmShortTagSize, kCcmLongTagSize}) {
        const SealedFrame sealed = this->MakeSealedFrame(tag_size);
        for (size_t bit = 0; bit < 8 * tag_size; ++bit) {
            SealedFrame frame = sealed;
            frame.tag[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));
            EXPECT_EQ(CryptoStatus::kAuthFailed, this->Open(frame)) << bit;
        }
    }
}

TYPED_TEST(CryptoBackendTest, OpenRejectsChangedAssociatedData) {
    for (const size_t tag_size : {kCcmShortTagSize, kCcmLongTagSize}) {
        const SealedFrame sealed = this->MakeSealedFrame(tag_size);
        for (size_t i = 0; i < sealed.associated_data.size(); ++i) {
            SealedFrame frame = sealed;
            frame.associated_data[i] ^= 0x01;
            EXPECT_EQ(CryptoStatus::kAuthFailed, this->Open(frame)) << i;
        }

        SealedFrame shorter = sealed;
        shorter.associated_data.pop_back();
        EXPECT_EQ(CryptoStatus::kAuthFailed, this->Open(shorter));

        SealedFrame longer = sealed;
        longer.associated_data.push_back(0x00);
        EXPECT_EQ(CryptoStatus::kAuthFailed, this->Open(longer));

        SealedFrame none = sealed;
        none.associated_data.clear();
        EXPECT_EQ(CryptoStatus::kAuthFailed, this->Open(none));
    }
}

TYPED_TEST(CryptoBackendTest, OpenRejectsWrongNonce) {
    for (const size_t tag_size : {kCcmShortTagSize, kCcmLongTagSize}) {
        const SealedFrame sealed = this->MakeSealedFrame(tag_size);
        for (size_t i = 0; i < kCcmNonceSize; ++i) {
            SealedFrame frame = sealed;
            frame.nonce[i] ^= 0x01;
            EXPECT_EQ(CryptoStatus::kAuthFailed, this->Open(frame)) << i;
        }
    }
}

TYPED_TEST(CryptoBackendTest, OpenRejectsWrongKey) {
    for (const size_t tag_size : {kCcmShortTagSize, kCcmLongTagSize}) {
        const SealedFrame sealed = this->MakeSealedFrame(tag_size);
        for (size_t i = 0; i < kAesBlockSize; ++i) {
            SealedFrame frame = sealed;
            frame.key[i] ^= 0x10;
            EXPECT_EQ(CryptoStatus::kAuthFailed, this->Open(frame)) << i;
        }
    }
}

TYPED_TEST(CryptoBackendTest, OpenRejectsTruncatedCiphertext) {
    SealedFrame frame = this->MakeSealedFrame(kCcmLongTagSize);
    frame.ciphertext.pop_back();
    frame.plaintext.pop_back();
    EXPECT_EQ(CryptoStatus::kAuthFailed, this->Open(frame));
}

TYPED_TEST(CryptoBackendTest, TagLengthIsPartOfTheAuthentication) {
    // The first 4 bytes of an 8-byte tag are not a valid 4-byte tag
    SealedFrame frame = this->MakeSealedFrame(kCcmLongTagSize);
    frame.tag.resize(kCcmShortTagSize);
    EXPECT_EQ(CryptoStatus::kAuthFailed, this->Open(frame));
}

TYPED_TEST(CryptoBackendTest, AuthenticatesFrameWithoutPayload) {
    const auto key =
        ArrayFromHex<Aes128Key>("000102030405060708090a0b0c0d0e0f");
    const CcmNonce nonce{};
    std::vector<uint8_t> associated_data = FromHex("0102030405");
    std::vector<uint8_t> tag(kCcmShortTagSize);
    ASSERT_EQ(CryptoStatus::kOk,
              this->backend_.CcmSeal(key, nonce, associated_data, {}, tag));
    EXPECT_EQ(CryptoStatus::kOk,
              this->backend_.CcmOpen(key, nonce, associated_data, {}, tag));
    associated_data[4] ^= 0x01;
    EXPECT_EQ(CryptoStatus::kAuthFailed,
              this->backend_.CcmOpen(key, nonce, associated_data, {}, tag));
}

TYPED_TEST(CryptoBackendTest, RejectsUnsupportedTagLengths) {
    const Aes128Key key{};
    const CcmNonce nonce{};
    const std::vector<uint8_t> original = FromHex("00112233445566778899");
    for (const size_t tag_size : {0u, 2u, 3u, 5u, 6u, 7u, 10u, 12u, 16u}) {
        SCOPED_TRACE(tag_size);
        std::vector<uint8_t> data = original;
        std::vector<uint8_t> tag(tag_size, 0xEE);
        EXPECT_EQ(CryptoStatus::kInvalidArgument,
                  this->backend_.CcmSeal(key, nonce, {}, data, tag));
        EXPECT_EQ(original, data);
        EXPECT_EQ(std::vector<uint8_t>(tag_size, 0xEE), tag);
        EXPECT_EQ(CryptoStatus::kInvalidArgument,
                  this->backend_.CcmOpen(key, nonce, {}, data, tag));
        EXPECT_EQ(original, data);
    }
}

TYPED_TEST(CryptoBackendTest, RejectsOversizedInputs) {
    const Aes128Key key{};
    const CcmNonce nonce{};
    std::vector<uint8_t> tag(kCcmLongTagSize);

    std::vector<uint8_t> too_much_data(kCcmMaxDataSize + 1);
    EXPECT_EQ(CryptoStatus::kInvalidArgument,
              this->backend_.CcmSeal(key, nonce, {}, too_much_data, tag));

    const std::vector<uint8_t> too_much_associated(kCcmMaxAssociatedDataSize +
                                                   1);
    std::vector<uint8_t> data(4);
    EXPECT_EQ(
        CryptoStatus::kInvalidArgument,
        this->backend_.CcmSeal(key, nonce, too_much_associated, data, tag));
}

}  // namespace
}  // namespace security
}  // namespace loramesher
