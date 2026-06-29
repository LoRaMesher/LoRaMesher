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
#include <mutex>
#include <vector>

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
    /**
     * @param mutex Coordinator mutex guarding shared protocol state; locked by
     *              the public membership operations exactly as before.
     */
    explicit ReliableMessaging(std::mutex& mutex);

    /// Join a multicast group. @return Success or an error for invalid/full.
    Result JoinGroup(AddressType group);

    /// Leave a multicast group. @return Success (idempotent).
    Result LeaveGroup(AddressType group);

    /// @return true if this node is a member of @p group.
    bool IsMemberOfGroup(AddressType group) const;

    /// @return the list of groups this node currently belongs to.
    std::vector<AddressType> GetGroups() const;

   private:
    static constexpr size_t kMaxGroups = 8;
    std::array<AddressType, kMaxGroups> groups_{};
    uint8_t group_count_ = 0;

    std::mutex& mutex_;  ///< Coordinator mutex (not owned)
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
