/**
 * @file i_counter_store.hpp
 * @brief Interface of the store that persists a frame counter mark
 */

#pragma once

#include <cstdint>

#include "types/error_codes/result.hpp"

namespace loramesher {
namespace security {

/**
 * @brief Non-volatile slot holding one 32-bit high-water mark
 *
 * FrameCounter writes the mark before it uses the counter values below it,
 * so the store must survive a reset and a power loss. A Store() that returns
 * success must be durable.
 */
class ICounterStore {
   public:
    virtual ~ICounterStore() = default;

    /**
     * @brief Read the persisted mark
     *
     * @param mark Receives the mark. A store that was never written yields 0
     * @return Success, or an error when the store cannot be read. A read
     *         failure must be reported and never be turned into a mark of 0
     */
    virtual Result Load(uint32_t& mark) = 0;

    /**
     * @brief Persist a new mark
     *
     * @param mark Mark to write
     * @return Success once the mark is durable, or an error
     */
    virtual Result Store(uint32_t mark) = 0;
};

}  // namespace security
}  // namespace loramesher
