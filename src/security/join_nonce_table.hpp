/**
 * @file join_nonce_table.hpp
 * @brief Join nonces a sponsor stores and a joiner remembers
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "security/frame_format.hpp"
#include "types/messages/base_header.hpp"

namespace loramesher {
namespace security {

using JoinNonce = std::array<uint8_t, kJoinNonceSize>;

/**
 * @brief Nonces of the joiners a member heard directly
 *
 * The member binds the stored nonce into the MIC of the final
 * JOIN_RESPONSE. An entry is replaced by a newer request of the same joiner,
 * is removed once it was bound, and expires after the joining timeout.
 */
class SponsorNonceTable {
   public:
    static constexpr size_t kCapacity = 4;

    /**
     * @brief Store the nonce of a request heard from the joiner itself
     *
     * Reuses the entry of the same joiner, then a free or expired entry,
     * then the oldest one.
     */
    void Store(AddressType joiner, const JoinNonce& nonce, uint32_t now_ms,
               uint32_t lifetime_ms) {
        Entry* target = nullptr;
        for (Entry& entry : entries_) {
            if (entry.used && entry.joiner == joiner) {
                target = &entry;
                break;
            }
        }
        if (target == nullptr) {
            for (Entry& entry : entries_) {
                if (!entry.used || IsExpired(entry, now_ms, lifetime_ms)) {
                    target = &entry;
                    break;
                }
            }
        }
        if (target == nullptr) {
            target = &entries_[0];
            for (Entry& entry : entries_) {
                if (now_ms - entry.stored_at_ms >
                    now_ms - target->stored_at_ms) {
                    target = &entry;
                }
            }
        }
        *target = Entry{true, joiner, nonce, now_ms};
    }

    /**
     * @brief Nonce of a joiner, when a live entry exists
     *
     * @return True when nonce was written
     */
    bool Find(AddressType joiner, uint32_t now_ms, uint32_t lifetime_ms,
              JoinNonce& nonce) const {
        for (const Entry& entry : entries_) {
            if (entry.used && entry.joiner == joiner &&
                !IsExpired(entry, now_ms, lifetime_ms)) {
                nonce = entry.nonce;
                return true;
            }
        }
        return false;
    }

    /// Remove the entry of a joiner, after its nonce was bound once
    void Remove(AddressType joiner) {
        for (Entry& entry : entries_) {
            if (entry.used && entry.joiner == joiner) {
                entry.used = false;
            }
        }
    }

    void Clear() {
        for (Entry& entry : entries_) {
            entry.used = false;
        }
    }

   private:
    struct Entry {
        bool used = false;
        AddressType joiner = 0;
        JoinNonce nonce{};
        uint32_t stored_at_ms = 0;
    };

    static bool IsExpired(const Entry& entry, uint32_t now_ms,
                          uint32_t lifetime_ms) {
        return now_ms - entry.stored_at_ms >= lifetime_ms;
    }

    std::array<Entry, kCapacity> entries_{};
};

/**
 * @brief The last two join nonces a joining node sent
 *
 * A retry draws a new nonce while the response to the request before it may
 * still be in flight, so exactly two are kept.
 */
class OwnJoinNonces {
   public:
    static constexpr size_t kCapacity = 2;

    /// Remember the nonce of a request this node just sealed
    void Push(const JoinNonce& nonce) {
        nonces_[1] = nonces_[0];
        nonces_[0] = nonce;
        if (count_ < kCapacity) {
            ++count_;
        }
    }

    /// Number of remembered nonces, 0 when the node is not joining
    size_t Count() const { return count_; }

    /// Nonce by age, 0 is the latest
    const JoinNonce& Get(size_t index) const { return nonces_[index]; }

    /// Forget the nonces when the node leaves JOINING
    void Clear() { count_ = 0; }

   private:
    std::array<JoinNonce, kCapacity> nonces_{};
    size_t count_ = 0;
};

}  // namespace security
}  // namespace loramesher
