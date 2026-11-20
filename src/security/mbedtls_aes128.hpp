/**
 * @file mbedtls_aes128.hpp
 * @brief AES-128 block encryption through mbedTLS
 */

#pragma once

// mbedTLS ships with ESP-IDF and the ESP32 Arduino core, where its AES uses
// the hardware accelerator. Other builds opt in with
// -DLORAMESHER_SECURITY_USE_MBEDTLS=1 when they link mbedTLS.
#if !defined(LORAMESHER_SECURITY_USE_MBEDTLS)
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#define LORAMESHER_SECURITY_USE_MBEDTLS 1
#else
#define LORAMESHER_SECURITY_USE_MBEDTLS 0
#endif
#endif

#if LORAMESHER_SECURITY_USE_MBEDTLS

#include <mbedtls/aes.h>

#include "security/aes_modes.hpp"

namespace loramesher {
namespace security {

/**
 * @brief AES-128 block encryption by the mbedTLS AES module
 *
 * Only the AES module is used. It needs no heap, unlike the mbedTLS CCM and
 * CMAC modules, which allocate a cipher context for every key they set.
 */
class MbedTlsAes128 final : public IAes128Block {
   public:
    explicit MbedTlsAes128(const Aes128Key& key) {
        mbedtls_aes_init(&context_);
        healthy_ = mbedtls_aes_setkey_enc(&context_, key.data(),
                                          8 * kAesBlockSize) == 0;
    }

    ~MbedTlsAes128() { mbedtls_aes_free(&context_); }

    MbedTlsAes128(const MbedTlsAes128&) = delete;
    MbedTlsAes128& operator=(const MbedTlsAes128&) = delete;

    void EncryptBlock(const uint8_t* in, uint8_t* out) override {
        if (mbedtls_aes_crypt_ecb(&context_, MBEDTLS_AES_ENCRYPT, in, out) !=
            0) {
            healthy_ = false;
        }
    }

    /// False once mbedTLS has reported an error for this key
    bool IsHealthy() const { return healthy_; }

   private:
    mbedtls_aes_context context_;
    bool healthy_ = false;
};

}  // namespace security
}  // namespace loramesher

#endif  // LORAMESHER_SECURITY_USE_MBEDTLS
