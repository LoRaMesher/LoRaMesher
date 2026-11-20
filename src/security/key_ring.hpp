/**
 * @file key_ring.hpp
 * @brief Traffic keys of the network key epochs a node holds
 */

#pragma once

#include <cstdint>

#include "security/aes_modes.hpp"
#include "security/counter_record_codec.hpp"
#include "security/i_key_provider.hpp"
#include "security/kdf.hpp"

namespace loramesher {
namespace security {

/**
 * @brief Derived network traffic keys of at most two epochs
 *
 * A frame names its epoch by one parity bit. The two epochs a node holds
 * always differ in parity, so the ring has one slot per parity and a frame
 * selects its key without any trial decryption. Derived keys live in RAM
 * only and are wiped when a slot is cleared.
 */
class KeyRing {
   public:
    KeyRing() = default;

    ~KeyRing() { Clear(); }

    KeyRing(const KeyRing&) = delete;
    KeyRing& operator=(const KeyRing&) = delete;

    /**
     * @brief Derive and hold the traffic keys of a network key
     *
     * A key of a lower epoch is never installed over a higher one of the
     * same parity.
     *
     * @return kOk, kInvalidArgument when the epoch is above kMaxEpoch or
     *         below the held epoch of the same parity, or kBackendError
     */
    CryptoStatus Install(const ICryptoBackend& backend, const NetworkKey& key) {
        if (key.epoch > kMaxEpoch) {
            return CryptoStatus::kInvalidArgument;
        }
        Slot& slot = slots_[key.epoch & 1u];
        if (slot.used && slot.epoch > key.epoch) {
            return CryptoStatus::kInvalidArgument;
        }
        NetworkTrafficKeys derived;
        const CryptoStatus status =
            DeriveNetworkTrafficKeys(backend, key.key, key.epoch, derived);
        if (status == CryptoStatus::kOk) {
            slot.keys = derived;
            slot.epoch = key.epoch;
            slot.used = true;
        }
        Wipe(derived);
        return status;
    }

    /// Keys of an exact epoch, or nullptr
    const NetworkTrafficKeys* FindByEpoch(uint8_t epoch) const {
        const Slot& slot = slots_[epoch & 1u];
        return slot.used && slot.epoch == epoch ? &slot.keys : nullptr;
    }

    /**
     * @brief Keys of the held epoch with this parity
     *
     * @param parity Epoch bit of the type byte, 0 or 1
     * @param epoch Receives the full epoch number
     * @return The keys, or nullptr when no held epoch has that parity
     */
    const NetworkTrafficKeys* FindByParity(uint8_t parity,
                                           uint8_t& epoch) const {
        const Slot& slot = slots_[parity & 1u];
        if (!slot.used) {
            return nullptr;
        }
        epoch = slot.epoch;
        return &slot.keys;
    }

    /// Erase the keys of an epoch
    void Retire(uint8_t epoch) {
        Slot& slot = slots_[epoch & 1u];
        if (slot.used && slot.epoch == epoch) {
            Wipe(slot.keys);
            slot.used = false;
        }
    }

    /// Erase every key
    void Clear() {
        for (Slot& slot : slots_) {
            Wipe(slot.keys);
            slot.used = false;
        }
    }

    /// Number of held epochs
    size_t Count() const {
        return (slots_[0].used ? 1u : 0u) + (slots_[1].used ? 1u : 0u);
    }

   private:
    struct Slot {
        bool used = false;
        uint8_t epoch = 0;
        NetworkTrafficKeys keys{};
    };

    static void Wipe(NetworkTrafficKeys& keys) {
        SecureZero(keys.he);
        SecureZero(keys.hm);
        SecureZero(keys.hp);
        SecureZero(keys.ge);
        SecureZero(keys.gm);
    }

    Slot slots_[2];
};

}  // namespace security
}  // namespace loramesher
