/**
 * @file superframe.cpp
 * @brief Implementation of superframe structure for LoRaMesh protocol
 */

#include "superframe.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>
#include "types/error_codes/loramesher_error_codes.hpp"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

Result Superframe::Validate() const {
    // Check that slot counts are reasonable
    if (total_slots == 0) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Total slots cannot be zero");
    }

    // Check that sum of specific slot types doesn't exceed total
    uint32_t allocated_slots = data_slots + discovery_slots + control_slots;
    if (allocated_slots > total_slots) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Sum of slot types exceeds total slots");
    }

    // Check slot duration is reasonable
    if (slot_duration_ms < 10 || slot_duration_ms > 60000) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Slot duration must be between 10ms and 60s");
    }

    return Result::Success();
}

uint32_t Superframe::GetSuperframeDuration() const {
    return total_slots * slot_duration_ms;
}

uint16_t Superframe::GetCurrentSlot(uint32_t current_time) const {
    if (current_time < superframe_start_time) {
        return 0;  // Before superframe started
    }

    uint32_t elapsed = current_time - superframe_start_time;
    uint32_t slot_index = (elapsed / slot_duration_ms) % total_slots;

    return static_cast<uint16_t>(slot_index);
}

uint32_t Superframe::GetSlotStartTime(uint16_t slot_number) const {
    if (slot_number >= total_slots) {
        slot_number = slot_number % total_slots;
    }

    return superframe_start_time + (slot_number * slot_duration_ms);
}

uint32_t Superframe::GetSlotEndTime(uint16_t slot_number) const {
    return GetSlotStartTime(slot_number) + slot_duration_ms;
}

bool Superframe::IsNewSuperframe(uint32_t current_time) const {
    if (current_time < superframe_start_time) {
        return false;
    }

    uint32_t elapsed = current_time - superframe_start_time;
    uint32_t superframe_duration = GetSuperframeDuration();

    // Check if we've completed at least one full superframe
    return elapsed >= superframe_duration;
}

void Superframe::AdvanceToNextSuperframe(uint32_t current_time) {
    uint32_t superframe_duration = GetSuperframeDuration();

    // Calculate how many complete superframes have passed
    uint32_t elapsed = current_time - superframe_start_time;
    uint32_t superframes_passed = elapsed / superframe_duration;

    // Advance to the start of the next superframe
    superframe_start_time += (superframes_passed + 1) * superframe_duration;
}

std::tuple<float, float, float> Superframe::GetSlotDistribution() const {
    if (total_slots == 0) {
        return std::tuple<float, float, float>(0.0f, 0.0f, 0.0f);
    }

    float data_pct = (static_cast<float>(data_slots) / total_slots) * 100.0f;
    float discovery_pct =
        (static_cast<float>(discovery_slots) / total_slots) * 100.0f;
    float control_pct =
        (static_cast<float>(control_slots) / total_slots) * 100.0f;

    return std::tuple<float, float, float>(data_pct, discovery_pct,
                                           control_pct);
}

Result Superframe::Serialize(utils::ByteSerializer& serializer) const {
    serializer.WriteUint16(total_slots);
    serializer.WriteUint16(data_slots);
    serializer.WriteUint16(discovery_slots);
    serializer.WriteUint16(control_slots);
    serializer.WriteUint32(slot_duration_ms);
    serializer.WriteUint32(superframe_start_time);

    return Result::Success();
}

std::optional<Superframe> Superframe::Deserialize(
    utils::ByteDeserializer& deserializer) {

    auto total_slots = deserializer.ReadUint16();
    auto data_slots = deserializer.ReadUint16();
    auto discovery_slots = deserializer.ReadUint16();
    auto control_slots = deserializer.ReadUint16();
    auto slot_duration_ms = deserializer.ReadUint32();
    auto superframe_start_time = deserializer.ReadUint32();

    if (!total_slots || !data_slots || !discovery_slots || !control_slots ||
        !slot_duration_ms || !superframe_start_time) {
        return std::nullopt;
    }

    Superframe superframe(*total_slots, *data_slots, *discovery_slots,
                          *control_slots, *slot_duration_ms,
                          *superframe_start_time);

    // Validate the deserialized superframe
    Result validation = superframe.Validate();
    if (!validation.IsSuccess()) {
        return std::nullopt;
    }

    return superframe;
}

