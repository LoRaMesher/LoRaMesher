/**
 * @file i_superframe_service.hpp
 * @brief Interface for superframe management service
 */

#pragma once

#include "types/error_codes/result.hpp"
#include "types/protocols/lora_mesh/superframe.hpp"

namespace loramesher {

namespace protocols {
namespace lora_mesh {

/**
 * @brief Interface for superframe management service
 * 
 * Defines the interface for managing superframe timing and synchronization
 */
class ISuperframeService {
   public:
    /**
     * @brief Virtual destructor
     */
    virtual ~ISuperframeService() = default;

    /**
     * @brief Start the superframe
     * 
     * @return Result Success if started successfully
     */
    virtual Result StartSuperframe() = 0;

    /**
     * @brief Stop the superframe
     * 
     * @return Result Success if stopped successfully
     */
    virtual Result StopSuperframe() = 0;

    /**
     * @brief Handle transition to a new superframe
     * 
     * @return Result Success if handled successfully
     */
    virtual Result HandleNewSuperframe() = 0;

    /**
     * @brief Check if superframe is synchronized
     * 
     * @return bool True if synchronized
     */
    virtual bool IsSynchronized() const = 0;

    /**
     * @brief Get current superframe configuration
     * 
     * @return const Superframe& Current superframe configuration
     */
    virtual const types::protocols::lora_mesh::Superframe& GetSuperframeConfig()
        const = 0;

    /**
     * @brief Update superframe configuration
     * 
     * @param total_slots Total number of slots in superframe
     * @param data_slots Number of data slots
     * @param discovery_slots Number of discovery slots
     * @param control_slots Number of control slots
     * @param slot_duration_ms Duration of each slot in milliseconds
     * @return Result Success if updated successfully
     */
    virtual Result UpdateSuperframeConfig(uint16_t total_slots,
                                          uint16_t data_slots,
                                          uint16_t discovery_slots,
                                          uint16_t control_slots,
                                          uint32_t slot_duration_ms) = 0;
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher