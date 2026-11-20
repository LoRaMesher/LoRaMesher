/**
 * @file aes_modes.hpp
 * @brief CTR, CCM and CMAC built on any AES-128 block encryption
 */

#pragma once

#include "security/i_crypto_backend.hpp"

namespace loramesher {
namespace security {

/**
 * @brief One keyed AES-128 block encryption
 *
 * CCM and CMAC only need the encrypt direction of the cipher.
 */
class IAes128Block {
   public:
    /**
     * @brief Encrypt one 16-byte block
     *
     * @param in Block to encrypt
     * @param out Receives the encrypted block, may be the same buffer as in
     */
    virtual void EncryptBlock(const uint8_t* in, uint8_t* out) = 0;

   protected:
    ~IAes128Block() = default;
};

/**
 * @brief Compare two byte ranges without an early exit
 *
 * @return True when both ranges have the same size and content
 */
bool ConstantTimeEqual(std::span<const uint8_t> a, std::span<const uint8_t> b);

/**
 * @brief Overwrite a buffer with zeros in a way the compiler keeps
 */
void SecureZero(std::span<uint8_t> buffer);

/**
 * @brief CTR mode over a keyed block cipher (NIST SP 800-38A)
 *
 * The counter block is one 128-bit big-endian number that grows by one per
 * block and wraps to zero after its highest value.
 *
 * @param cipher Keyed block cipher
 * @param initial_counter Counter block of the first 16 bytes of data
 * @param data Data to encrypt or decrypt in place, of any length
 */
void CtrCrypt(IAes128Block& cipher, const AesBlock& initial_counter,
              std::span<uint8_t> data);

/**
 * @brief CCM seal over a keyed block cipher (RFC 3610, NIST SP 800-38C)
 *
 * Accepts every nonce length from 7 to 13 bytes and every even tag length
 * from 4 to 16 bytes. ICryptoBackend narrows these to what frames use.
 *
 * @param cipher Keyed block cipher
 * @param nonce Nonce of 7 to 13 bytes
 * @param associated_data Authenticated bytes, at most kCcmMaxAssociatedDataSize
 * @param data Plaintext on entry, ciphertext on return
 * @param tag Receives the tag, its size selects the tag length
 * @return kOk, or kInvalidArgument with data and tag left untouched
 */
CryptoStatus CcmSeal(IAes128Block& cipher, std::span<const uint8_t> nonce,
                     std::span<const uint8_t> associated_data,
                     std::span<uint8_t> data, std::span<uint8_t> tag);

/**
 * @brief CCM open over a keyed block cipher
 *
 * @param cipher Keyed block cipher
 * @param nonce Nonce of 7 to 13 bytes
 * @param associated_data Authenticated bytes
 * @param data Ciphertext on entry, plaintext on return
 * @param tag Received tag
 * @return kOk, kAuthFailed with data zeroed, or kInvalidArgument with data
 *         left untouched
 */
CryptoStatus CcmOpen(IAes128Block& cipher, std::span<const uint8_t> nonce,
                     std::span<const uint8_t> associated_data,
                     std::span<uint8_t> data, std::span<const uint8_t> tag);

/**
 * @brief CMAC over a keyed block cipher (RFC 4493)
 *
 * @param cipher Keyed block cipher
 * @param message Message to authenticate, may be empty
 * @param mac Receives the 16-byte MAC
 */
void Cmac(IAes128Block& cipher, std::span<const uint8_t> message,
          AesBlock& mac);

}  // namespace security
}  // namespace loramesher
