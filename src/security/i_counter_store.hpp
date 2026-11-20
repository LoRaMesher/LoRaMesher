/**
 * @file i_counter_store.hpp
 * @brief Interface of the store that persists the frame counter record
 */

#pragma once

#include <cstdint>
#include <optional>

#include "security/counter_record_codec.hpp"
#include "types/error_codes/result.hpp"
#include "types/messages/base_header.hpp"

namespace loramesher {
namespace security {

/**
 * @brief Non-volatile slot holding the counter record
 *
 * FrameCounters saves a higher limit before it uses the counter values
 * below it, so the store must survive a reset and a power loss. The record
 * and the keys share one fate: when one is erased the node is provisioned
 * again.
 */
class ICounterStore {
   public:
    virtual ~ICounterStore() = default;

    /**
     * @brief Read the record
     *
     * @return The record, or std::nullopt when none exists, it is corrupt
     *         or it cannot be read. A store never invents a record
     */
    virtual std::optional<CounterRecord> Load() = 0;

    /**
     * @brief Replace the record
     *
     * @return Success only once the record is durable
     */
    virtual Result Save(const CounterRecord& record) = 0;

    /**
     * @brief Saved floor of the end-to-end receive window of a peer
     *
     * Optional. A store that does not keep floors returns std::nullopt.
     */
    virtual std::optional<uint32_t> LoadRxFloor(AddressType /*peer*/,
                                                uint8_t /*epoch*/) {
        return std::nullopt;
    }

    /**
     * @brief Save the floor of the end-to-end receive window of a peer
     *
     * Optional. A store may ignore the call.
     */
    virtual void SaveRxFloor(AddressType /*peer*/, uint8_t /*epoch*/,
                             uint32_t /*high*/) {}
};

}  // namespace security
}  // namespace loramesher
