/**
 * @file aes_modes.cpp
 * @brief CTR and CMAC built on any AES-128 block encryption
 */

#include "security/aes_modes.hpp"

namespace loramesher {
namespace security {

namespace {

/// Double a value in GF(2^128), the CMAC subkey step
void DoubleBlock(AesBlock& block) {
    const uint8_t carry = block[0] >> 7;
    for (size_t i = 0; i + 1 < kAesBlockSize; ++i) {
        block[i] = static_cast<uint8_t>((block[i] << 1) | (block[i + 1] >> 7));
    }
    block[kAesBlockSize - 1] = static_cast<uint8_t>(
        (block[kAesBlockSize - 1] << 1) ^ (0x87 & (0 - carry)));
}

}  // namespace

bool ConstantTimeEqual(std::span<const uint8_t> a, std::span<const uint8_t> b) {
    if (a.size() != b.size()) {
        return false;
    }
    volatile uint8_t difference = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        difference = difference | (a[i] ^ b[i]);
    }
    return difference == 0;
}

void SecureZero(std::span<uint8_t> buffer) {
    volatile uint8_t* bytes = buffer.data();
    for (size_t i = 0; i < buffer.size(); ++i) {
        bytes[i] = 0;
    }
}

void CtrCrypt(IAes128Block& cipher, const AesBlock& initial_counter,
              std::span<uint8_t> data) {
    AesBlock counter = initial_counter;
    AesBlock key_stream;
    for (size_t offset = 0; offset < data.size(); offset += kAesBlockSize) {
        cipher.EncryptBlock(counter.data(), key_stream.data());
        const size_t chunk_size = (data.size() - offset < kAesBlockSize)
                                      ? data.size() - offset
                                      : kAesBlockSize;
        for (size_t i = 0; i < chunk_size; ++i) {
            data[offset + i] ^= key_stream[i];
        }
        // Add one to the big-endian counter, with carry
        for (size_t i = kAesBlockSize; i-- > 0;) {
            if (++counter[i] != 0) {
                break;
            }
        }
    }
    SecureZero(key_stream);
}

void Cmac(IAes128Block& cipher, std::span<const uint8_t> message,
          AesBlock& mac) {
    AesBlock subkey{};
    cipher.EncryptBlock(subkey.data(), subkey.data());
    DoubleBlock(subkey);

    // Every block but the last goes through plain CBC-MAC
    const size_t last_size =
        message.empty() ? 0 : ((message.size() - 1) % kAesBlockSize) + 1;
    const size_t head_size = message.size() - last_size;

    AesBlock state{};
    for (size_t offset = 0; offset < head_size; offset += kAesBlockSize) {
        for (size_t i = 0; i < kAesBlockSize; ++i) {
            state[i] ^= message[offset + i];
        }
        cipher.EncryptBlock(state.data(), state.data());
    }

    for (size_t i = 0; i < last_size; ++i) {
        state[i] ^= message[head_size + i];
    }
    if (last_size != kAesBlockSize) {
        // An incomplete last block takes the padding and the second subkey
        state[last_size] ^= 0x80;
        DoubleBlock(subkey);
    }
    for (size_t i = 0; i < kAesBlockSize; ++i) {
        state[i] ^= subkey[i];
    }
    cipher.EncryptBlock(state.data(), mac.data());
    SecureZero(subkey);
    SecureZero(state);
}

}  // namespace security
}  // namespace loramesher
