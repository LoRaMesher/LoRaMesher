/**
 * @file security_config.hpp
 * @brief Security mode, codec configuration and counters of dropped frames
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "types/messages/base_header.hpp"

namespace loramesher {
namespace security {

/**
 * @brief How frames are protected
 *
 * There is no mixed mode. A secured node drops plain frames and a plain
 * node drops secured frames.
 */
enum class SecurityMode : uint8_t {
    kDisabled,  ///< 2.0.0 behaviour: plain frames, secured frames are rejected
    kSecured,   ///< Every frame secured, plain frames are dropped
};

/// Largest on-air frame at the default radio setting (SF9, 950 ms slot)
constexpr size_t kDefaultMaxPacketSize = 115;

/**
 * @brief Settings of one FrameCodec
 */
struct FrameCodecConfig {
    SecurityMode mode = SecurityMode::kDisabled;
    /// Address of this node
    AddressType node_address = 0;
    /// Largest on-air frame the radio setting allows, trailer included
    size_t max_packet_size = kDefaultMaxPacketSize;
    /// Peers with receive state, max_network_nodes + 8
    size_t max_peers = 58;
    /// Lifetime of a stored join nonce, the joining timeout of the protocol
    uint32_t join_nonce_lifetime_ms = 30000;
    /// Millisecond clock for the join nonce lifetime. Without one a stored
    /// nonce stays until a newer request replaces it
    std::function<uint32_t()> clock_ms;
};

/**
 * @brief Counters of sealed, opened and dropped frames
 *
 * A dropped received frame never surfaces as an error to the application.
 * It is counted here.
 */
struct SecurityStats {
    uint32_t tx_sealed = 0;
    uint32_t tx_refused = 0;  ///< Counter, key or size problem
    uint32_t rx_opened = 0;
    uint32_t rx_plain_dropped = 0;  ///< S = 0 in secured mode
    uint32_t rx_malformed = 0;      ///< Length rule, H flag, unknown type
    uint32_t rx_unknown_epoch = 0;
    uint32_t rx_auth_failed = 0;  ///< MIC or hop MIC
    uint32_t rx_tag_failed = 0;   ///< Body tag
    uint32_t rx_replay = 0;       ///< Includes flood duplicates
    uint32_t rx_e2e_required = 0;
    uint32_t rx_key_unavailable = 0;
    uint32_t rx_recovery_dropped = 0;
    uint32_t rx_key_forward_dropped = 0;
    uint32_t rx_join_unverified = 0;  ///< JOIN_RESPONSE for another node
    uint32_t counter_store_writes = 0;
    uint8_t tx_epoch = 0;
};

}  // namespace security
}  // namespace loramesher
