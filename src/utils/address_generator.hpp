/**
 * @file address_generator.hpp
 * @brief Robust address generation utilities for mesh network device addressing
 */

#pragma once

#include <array>
#include <cstdint>
#include "types/messages/base_header.hpp"

namespace loramesher {
namespace utils {

/**
 * @brief Utility class for generating collision-resistant device addresses
 */
class AddressGenerator {
   public:
    /// Address generation configuration options
    struct Config {
        bool use_hardware_id;           ///< Use hardware unique ID if available
        bool avoid_reserved_addresses;  ///< Avoid 0x0000 and 0xFFFF
        uint16_t
            address_mask;  ///< Mask for address generation (default: full 16-bit)
        bool
            use_legacy_mac_formula;  ///< Use (mac[4]<<8)|mac[5] — original LoRaMesher formula
        bool restrict_to_unicast_range;  ///< Fold generated addresses into the
                                         ///< unicast range [0x0001, 0x7FFF]

        /// Default constructor with sensible defaults
        Config()
            : use_hardware_id(true),
              avoid_reserved_addresses(true),
              address_mask(0xFFFF),
              use_legacy_mac_formula(true),
              restrict_to_unicast_range(true) {}
    };

    /**
     * @brief Generate device address from raw hardware unique ID bytes
     *
     * @param hardware_id Pointer to hardware unique ID bytes (6 bytes expected)
     * @param id_length Length of hardware ID data
     * @param config Address generation configuration
     * @return AddressType Generated device address (0 if generation failed)
     */
    static AddressType GenerateFromHardwareId(const uint8_t* hardware_id,
                                              size_t id_length,
                                              const Config& config = Config{});

    /**
     * @brief Generate fallback address using enhanced randomization
     *
     * Uses system entropy and timing for better randomization than simple tick count.
     *
     * @param config Address generation configuration
     * @return AddressType Generated device address (never 0)
     */
    static AddressType GenerateFallback(const Config& config = Config{});

    /**
     * @brief Validate if an address is suitable for use
     *
     * @param address Address to validate
     * @param avoid_reserved Whether to reject reserved addresses (0x0000, 0xFFFF)
     * @param restrict_to_unicast Whether to reject group-range addresses
     *                            (0x8000-0xFFFE)
     * @return bool True if address is valid for use
     */
    static bool IsValidAddress(AddressType address, bool avoid_reserved = true,
                               bool restrict_to_unicast = false);

    /**
     * @brief Get information about the last address generation
     *
     * @return const char* Description of the address generation source
     */
    static const char* GetLastGenerationSource();

   private:
    /**
     * @brief Calculate CRC16 hash of data
     *
     * @param data Pointer to data buffer
     * @param length Length of data in bytes
     * @return uint16_t CRC16 hash value
     */
    static uint16_t CalculateCRC16(const uint8_t* data, size_t length);

    /**
     * @brief Calculate FNV-1a hash of data (16-bit)
     *
     * @param data Pointer to data buffer
     * @param length Length of data in bytes
     * @return uint16_t FNV-1a hash value
     */
    static uint16_t CalculateFNV1a(const uint8_t* data, size_t length);

    /**
     * @brief Fold an address into the unicast range [0x0001, 0x7FFF]
     *
     * Clears the group-range high bit and remaps the reserved zero address.
     *
     * @param address Address to fold
     * @return AddressType Address guaranteed to be in the unicast range
     */
    static AddressType FoldToUnicastRange(AddressType address);

    /// Last generation source description
    static const char* last_generation_source_;
};

}  // namespace utils
}  // namespace loramesher