/**
 * @file i_crypto_backend.hpp
 * @brief Interface of the AES-128 primitives used to protect frames
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "utils/compat/span.hpp"

namespace loramesher {
namespace security {

/// Size of an AES block and of an AES-128 key, in bytes
constexpr size_t kAesBlockSize = 16;
/// Size of the CCM nonce, in bytes
constexpr size_t kCcmNonceSize = 13;
/// Short CCM tag length, in bytes
constexpr size_t kCcmShortTagSize = 4;
/// Long CCM tag length, in bytes
constexpr size_t kCcmLongTagSize = 8;
/// Largest payload one CCM call takes with a 13-byte nonce, in bytes
constexpr size_t kCcmMaxDataSize = 65535;
/// Largest associated data one CCM call takes, in bytes
constexpr size_t kCcmMaxAssociatedDataSize = 0xFEFF;
/// Shortest truncated CMAC tag, in bytes
constexpr size_t kCmacMinTagSize = 3;
/// Full CMAC tag length, in bytes
constexpr size_t kCmacMaxTagSize = kAesBlockSize;

using AesBlock = std::array<uint8_t, kAesBlockSize>;
using Aes128Key = std::array<uint8_t, kAesBlockSize>;
using CcmNonce = std::array<uint8_t, kCcmNonceSize>;

/**
 * @brief Outcome of a crypto primitive
 *
 * A plain enum keeps the failure path free of heap allocation. A frame with
 * a bad tag is an expected event on a radio link.
 */
enum class CryptoStatus : uint8_t {
    kOk = 0,           ///< Operation completed
    kInvalidArgument,  ///< A length is outside what the primitive accepts
    kAuthFailed,       ///< The tag does not match, nothing was released
    kBackendError,     ///< The AES implementation reported a failure
};

/**
 * @brief AES-128 block, CTR, CMAC and CCM behind one interface
 *
 * Every call takes the key, so one backend serves any number of keys and
 * holds no key material between calls. Calls work on caller buffers and do
 * not allocate. A backend instance has no mutable state, so concurrent calls
 * from several tasks are safe.
 *
 * The caller owns nonce uniqueness. A nonce must never be used twice with
 * the same key.
 */
class ICryptoBackend {
   public:
    virtual ~ICryptoBackend() = default;

    /**
     * @brief Encrypt data in place and compute its CCM tag
     *
     * @param key AES-128 key
     * @param nonce Nonce, unique for this key
     * @param associated_data Bytes that are authenticated and stay readable
     * @param data Plaintext on entry, ciphertext of the same length on return
     * @param tag Receives the tag. Its size selects the tag length and must
     *        be kCcmShortTagSize or kCcmLongTagSize
     * @return kOk, kInvalidArgument with the buffers left untouched, or
     *         kBackendError with data and tag zeroed
     */
    virtual CryptoStatus CcmSeal(const Aes128Key& key, const CcmNonce& nonce,
                                 std::span<const uint8_t> associated_data,
                                 std::span<uint8_t> data,
                                 std::span<uint8_t> tag) const = 0;

    /**
     * @brief Verify a CCM tag and decrypt data in place
     *
     * The tag comparison runs in constant time.
     *
     * @param key AES-128 key
     * @param nonce Nonce the sender used
     * @param associated_data Bytes that were authenticated with the data
     * @param data Ciphertext on entry, plaintext of the same length on return
     * @param tag Received tag of kCcmShortTagSize or kCcmLongTagSize bytes
     * @return kOk when the tag matches. On kAuthFailed and kBackendError
     *         data is zeroed, so no unauthenticated plaintext is released.
     *         kInvalidArgument leaves data untouched
     */
    virtual CryptoStatus CcmOpen(const Aes128Key& key, const CcmNonce& nonce,
                                 std::span<const uint8_t> associated_data,
                                 std::span<uint8_t> data,
                                 std::span<const uint8_t> tag) const = 0;

    /**
     * @brief Compute the AES-128-CMAC of a message (RFC 4493)
     *
     * @param key AES-128 key
     * @param message Message to authenticate, may be empty
     * @param mac Receives the leading bytes of the MAC. Its size selects the
     *        tag length, from kCmacMinTagSize to kCmacMaxTagSize
     * @return kOk, kInvalidArgument with mac left untouched, or
     *         kBackendError with mac zeroed
     */
    virtual CryptoStatus Cmac(const Aes128Key& key,
                              std::span<const uint8_t> message,
                              std::span<uint8_t> mac) const = 0;

    /**
     * @brief Check a full or truncated AES-128-CMAC tag
     *
     * The comparison runs in constant time.
     *
     * @param key AES-128 key
     * @param message Message the tag was computed over
     * @param tag Received tag of kCmacMinTagSize to kCmacMaxTagSize bytes,
     *        compared with the leading bytes of the MAC
     * @return kOk when the tag matches, kAuthFailed when it does not, or
     *         kInvalidArgument or kBackendError
     */
    virtual CryptoStatus CmacVerify(const Aes128Key& key,
                                    std::span<const uint8_t> message,
                                    std::span<const uint8_t> tag) const = 0;

    /**
     * @brief Encrypt or decrypt data in place with AES-128-CTR
     *
     * Implements NIST SP 800-38A counter mode. The counter block is treated
     * as one 128-bit big-endian number and grows by one for every 16 bytes
     * of data. Encryption and decryption are the same operation.
     *
     * CTR gives no authenticity. The caller pairs it with a MAC and makes
     * sure that no counter block is ever used twice with the same key.
     *
     * @param key AES-128 key
     * @param initial_counter Counter block of the first 16 bytes of data
     * @param data Data to transform in place, of any length
     * @return kOk, or kBackendError with data zeroed
     */
    virtual CryptoStatus CtrCrypt(const Aes128Key& key,
                                  const AesBlock& initial_counter,
                                  std::span<uint8_t> data) const = 0;

    /**
     * @brief Encrypt one block with AES-128
     *
     * @param key AES-128 key
     * @param input Block to encrypt
     * @param output Receives the encrypted block, may be the same as input
     * @return kOk, or kBackendError with output zeroed
     */
    virtual CryptoStatus EncryptBlock(const Aes128Key& key,
                                      const AesBlock& input,
                                      AesBlock& output) const = 0;
};

}  // namespace security
}  // namespace loramesher
