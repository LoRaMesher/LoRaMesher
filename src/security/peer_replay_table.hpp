/**
 * @file peer_replay_table.hpp
 * @brief Receive state of the peers a node hears
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "security/replay_state.hpp"
#include "types/messages/base_header.hpp"

namespace loramesher {
namespace security {

/**
 * @brief Table of the hop layer replay state, one entry per peer address
 *
 * The storage is allocated once, at construction. When the table is full,
 * the least recently heard peer that is absent from the routing table is
 * replaced. State is kept across DISCOVERY, JOINING and FAULT_RECOVERY.
 * Only a reboot or a newer key epoch of the peer clears it.
 */
class PeerReplayTable {
   public:
    /// True when the routing table holds the address
    using IsRoutedFn = std::function<bool(AddressType)>;

    explicit PeerReplayTable(size_t capacity) { entries_.reserve(capacity); }

    /// Tell the table how to ask whether a peer is in the routing table
    void SetIsRouted(IsRoutedFn is_routed) {
        is_routed_ = std::move(is_routed);
    }

    /// State of a peer, or nullptr when the peer was never heard
    const HopReplayState* Find(AddressType peer) const {
        for (const Entry& entry : entries_) {
            if (entry.address == peer) {
                return &entry.state;
            }
        }
        return nullptr;
    }

    /**
     * @brief State of a peer, created when the peer is new
     *
     * Marks the peer as the most recently heard one.
     *
     * @param peer Address of the peer
     * @param epoch Epoch of the frame that creates the entry
     * @return The state, or nullptr when the table has no capacity
     */
    HopReplayState* Touch(AddressType peer, uint8_t epoch) {
        ++clock_;
        for (Entry& entry : entries_) {
            if (entry.address == peer) {
                entry.last_heard = clock_;
                return &entry.state;
            }
        }
        if (entries_.size() < entries_.capacity()) {
            entries_.push_back(Entry{peer, clock_, HopReplayState(epoch)});
            return &entries_.back().state;
        }
        Entry* victim = PickVictim();
        if (victim == nullptr) {
            return nullptr;
        }
        *victim = Entry{peer, clock_, HopReplayState(epoch)};
        return &victim->state;
    }

    /// Number of peers with state
    size_t Size() const { return entries_.size(); }

    size_t Capacity() const { return entries_.capacity(); }

    /// Forget every peer, as after a reboot
    void Clear() { entries_.clear(); }

   private:
    struct Entry {
        AddressType address;
        uint32_t last_heard;
        HopReplayState state;
    };

    /// Least recently heard peer outside the routing table. When every peer
    /// is routed, the least recently heard of all
    Entry* PickVictim() {
        Entry* unrouted = nullptr;
        Entry* any = nullptr;
        for (Entry& entry : entries_) {
            if (any == nullptr || entry.last_heard < any->last_heard) {
                any = &entry;
            }
            const bool routed = is_routed_ && is_routed_(entry.address);
            if (!routed && (unrouted == nullptr ||
                            entry.last_heard < unrouted->last_heard)) {
                unrouted = &entry;
            }
        }
        return unrouted != nullptr ? unrouted : any;
    }

    std::vector<Entry> entries_;
    IsRoutedFn is_routed_;
    uint32_t clock_ = 0;
};

}  // namespace security
}  // namespace loramesher
