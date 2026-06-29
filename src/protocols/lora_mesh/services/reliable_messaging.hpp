/**
 * @file reliable_messaging.hpp
 * @brief Group multicast + reliable-delivery glue extracted from NetworkService
 *
 * Owns the local state and logic for group (multicast) membership and, in
 * later steps, end-to-end reliable unicast/group delivery. Constructed by and
 * owned by NetworkService, which delegates the corresponding public API to it.
 *
 * Threading: this component does not own a mutex. It locks the coordinator's
 * mutex (passed by reference) so its critical sections stay identical to the
 * pre-extraction behavior and no second lock is introduced.
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "protocols/reliability/reliable_delivery.hpp"
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Group multicast and reliable-delivery glue for NetworkService
 */
class ReliableMessaging {
   public:
    /// Routing hop-count to a destination (>= 1; 1 when unknown).
    using HopsToDestFn = std::function<uint8_t(AddressType dest)>;
    /// Current superframe duration in milliseconds (0 when unavailable).
    using SuperframeDurationFn = std::function<uint32_t()>;

    /**
     * @param mutex Coordinator mutex guarding shared protocol state; locked by
     *              the public membership operations exactly as before.
     * @param hops_to_dest Closure returning the routing hop-count to a node.
     * @param superframe_duration Closure returning the superframe duration (ms).
     */
    ReliableMessaging(std::mutex& mutex, HopsToDestFn hops_to_dest,
                      SuperframeDurationFn superframe_duration);

    /// Estimate a retransmit timeout (ms) from hop count and superframe duration.
    uint32_t ComputeReliableTimeout(AddressType dest) const;

    /// Join a multicast group. @return Success or an error for invalid/full.
    Result JoinGroup(AddressType group);

    /// Leave a multicast group. @return Success (idempotent).
    Result LeaveGroup(AddressType group);

    /// @return true if this node is a member of @p group.
    bool IsMemberOfGroup(AddressType group) const;

    /// @return the list of groups this node currently belongs to.
    std::vector<AddressType> GetGroups() const;

    /// @return the destination recorded for an in-flight reliable @p seq, or 0.
    AddressType LookupReliableDest(uint8_t seq) const;

    /// Record the destination for an in-flight reliable @p seq.
    void RecordReliableDest(uint8_t seq, AddressType dest);

    /// Forget the destination recorded for a completed reliable @p seq.
    void ClearReliableDest(uint8_t seq);

   private:
    static constexpr size_t kMaxGroups = 8;
    std::array<AddressType, kMaxGroups> groups_{};
    uint8_t group_count_ = 0;

    /// Shadow table mapping an in-flight reliable seq to its destination, so the
    /// send_attempt closure can rebuild the message (the reliability component
    /// is destination-agnostic). Sized to the component's pending capacity.
    struct ReliableDest {
        bool valid = false;
        uint8_t seq = 0;
        AddressType dest = 0;
    };

    std::array<ReliableDest, reliability::ReliableDelivery::kMaxPending>
        reliable_dest_{};

    std::mutex& mutex_;  ///< Coordinator mutex (not owned)
    HopsToDestFn hops_to_dest_;
    SuperframeDurationFn superframe_duration_;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
