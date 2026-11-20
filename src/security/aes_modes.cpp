/**
 * @file aes_modes.cpp
 * @brief CTR, CCM and CMAC built on any AES-128 block encryption
 */

#include "security/aes_modes.hpp"

#include <cstring>

namespace loramesher {
namespace security {

namespace {

constexpr size_t kCcmMinNonceSize = 7;
constexpr size_t kCcmMinTagSize = 4;
constexpr size_t kCcmMaxTagSize = 16;

/**
 * @brief Running CBC-MAC with an all-zero initial vector
 */
class CbcMac {
   public:
    explicit CbcMac(IAes128Block& cipher) : cipher_(cipher) {}

    ~CbcMac() { SecureZero(state_); }

    void Update(std::span<const uint8_t> bytes) {
        for (const uint8_t byte : bytes) {
            state_[fill_++] ^= byte;
            if (fill_ == kAesBlockSize) {
                Flush();
            }
        }
    }

    /// Close a partial block as if it were padded with zeros
    void Pad() {
        if (fill_ != 0) {
            Flush();
        }
    }

    const AesBlock& state() const { return state_; }

   private:
    void Flush() {
        cipher_.EncryptBlock(state_.data(), state_.data());
        fill_ = 0;
    }

    IAes128Block& cipher_;
    AesBlock state_{};
    size_t fill_ = 0;
};

/**
 * @brief Parameters and block formatting shared by CCM seal and open
 */
struct CcmLayout {
    size_t length_field_size;  ///< Bytes that encode the data length
    size_t tag_size;

    static bool IsValid(size_t nonce_size, size_t associated_size,
                        size_t data_size, size_t tag_size) {
        if (nonce_size < kCcmMinNonceSize || nonce_size > kCcmNonceSize) {
            return false;
        }
        if (tag_size < kCcmMinTagSize || tag_size > kCcmMaxTagSize ||
            tag_size % 2 != 0) {
            return false;
        }
        if (associated_size > kCcmMaxAssociatedDataSize) {
            return false;
        }
        const size_t length_field_size = kAesBlockSize - 1 - nonce_size;
        if (length_field_size < sizeof(uint64_t) &&
            (static_cast<uint64_t>(data_size) >> (8 * length_field_size)) !=
                0) {
            return false;
        }
        return true;
    }

    /// Write the big-endian value into the length or counter field
    void WriteField(AesBlock& block, uint64_t value) const {
        for (size_t i = 0; i < length_field_size; ++i) {
            block[kAesBlockSize - 1 - i] = static_cast<uint8_t>(value);
            value >>= 8;
        }
    }

    /// First CBC-MAC block: flags, nonce and data length
    AesBlock FirstMacBlock(std::span<const uint8_t> nonce, bool has_associated,
                           size_t data_size) const {
        AesBlock block{};
        block[0] = static_cast<uint8_t>((has_associated ? 0x40 : 0x00) |
                                        (((tag_size - 2) / 2) << 3) |
                                        (length_field_size - 1));
        std::memcpy(&block[1], nonce.data(), nonce.size());
        WriteField(block, data_size);
        return block;
    }

    /// Counter block number index. Index 0 masks the tag
    AesBlock CounterBlock(std::span<const uint8_t> nonce,
                          uint64_t index) const {
        AesBlock block{};
        block[0] = static_cast<uint8_t>(length_field_size - 1);
        std::memcpy(&block[1], nonce.data(), nonce.size());
        WriteField(block, index);
        return block;
    }
};

/**
 * @brief Run CCM over data in place and produce the tag
 *
 * @param decrypt True when data holds ciphertext
 */
void CcmProcess(IAes128Block& cipher, const CcmLayout& layout,
                std::span<const uint8_t> nonce,
                std::span<const uint8_t> associated_data,
                std::span<uint8_t> data, bool decrypt,
                std::span<uint8_t> tag_out) {
    CbcMac mac(cipher);
    mac.Update(
        layout.FirstMacBlock(nonce, !associated_data.empty(), data.size()));
    if (!associated_data.empty()) {
        const uint8_t length_prefix[2] = {
            static_cast<uint8_t>(associated_data.size() >> 8),
            static_cast<uint8_t>(associated_data.size())};
        mac.Update(length_prefix);
        mac.Update(associated_data);
        mac.Pad();
    }

    AesBlock key_stream;
    uint64_t block_index = 1;
    for (size_t offset = 0; offset < data.size(); offset += kAesBlockSize) {
        const size_t chunk_size = (data.size() - offset < kAesBlockSize)
                                      ? data.size() - offset
                                      : kAesBlockSize;
        const std::span<uint8_t> chunk = data.subspan(offset, chunk_size);
        const AesBlock counter = layout.CounterBlock(nonce, block_index++);
        cipher.EncryptBlock(counter.data(), key_stream.data());

        // The MAC always covers the plaintext
        if (!decrypt) {
            mac.Update(chunk);
        }
        for (size_t i = 0; i < chunk_size; ++i) {
            chunk[i] ^= key_stream[i];
        }
        if (decrypt) {
            mac.Update(chunk);
        }
    }
    mac.Pad();

    const AesBlock counter = layout.CounterBlock(nonce, 0);
    cipher.EncryptBlock(counter.data(), key_stream.data());
    for (size_t i = 0; i < tag_out.size(); ++i) {
        tag_out[i] = mac.state()[i] ^ key_stream[i];
    }
    SecureZero(key_stream);
}

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

CryptoStatus CcmSeal(IAes128Block& cipher, std::span<const uint8_t> nonce,
                     std::span<const uint8_t> associated_data,
                     std::span<uint8_t> data, std::span<uint8_t> tag) {
    if (!CcmLayout::IsValid(nonce.size(), associated_data.size(), data.size(),
                            tag.size())) {
        return CryptoStatus::kInvalidArgument;
    }
    const CcmLayout layout{kAesBlockSize - 1 - nonce.size(), tag.size()};
    CcmProcess(cipher, layout, nonce, associated_data, data, false, tag);
    return CryptoStatus::kOk;
}

CryptoStatus CcmOpen(IAes128Block& cipher, std::span<const uint8_t> nonce,
                     std::span<const uint8_t> associated_data,
                     std::span<uint8_t> data, std::span<const uint8_t> tag) {
    if (!CcmLayout::IsValid(nonce.size(), associated_data.size(), data.size(),
                            tag.size())) {
        return CryptoStatus::kInvalidArgument;
    }
    const CcmLayout layout{kAesBlockSize - 1 - nonce.size(), tag.size()};
    AesBlock expected_tag{};
    const std::span<uint8_t> expected(expected_tag.data(), tag.size());
    CcmProcess(cipher, layout, nonce, associated_data, data, true, expected);
    const bool authentic = ConstantTimeEqual(expected, tag);
    SecureZero(expected_tag);
    if (!authentic) {
        SecureZero(data);
        return CryptoStatus::kAuthFailed;
    }
    return CryptoStatus::kOk;
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
