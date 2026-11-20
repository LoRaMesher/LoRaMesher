/**
 * @file i_key_provider.hpp
 * @brief Interface through which the application hands keys to the library
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "security/i_crypto_backend.hpp"
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"

namespace loramesher {
namespace security {

using Key128 = Aes128Key;

/// A network key and the epoch it belongs to
struct NetworkKey {
    uint8_t epoch = 0;
    Key128 key{};
};

/**
 * @brief Source of the network keys and the per-node keys
 *
 * The library holds no key storage policy. It asks this interface for keys,
 * derives the traffic keys from them and keeps those in RAM only.
 */
class IKeyProvider {
   public:
    virtual ~IKeyProvider() = default;

    /**
     * @brief Network keys the node holds, in any order, at most two
     *
     * @return Number of keys written (0, 1 or 2). 0 makes the node start as
     *         an orphan
     */
    virtual size_t GetNetworkKeys(std::array<NetworkKey, 2>& out) = 0;

    /**
     * @brief Per-node key shared with a peer
     *
     * A node returns its own node key when the peer is its gateway and
     * std::nullopt for any other peer. A gateway returns the node key of
     * the peer, or std::nullopt for an unknown or revoked address.
     */
    virtual std::optional<Key128> GetNodeKey(AddressType peer) = 0;

    /// Address of the gateway that holds this node's key, 0 on a gateway
    virtual AddressType GetGatewayAddress() = 0;

    /// Rekey and recovery: persist a newly received network key
    virtual Result StoreNetworkKey(const NetworkKey& /*next*/) {
        return Result(LoraMesherErrorCode::kNotImplemented, "rekey");
    }

    /// Rekey: the epoch is no longer accepted, erase its key
    virtual void RetireNetworkKey(uint8_t /*epoch*/) {}
};

}  // namespace security
}  // namespace loramesher
