/**
 * @file crypto_backend.hpp
 * @brief Crypto backends for the supported platforms
 */

#pragma once

#include <cstring>

#include "security/aes_modes.hpp"
#include "security/i_crypto_backend.hpp"
#include "security/mbedtls_aes128.hpp"
#include "security/software_aes128.hpp"

namespace loramesher {
namespace security {

/**
 * @brief ICryptoBackend over one AES-128 block implementation
 *
 * The CTR, CCM and CMAC code is the same for every block implementation, so a
 * platform only supplies the block encryption. The block object lives on the
 * stack for the length of one call and wipes its key schedule afterwards.
 *
 * @tparam Block Block cipher constructible from an Aes128Key, derived from
 *         IAes128Block, with an IsHealthy() accessor
 */
template <typename Block>
class BlockCryptoBackend final : public ICryptoBackend {
   public:
    CryptoStatus CcmSeal(const Aes128Key& key, const CcmNonce& nonce,
                         std::span<const uint8_t> associated_data,
                         std::span<uint8_t> data,
                         std::span<uint8_t> tag) const override {
        if (!IsFrameTagSize(tag.size())) {
            return CryptoStatus::kInvalidArgument;
        }
        Block block(key);
        CryptoStatus status =
            security::CcmSeal(block, nonce, associated_data, data, tag);
        if (status == CryptoStatus::kOk && !block.IsHealthy()) {
            SecureZero(data);
            SecureZero(tag);
            status = CryptoStatus::kBackendError;
        }
        return status;
    }

    CryptoStatus CcmOpen(const Aes128Key& key, const CcmNonce& nonce,
                         std::span<const uint8_t> associated_data,
                         std::span<uint8_t> data,
                         std::span<const uint8_t> tag) const override {
        if (!IsFrameTagSize(tag.size())) {
            return CryptoStatus::kInvalidArgument;
        }
        Block block(key);
        CryptoStatus status =
            security::CcmOpen(block, nonce, associated_data, data, tag);
        if (status == CryptoStatus::kOk && !block.IsHealthy()) {
            SecureZero(data);
            status = CryptoStatus::kBackendError;
        }
        return status;
    }

    CryptoStatus Cmac(const Aes128Key& key, std::span<const uint8_t> message,
                      std::span<uint8_t> mac) const override {
        if (!IsCmacTagSize(mac.size())) {
            return CryptoStatus::kInvalidArgument;
        }
        AesBlock full_mac{};
        const CryptoStatus status = FullCmac(key, message, full_mac);
        if (status == CryptoStatus::kOk) {
            std::memcpy(mac.data(), full_mac.data(), mac.size());
        } else {
            SecureZero(mac);
        }
        SecureZero(full_mac);
        return status;
    }

    CryptoStatus CmacVerify(const Aes128Key& key,
                            std::span<const uint8_t> message,
                            std::span<const uint8_t> tag) const override {
        if (!IsCmacTagSize(tag.size())) {
            return CryptoStatus::kInvalidArgument;
        }
        AesBlock full_mac{};
        CryptoStatus status = FullCmac(key, message, full_mac);
        if (status == CryptoStatus::kOk &&
            !ConstantTimeEqual(
                std::span<const uint8_t>(full_mac.data(), tag.size()), tag)) {
            status = CryptoStatus::kAuthFailed;
        }
        SecureZero(full_mac);
        return status;
    }

    CryptoStatus CtrCrypt(const Aes128Key& key, const AesBlock& initial_counter,
                          std::span<uint8_t> data) const override {
        Block block(key);
        security::CtrCrypt(block, initial_counter, data);
        if (!block.IsHealthy()) {
            SecureZero(data);
            return CryptoStatus::kBackendError;
        }
        return CryptoStatus::kOk;
    }

    CryptoStatus EncryptBlock(const Aes128Key& key, const AesBlock& input,
                              AesBlock& output) const override {
        Block block(key);
        block.EncryptBlock(input.data(), output.data());
        if (!block.IsHealthy()) {
            SecureZero(output);
            return CryptoStatus::kBackendError;
        }
        return CryptoStatus::kOk;
    }

   private:
    static bool IsFrameTagSize(size_t size) {
        return size == kCcmShortTagSize || size == kCcmLongTagSize;
    }

    static bool IsCmacTagSize(size_t size) {
        return size >= kCmacMinTagSize && size <= kCmacMaxTagSize;
    }

    static CryptoStatus FullCmac(const Aes128Key& key,
                                 std::span<const uint8_t> message,
                                 AesBlock& mac) {
        Block block(key);
        security::Cmac(block, message, mac);
        return block.IsHealthy() ? CryptoStatus::kOk
                                 : CryptoStatus::kBackendError;
    }
};

/// Backend that runs everywhere, used by native builds and plain MCUs
using PortableCryptoBackend = BlockCryptoBackend<SoftwareAes128>;

#if LORAMESHER_SECURITY_USE_MBEDTLS
/// Backend on the mbedTLS AES module, hardware accelerated on ESP32
using MbedTlsCryptoBackend = BlockCryptoBackend<MbedTlsAes128>;

using DefaultCryptoBackend = MbedTlsCryptoBackend;
#else
using DefaultCryptoBackend = PortableCryptoBackend;
#endif

}  // namespace security
}  // namespace loramesher