bool Superframe::operator==(const Superframe& other) const {
    return total_slots == other.total_slots && data_slots == other.data_slots &&
           discovery_slots == other.discovery_slots &&
           control_slots == other.control_slots &&
           slot_duration_ms == other.slot_duration_ms &&
           superframe_start_time == other.superframe_start_time;
}

bool Superframe::operator!=(const Superframe& other) const {
    return !(*this == other);
}

namespace superframe_utils {

Superframe CreateDefaultSuperframe(uint16_t total_slots,
                                   uint32_t slot_duration_ms) {
    // Default distribution: 60% data, 20% discovery, 20% control
    uint16_t data_slots = static_cast<uint16_t>(total_slots * 0.6);
    uint16_t discovery_slots = static_cast<uint16_t>(total_slots * 0.2);
    uint16_t control_slots = total_slots - data_slots - discovery_slots;

    return Superframe(total_slots, data_slots, discovery_slots, control_slots,
                      slot_duration_ms, 0);
}

Superframe CreateOptimizedSuperframe(uint8_t node_count,
                                     uint32_t slot_duration_ms) {
    // Calculate optimal slot count based on network size
    uint16_t total_slots =
        std::max(50, std::min(200, static_cast<int>(node_count * 5)));

    // Adjust slot distribution based on network size
    float data_ratio, discovery_ratio, control_ratio;

    if (node_count <= 5) {
        // Small network: more discovery slots for dynamic joining
        data_ratio = 0.5f;
        discovery_ratio = 0.3f;
        control_ratio = 0.2f;
    } else if (node_count <= 20) {
        // Medium network: balanced approach
        data_ratio = 0.6f;
        discovery_ratio = 0.2f;
        control_ratio = 0.2f;
    } else {
        // Large network: more data slots, less discovery overhead
        data_ratio = 0.7f;
        discovery_ratio = 0.15f;
        control_ratio = 0.15f;
    }

    uint16_t data_slots = static_cast<uint16_t>(total_slots * data_ratio);
    uint16_t discovery_slots =
        static_cast<uint16_t>(total_slots * discovery_ratio);
    uint16_t control_slots = total_slots - data_slots - discovery_slots;

    return Superframe(total_slots, data_slots, discovery_slots, control_slots,
                      slot_duration_ms, 0);
}

std::string ValidateSlotDistribution(const Superframe& superframe) {
    Result validation = superframe.Validate();
    if (!validation.IsSuccess()) {
        return validation.GetErrorMessage();
    }

    // Additional validation checks
    auto [data_pct, discovery_pct, control_pct] =
        superframe.GetSlotDistribution();

    // Warn if slot distribution seems unbalanced
    if (data_pct < 30.0f) {
        return "Warning: Data slots are less than 30% of total";
    }

    if (discovery_pct < 10.0f) {
        return "Warning: Discovery slots are less than 10% of total";
    }

    if (control_pct < 10.0f) {
        return "Warning: Control slots are less than 10% of total";
    }

    return "";  // No issues found
}

uint32_t CalculateOptimalSlotDuration(uint16_t max_packet_size,
                                      uint32_t data_rate_bps,
                                      uint32_t guard_time_ms) {
    // Calculate transmission time for maximum packet
    // Add 8 bits for start/stop bits, parity, etc.
    uint32_t bits_per_packet = (max_packet_size * 8) + 64;  // 64 bits overhead

    // Calculate transmission time in milliseconds
    uint32_t tx_time_ms = (bits_per_packet * 1000) / data_rate_bps;

    // Add guard time and round up to nearest 10ms
    uint32_t total_time_ms = tx_time_ms + guard_time_ms;

    // Round up to nearest 10ms for clean timing
    return ((total_time_ms + 9) / 10) * 10;
}

}  // namespace superframe_utils

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher