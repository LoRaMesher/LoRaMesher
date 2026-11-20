/**
 * @file software_aes128.hpp
 * @brief Portable AES-128 block encryption
 */

#pragma once

#include "security/aes_modes.hpp"

namespace loramesher {
namespace security {

/**
 * @brief AES-128 block encryption in plain C++ (FIPS 197)
 *
 * Runs on every platform and needs no hardware support. It only implements
 * the encrypt direction, which is all that CCM and CMAC use. The S-box is a
 * lookup table, so the running time can depend on the data on processors
 * with a data cache. Targets with hardware AES use their own block class.
 */
class SoftwareAes128 final : public IAes128Block {
   public:
    explicit SoftwareAes128(const Aes128Key& key);
    ~SoftwareAes128();

    SoftwareAes128(const SoftwareAes128&) = delete;
    SoftwareAes128& operator=(const SoftwareAes128&) = delete;

    void EncryptBlock(const uint8_t* in, uint8_t* out) override;

    /// Software encryption cannot fail
    bool IsHealthy() const { return true; }

   private:
    static constexpr size_t kRounds = 10;

    uint8_t round_keys_[(kRounds + 1) * kAesBlockSize];
};

}  // namespace security
}  // namespace loramesher
