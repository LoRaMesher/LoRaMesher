/**
 * @file superframe.hpp
 * @brief Definition of superframe structure for LoRaMesh protocol
 */

#pragma once

#include <cstdint>
#include <vector>
#include "types/error_codes/result.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Structure representing a superframe in the TDMA schedule
 * 
 * A superframe contains a fixed number of slots that repeat cyclically.
 * Different types of slots are allocated for different purposes.
 */
struct Superframe {
    uint16_t total_slots = 0;  ///< Total number of slots in the superframe
    uint16_t data_slots = 0;   ///< Number of data transmission/reception slots
    uint16_t discovery_slots =
        0;  ///< Number of discovery slots for network joining
    uint16_t control_slots = 0;     ///< Number of control slots for management
    uint32_t slot_duration_ms = 0;  ///< Duration of each slot in milliseconds
    uint32_t superframe_start_time =
        0;  ///< Start time of current superframe cycle

    /**
     * @brief Default constructor with common default values
     */
    Superframe()
        : total_slots(100),
          data_slots(60),
          discovery_slots(20),
          control_slots(20),
          slot_duration_ms(1000),
          superframe_start_time(0) {}

    /**
     * @brief Constructor with all parameters
     * 
     * @param total Total number of slots
     * @param data Number of data slots
     * @param discovery Number of discovery slots
     * @param control Number of control slots
     * @param duration_ms Duration of each slot in milliseconds
     * @param start_time Start time of the superframe
     */
    Superframe(uint16_t total, uint16_t data, uint16_t discovery,
               uint16_t control, uint32_t duration_ms, uint32_t start_time = 0)
        : total_slots(total),
          data_slots(data),
          discovery_slots(discovery),
          control_slots(control),
          slot_duration_ms(duration_ms),
          superframe_start_time(start_time) {}

    /**
     * @brief Validate the superframe configuration
     * 
     * Checks that the sum of slot types doesn't exceed total slots,
     * and that all parameters are within reasonable bounds.
     * 
     * @return Result Success if valid, error with details if invalid
     */
    Result Validate() const;

    /**
     * @brief Calculate the total duration of the superframe
     * 
     * @return uint32_t Total superframe duration in milliseconds
     */
    uint32_t GetSuperframeDuration() const;

    /**
     * @brief Get the current slot number based on elapsed time
     * 
     * @param current_time Current time in milliseconds
     * @return uint16_t Current slot number (0 to total_slots-1)
     */
    uint16_t GetCurrentSlot(uint32_t current_time) const;

    /**
     * @brief Get the start time of a specific slot
     * 
     * @param slot_number Slot number (0 to total_slots-1)
     * @return uint32_t Start time of the slot in milliseconds
     */
    uint32_t GetSlotStartTime(uint16_t slot_number) const;

    /**
     * @brief Get the end time of a specific slot
     * 
     * @param slot_number Slot number (0 to total_slots-1)
     * @return uint32_t End time of the slot in milliseconds
     */
    uint32_t GetSlotEndTime(uint16_t slot_number) const;

    /**
     * @brief Check if we're in a new superframe cycle
     * 
     * @param current_time Current time in milliseconds
     * @return bool True if a new superframe has started
     */
    bool IsNewSuperframe(uint32_t current_time) const;

    /**
     * @brief Update the superframe start time to the next cycle
     * 
     * @param current_time Current time in milliseconds
     */
    void AdvanceToNextSuperframe(uint32_t current_time);

    /**
     * @brief Calculate slot distribution percentages
     * 
     * @return std::tuple<float, float, float> Data, Discovery, Control percentages
     */
    std::tuple<float, float, float> GetSlotDistribution() const;

    /**
     * @brief Serialize the superframe configuration
     * 
     * @param serializer The serializer to write to
     * @return Result Success if serialization succeeded
     */
    Result Serialize(utils::ByteSerializer& serializer) const;

    /**
     * @brief Deserialize a superframe configuration
     * 
     * @param deserializer The deserializer to read from
     * @return std::optional<Superframe> The superframe if successful
     */
    static std::optional<Superframe> Deserialize(
        utils::ByteDeserializer& deserializer);

    /**
     * @brief Size of a superframe when serialized
     * 
     * @return size_t Size in bytes
     */
    static constexpr size_t SerializedSize() {
        return sizeof(uint16_t) +  // Total slots
               sizeof(uint16_t) +  // Data slots
               sizeof(uint16_t) +  // Discovery slots
               sizeof(uint16_t) +  // Control slots
               sizeof(uint32_t) +  // Slot duration
               sizeof(uint32_t);   // Superframe start time
    }

    /**
     * @brief Equality operator
     * 
     * @param other The other superframe to compare
     * @return bool True if superframes are equal
     */
    bool operator==(const Superframe& other) const;

    /**
     * @brief Inequality operator
     * 
     * @param other The other superframe to compare
     * @return bool True if superframes are not equal
     */
    bool operator!=(const Superframe& other) const;
};

/**
 * @brief Helper functions for superframe operations
 */
namespace superframe_utils {

/**
 * @brief Create a default superframe configuration
 * 
 * @param total_slots Total number of slots
 * @param slot_duration_ms Duration of each slot
 * @return Superframe Default superframe with proportional slot allocation
 */
Superframe CreateDefaultSuperframe(uint16_t total_slots = 100,
                                   uint32_t slot_duration_ms = 1000);

/**
 * @brief Create a superframe optimized for a specific number of nodes
 * 
 * @param node_count Number of nodes in the network
 * @param slot_duration_ms Duration of each slot
 * @return Superframe Optimized superframe configuration
 */
Superframe CreateOptimizedSuperframe(uint8_t node_count,
                                     uint32_t slot_duration_ms = 1000);

/**
 * @brief Validate superframe slot distribution
 * 
 * @param superframe The superframe to validate
 * @return std::string Error message if invalid, empty string if valid
 */
std::string ValidateSlotDistribution(const Superframe& superframe);

/**
 * @brief Calculate optimal slot duration based on packet size and data rate
 * 
 * @param max_packet_size Maximum packet size in bytes
 * @param data_rate_bps Data rate in bits per second
 * @param guard_time_ms Guard time between slots in milliseconds
 * @return uint32_t Optimal slot duration in milliseconds
 */
uint32_t CalculateOptimalSlotDuration(uint16_t max_packet_size,
                                      uint32_t data_rate_bps,
                                      uint32_t guard_time_ms = 100);

}  // namespace superframe_utils

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher