/**
 * @file message_cache.hpp
 * @brief Thread-safe per-node sequence counter and message de-duplication cache
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "types/messages/base_header.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Sequence allocator and recently-seen (source, seq) cache
 *
 * Shared by every send and receive path of a node. Sends run on the
 * application thread and receives on the protocol task, so all operations are
 * thread-safe.
 */
class MessageCache {
   public:
    static constexpr size_t kCapacity = 32;

    /// Allocate the next per-node sequence number.
    uint8_t NextSeq() {
        return static_cast<uint8_t>(
            seq_.fetch_add(1, std::memory_order_relaxed) + 1);
    }

    /// True if (source, seq) is in the cache.
    bool Contains(AddressType source, uint8_t seq) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ContainsLocked(source, seq);
    }

    /// Record (source, seq), evicting the oldest entry when full.
    void Record(AddressType source, uint8_t seq) {
        std::lock_guard<std::mutex> lock(mutex_);
        RecordLocked(source, seq);
    }

    /**
     * @brief Record (source, seq) unless it is already cached
     *
     * @return true if the entry was new and has been recorded
     */
    bool RecordIfNew(AddressType source, uint8_t seq) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ContainsLocked(source, seq)) {
            return false;
        }
        RecordLocked(source, seq);
        return true;
    }

    /**
     * @brief Clear the cache of received messages
     *
     * The sequence counter keeps counting: neighbours may still cache this
     * node's recent packets, so restarting it would get new packets dropped
     * as duplicates.
     */
    void Reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.fill({});
        head_ = 0;
    }

   private:
    struct Entry {
        AddressType source = 0;
        uint8_t seq = 0;
        bool valid = false;
    };

    bool ContainsLocked(AddressType source, uint8_t seq) const {
        for (const auto& entry : entries_) {
            if (entry.valid && entry.source == source && entry.seq == seq) {
                return true;
            }
        }
        return false;
    }

    void RecordLocked(AddressType source, uint8_t seq) {
        entries_[head_] = {source, seq, true};
        head_ = static_cast<uint8_t>((head_ + 1) % kCapacity);
    }

    mutable std::mutex mutex_;
    std::array<Entry, kCapacity> entries_{};
    uint8_t head_ = 0;
    std::atomic<uint8_t> seq_{0};
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
