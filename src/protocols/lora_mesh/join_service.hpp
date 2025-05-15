/**
 * @file join_service.hpp
 * @brief Interface for network join functionality
 */

#pragma once

#include "types/error_codes/result.hpp"
#include "types/messages/base_message.hpp"
#include "types/messages/loramesher/join_response_header.hpp"

namespace loramesher {
namespace protocols {

/**
 * @brief Interface for join service
 * 
 * Handles joining existing networks and processing join requests
 */
class IJoinService {
   public:
    virtual ~IJoinService() = default;

    /**
     * @brief Attempt to join an existing network
     * 
     * @param manager_address Address of the network manager
     * @param requested_slots Number of slots requested
     * @return Result Success if join was successful, error details otherwise
     */
    virtual Result JoinNetwork(AddressType manager_address,
                               uint8_t requested_slots) = 0;

    /**
     * @brief Process a join request from another node
     * 
     * @param message The join request message
     * @return Result Success if processed correctly, error details otherwise
     */
    virtual Result ProcessJoinRequest(const BaseMessage& message) = 0;

    /**
     * @brief Process a join response from the network manager
     * 
     * @param message The join response message
     * @return Result Success if processed correctly, error details otherwise
     */
    virtual Result ProcessJoinResponse(const BaseMessage& message) = 0;

    /**
     * @brief Send a join request to the network
     * 
     * @param manager_address Address of the network manager
     * @param requested_slots Number of slots requested
     * @return Result Success if request sent, error details otherwise
     */
    virtual Result SendJoinRequest(AddressType manager_address,
                                   uint8_t requested_slots) = 0;

    /**
     * @brief Send a join response to a node
     * 
     * @param dest Address of the node that requested to join
     * @param status Response status (accepted/rejected)
     * @param allocated_slots Number of slots allocated (if accepted)
     * @return Result Success if response sent, error details otherwise
     */
    virtual Result SendJoinResponse(AddressType dest,
                                    JoinResponseHeader::ResponseStatus status,
                                    uint8_t allocated_slots) = 0;

    /**
     * @brief Check if a node should be allowed to join
     * 
     * @param node_address Address of the requesting node
     * @param capabilities Node capabilities
     * @param requested_slots Number of slots requested
     * @return std::pair<bool, uint8_t> First element is true if accepted, 
     *         second element is the number of allocated slots
     */
    virtual std::pair<bool, uint8_t> ShouldAcceptJoin(
        AddressType node_address, uint8_t capabilities,
        uint8_t requested_slots) = 0;
};

}  // namespace protocols
}  // namespace loramesher