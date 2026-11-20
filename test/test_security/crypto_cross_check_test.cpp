/**
 * @file crypto_cross_check_test.cpp
 * @brief Checks that the backends agree with each other and with mbedTLS
 *
 * These tests need mbedTLS. They run on ESP32 and on any native build that
 * links mbedTLS and sets -DLORAMESHER_SECURITY_USE_MBEDTLS=1. Elsewhere they
 * are skipped.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "security/crypto_backend.hpp"

#if LORAMESHER_SECURITY_USE_MBEDTLS
#include <mbedtls/ccm.h>
#include <mbedtls/cipher.h>
#include <mbedtls/cmac.h>
#endif

namespace loramesher {
namespace security {
namespace {

#if LORAMESHER_SECURITY_USE_MBEDTLS

/// Deterministic byte source, so a failure can be reproduced
class ByteSource {
   public:
    uint8_t Next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<uint8_t>(state_ >> 11);
    }

    void Fill(std::span<uint8_t> bytes) {
        for (uint8_t& byte : bytes) {
            byte = Next();
        }
    }

   private:
    uint32_t state_ = 0x4C4D5348u;
};

/// One random frame: sizes sweep the range a LoRa frame can hold
struct RandomFrame {
    Aes128Key key;
    CcmNonce nonce;
    std::vector<uint8_t> associated_data;
    std::vector<uint8_t> plaintext;
    size_t tag_size;

    RandomFrame(ByteSource& source, size_t index)
        : associated_data(index % 23),
          plaintext(index % 131),
          tag_size(index % 2 == 0 ? kCcmShortTagSize : kCcmLongTagSize) {
        source.Fill(key);
        source.Fill(nonce);
        source.Fill(associated_data);
        source.Fill(plaintext);
    }
};

constexpr size_t kFrameCount = 600;

TEST(CryptoCrossCheckTest, PortableAndMbedTlsBackendsAgreeOnCcm) {
    const PortableCryptoBackend portable;
    const MbedTlsCryptoBackend mbedtls;
    ByteSource source;
    for (size_t index = 0; index < kFrameCount; ++index) {
        SCOPED_TRACE(index);
        const RandomFrame frame(source, index);

        std::vector<uint8_t> portable_data = frame.plaintext;
        std::vector<uint8_t> portable_tag(frame.tag_size);
        ASSERT_EQ(
            CryptoStatus::kOk,
            portable.CcmSeal(frame.key, frame.nonce, frame.associated_data,
                             portable_data, portable_tag));
        std::vector<uint8_t> mbedtls_data = frame.plaintext;
        std::vector<uint8_t> mbedtls_tag(frame.tag_size);
        ASSERT_EQ(CryptoStatus::kOk,
                  mbedtls.CcmSeal(frame.key, frame.nonce, frame.associated_data,
                                  mbedtls_data, mbedtls_tag));
        ASSERT_EQ(portable_data, mbedtls_data);
        ASSERT_EQ(portable_tag, mbedtls_tag);

        // Each backend opens what the other one sealed
        ASSERT_EQ(
            CryptoStatus::kOk,
            portable.CcmOpen(frame.key, frame.nonce, frame.associated_data,
                             mbedtls_data, mbedtls_tag));
        ASSERT_EQ(frame.plaintext, mbedtls_data);
        ASSERT_EQ(CryptoStatus::kOk,
                  mbedtls.CcmOpen(frame.key, frame.nonce, frame.associated_data,
                                  portable_data, portable_tag));
        ASSERT_EQ(frame.plaintext, portable_data);
    }
}

TEST(CryptoCrossCheckTest, PortableAndMbedTlsBackendsAgreeOnCmac) {
    const PortableCryptoBackend portable;
    const MbedTlsCryptoBackend mbedtls;
    ByteSource source;
    for (size_t index = 0; index < kFrameCount; ++index) {
        SCOPED_TRACE(index);
        const RandomFrame frame(source, index);
        AesBlock portable_mac{};
        AesBlock mbedtls_mac{};
        ASSERT_EQ(CryptoStatus::kOk,
                  portable.Cmac(frame.key, frame.plaintext, portable_mac));
        ASSERT_EQ(CryptoStatus::kOk,
                  mbedtls.Cmac(frame.key, frame.plaintext, mbedtls_mac));
        ASSERT_EQ(portable_mac, mbedtls_mac);
    }
}

TEST(CryptoCrossCheckTest, PortableAndMbedTlsBackendsAgreeOnCtrAndBlock) {
    const PortableCryptoBackend portable;
    const MbedTlsCryptoBackend mbedtls;
    ByteSource source;
    for (size_t index = 0; index < kFrameCount; ++index) {
        SCOPED_TRACE(index);
        const RandomFrame frame(source, index);
        AesBlock counter{};
        source.Fill(counter);
        if (index % 3 == 0) {
            // Force carries through the low counter bytes
            std::fill(counter.begin() + 4 + index % 12, counter.end(), 0xFF);
        }

        std::vector<uint8_t> portable_data = frame.plaintext;
        std::vector<uint8_t> mbedtls_data = frame.plaintext;
        ASSERT_EQ(CryptoStatus::kOk,
                  portable.CtrCrypt(frame.key, counter, portable_data));
        ASSERT_EQ(CryptoStatus::kOk,
                  mbedtls.CtrCrypt(frame.key, counter, mbedtls_data));
        ASSERT_EQ(portable_data, mbedtls_data);

#if defined(MBEDTLS_CIPHER_MODE_CTR)
        // The mbedTLS CTR function is a third, independent implementation
        mbedtls_aes_context context;
        mbedtls_aes_init(&context);
        ASSERT_EQ(0, mbedtls_aes_setkey_enc(&context, frame.key.data(), 128));
        std::vector<uint8_t> reference(frame.plaintext.size() + 1);
        size_t stream_offset = 0;
        AesBlock running_counter = counter;
        AesBlock stream_block{};
        ASSERT_EQ(0, mbedtls_aes_crypt_ctr(
                         &context, frame.plaintext.size(), &stream_offset,
                         running_counter.data(), stream_block.data(),
                         frame.plaintext.empty() ? reference.data()
                                                 : frame.plaintext.data(),
                         reference.data()));
        mbedtls_aes_free(&context);
        reference.resize(frame.plaintext.size());
        ASSERT_EQ(reference, portable_data);
#endif  // MBEDTLS_CIPHER_MODE_CTR

        AesBlock portable_block{};
        AesBlock mbedtls_block{};
        ASSERT_EQ(CryptoStatus::kOk,
                  portable.EncryptBlock(frame.key, counter, portable_block));
        ASSERT_EQ(CryptoStatus::kOk,
                  mbedtls.EncryptBlock(frame.key, counter, mbedtls_block));
        ASSERT_EQ(portable_block, mbedtls_block);
    }
}

#if defined(MBEDTLS_CCM_C)
TEST(CryptoCrossCheckTest, BackendsMatchTheMbedTlsCcmModule) {
    const PortableCryptoBackend portable;
    const MbedTlsCryptoBackend mbedtls;
    ByteSource source;
    for (size_t index = 0; index < kFrameCount; ++index) {
        SCOPED_TRACE(index);
        const RandomFrame frame(source, index);

        mbedtls_ccm_context context;
        mbedtls_ccm_init(&context);
        ASSERT_EQ(0, mbedtls_ccm_setkey(&context, MBEDTLS_CIPHER_ID_AES,
                                        frame.key.data(), 128));
        std::vector<uint8_t> reference_data(frame.plaintext.size());
        std::vector<uint8_t> reference_tag(frame.tag_size);
        ASSERT_EQ(0, mbedtls_ccm_encrypt_and_tag(
                         &context, frame.plaintext.size(), frame.nonce.data(),
                         frame.nonce.size(), frame.associated_data.data(),
                         frame.associated_data.size(), frame.plaintext.data(),
                         reference_data.data(), reference_tag.data(),
                         reference_tag.size()));

        for (const ICryptoBackend* backend :
             {static_cast<const ICryptoBackend*>(&portable),
              static_cast<const ICryptoBackend*>(&mbedtls)}) {
            std::vector<uint8_t> data = frame.plaintext;
            std::vector<uint8_t> tag(frame.tag_size);
            ASSERT_EQ(CryptoStatus::kOk,
                      backend->CcmSeal(frame.key, frame.nonce,
                                       frame.associated_data, data, tag));
            ASSERT_EQ(reference_data, data);
            ASSERT_EQ(reference_tag, tag);
        }

        // mbedTLS opens a frame sealed by the library
        std::vector<uint8_t> opened(frame.plaintext.size());
        ASSERT_EQ(
            0, mbedtls_ccm_auth_decrypt(
                   &context, reference_data.size(), frame.nonce.data(),
                   frame.nonce.size(), frame.associated_data.data(),
                   frame.associated_data.size(), reference_data.data(),
                   opened.data(), reference_tag.data(), reference_tag.size()));
        ASSERT_EQ(frame.plaintext, opened);
        mbedtls_ccm_free(&context);
    }
}
#else
TEST(CryptoCrossCheckTest, BackendsMatchTheMbedTlsCcmModule) {
    GTEST_SKIP() << "This mbedTLS build has no CCM module";
}
#endif  // MBEDTLS_CCM_C

#if defined(MBEDTLS_CMAC_C)
TEST(CryptoCrossCheckTest, BackendsMatchTheMbedTlsCmacModule) {
    const PortableCryptoBackend portable;
    const MbedTlsCryptoBackend mbedtls;
    const mbedtls_cipher_info_t* cipher_info =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    ASSERT_NE(nullptr, cipher_info);
    ByteSource source;
    for (size_t index = 0; index < kFrameCount; ++index) {
        SCOPED_TRACE(index);
        const RandomFrame frame(source, index);
        // mbedTLS rejects a null input pointer, even for an empty message
        const uint8_t no_bytes = 0;
        const uint8_t* input =
            frame.plaintext.empty() ? &no_bytes : frame.plaintext.data();
        AesBlock reference{};
        ASSERT_EQ(
            0, mbedtls_cipher_cmac(cipher_info, frame.key.data(), 128, input,
                                   frame.plaintext.size(), reference.data()));
        AesBlock mac{};
        ASSERT_EQ(CryptoStatus::kOk,
                  portable.Cmac(frame.key, frame.plaintext, mac));
        ASSERT_EQ(reference, mac);
        ASSERT_EQ(CryptoStatus::kOk,
                  mbedtls.Cmac(frame.key, frame.plaintext, mac));
        ASSERT_EQ(reference, mac);
    }
}
#else
TEST(CryptoCrossCheckTest, BackendsMatchTheMbedTlsCmacModule) {
    GTEST_SKIP() << "This mbedTLS build has no CMAC module";
}
#endif  // MBEDTLS_CMAC_C

#else  // LORAMESHER_SECURITY_USE_MBEDTLS

TEST(CryptoCrossCheckTest, PortableAndMbedTlsBackendsAgree) {
    GTEST_SKIP() << "mbedTLS is not linked in this build";
}

#endif  // LORAMESHER_SECURITY_USE_MBEDTLS

}  // namespace
}  // namespace security
}  // namespace loramesher
