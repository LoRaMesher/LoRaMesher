/**
 * @file address_generator.cpp
 * @brief Robust address generation utilities implementation
 */

#include "address_generator.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include "utils/logger.hpp"

namespace loramesher {
namespace utils {

// Static member initialization
const char* AddressGenerator::last_generation_source_ = "Unknown";

AddressType AddressGenerator::GenerateFromHardwareId(const uint8_t* hardware_id,
                                                     size_t id_length,
                                                     const Config& config) {
    if (!hardware_id || id_length == 0) {
        LOG_WARNING("Invalid hardware ID, using fallback");
        last_generation_source_ = "Fallback (invalid hardware ID)";
        return GenerateFallback(config);
    }

    if (!config.use_hardware_id) {
        LOG_DEBUG("Hardware ID disabled, using fallback generation");
        return GenerateFallback(config);
    }

    last_generation_source_ = "Hardware HAL";
    LOG_INFO("Generating address from %s", last_generation_source_);

    // Use both CRC16 and FNV1a to increase entropy and reduce collisions
    uint16_t crc_hash = CalculateCRC16(hardware_id, id_length);
    uint16_t fnv_hash = CalculateFNV1a(hardware_id, id_length);

    // Combine hashes using XOR to distribute bits more evenly
    AddressType address =
        static_cast<AddressType>((crc_hash ^ fnv_hash) & config.address_mask);

    LOG_DEBUG(
        "Generated address 0x%04X from unique ID (CRC16: 0x%04X, FNV1a: "
        "0x%04X)",
        address, crc_hash, fnv_hash);

    // Handle reserved addresses
    if (config.avoid_reserved_addresses) {
        if (address == 0x0000) {
            address = 0x0001;  // Use first valid address
            LOG_DEBUG("Avoided reserved address 0x0000, using 0x0001");
        } else if (address == 0xFFFF) {
            address = 0xFFFE;  // Use last valid address
            LOG_DEBUG("Avoided reserved address 0xFFFF, using 0xFFFE");
        }
    }

    if (!IsValidAddress(address, config.avoid_reserved_addresses)) {
        LOG_WARNING("Generated invalid address 0x%04X, using fallback",
                    address);
        return GenerateFallback(config);
    }

    return address;
}

AddressType AddressGenerator::GenerateFallback(const Config& config) {
    last_generation_source_ = "Enhanced Fallback Generation";

    // Use multiple entropy sources for better randomization
    std::random_device rd;
    auto time_point = std::chrono::high_resolution_clock::now();
    auto time_since_epoch = time_point.time_since_epoch();
    auto time_count = time_since_epoch.count();

    // Combine random device with time count for better entropy
    std::mt19937 gen(rd() ^ static_cast<uint32_t>(time_count));
    std::uniform_int_distribution<uint16_t> dis(
        1, 0xFFFE);  // Avoid reserved addresses by default

    AddressType address;
    int attempts = 0;
    const int max_attempts = 10;

    do {
        address = static_cast<AddressType>(dis(gen) & config.address_mask);
        attempts++;
    } while (!IsValidAddress(address, config.avoid_reserved_addresses) &&
             attempts < max_attempts);

    if (attempts >= max_attempts) {
        // Last resort: use a simple valid address
        address = 0x0001;
        LOG_WARNING("Fallback generation exceeded max attempts, using 0x0001");
    }

    LOG_INFO("Generated fallback address 0x%04X (attempts: %d)", address,
             attempts);
    return address;
}

bool AddressGenerator::IsValidAddress(AddressType address,
                                      bool avoid_reserved) {
    if (avoid_reserved) {
        // Check for reserved addresses
        if (address == 0x0000 || address == 0xFFFF) {
            return false;
        }
    }

    // Additional validation could be added here
    // (e.g., check against known problematic address ranges)

    return true;
}

const char* AddressGenerator::GetLastGenerationSource() {
    return last_generation_source_;
}

uint16_t AddressGenerator::CalculateCRC16(const uint8_t* data, size_t length) {
    // CRC-16-CCITT polynomial: 0x1021
    const uint16_t polynomial = 0x1021;
    uint16_t crc = 0xFFFF;  // Initial value

    for (size_t i = 0; i < length; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;

        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ polynomial;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

uint16_t AddressGenerator::CalculateFNV1a(const uint8_t* data, size_t length) {
    // FNV-1a constants for 16-bit
    const uint16_t fnv_prime = 0x0193;
    const uint16_t fnv_offset_basis = 0x2B4C;

    uint16_t hash = fnv_offset_basis;

    for (size_t i = 0; i < length; i++) {
        hash ^= static_cast<uint16_t>(data[i]);
        hash *= fnv_prime;
    }

    return hash;
}

}  // namespace utils
}  // namespace loramesher