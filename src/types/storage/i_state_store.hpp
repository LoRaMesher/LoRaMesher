/**
 * @file i_state_store.hpp
 * @brief Interface of a store that keeps protocol state across resets
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "types/error_codes/result.hpp"
#include "utils/compat/span.hpp"

namespace loramesher {
namespace storage {

/**
 * @brief Non-volatile slot holding one opaque state blob
 *
 * The protocol writes a snapshot with Save() before a planned reset (OTA
 * reboot, deep sleep) and reads it back once with Load() when it starts again.
 * Implementations decide where the blob lives: RTC memory, flash, a file, or
 * plain RAM in tests. A store keeps at most one blob; Save() replaces it.
 */
class IStateStore {
   public:
    virtual ~IStateStore() = default;

    /**
     * @brief Replace the stored blob
     *
     * @param data Blob to store
     * @return Success, or an error if the blob could not be stored (for
     *         example because it exceeds the store's capacity)
     */
    virtual Result Save(std::span<const uint8_t> data) = 0;

    /**
     * @brief Read the stored blob
     *
     * @return The blob, or std::nullopt when the store is empty
     */
    virtual std::optional<std::vector<uint8_t>> Load() = 0;

    /**
     * @brief Erase the stored blob
     */
    virtual void Clear() = 0;

    /**
     * @brief True if the store takes a Save() every superframe without wear
     *
     * Deep sleep saves the state before every sleep, so it needs such a
     * store (RAM or RTC memory, not flash).
     */
    virtual bool AllowsFrequentWrites() const { return true; }
};

}  // namespace storage
}  // namespace loramesher
