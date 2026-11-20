/**
 * @file aes_modes.hpp
 * @brief CTR and CMAC built on any AES-128 block encryption
 */

#pragma once

#include "security/i_crypto_backend.hpp"

namespace loramesher {
namespace security {

/**
 * @brief One keyed AES-128 block encryption
 *
 * CTR and CMAC only need the encrypt direction of the cipher.
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
