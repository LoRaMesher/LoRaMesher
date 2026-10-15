/**
 * @file protocol_state.hpp
 * @brief Protocol state enumeration for the LoRa mesh state machine
 */

#pragma once

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Protocol state enumeration
 */
enum class ProtocolState {
    INITIALIZING,      ///< Protocol is initializing
    DISCOVERY,         ///< Looking for existing network
    JOINING,           ///< Attempting to join network
    NORMAL_OPERATION,  ///< Normal network operation
    NETWORK_MANAGER,   ///< Acting as network manager
    FAULT_RECOVERY,    ///< Attempting to recover from fault
    NM_ELECTION        ///< Running for NM role after fault recovery backoff
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher
