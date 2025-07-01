/**
 * @file slot_allocation.cpp
 * @brief Implementation of slot allocation for TDMA scheduling
 */

#include "slot_allocation.hpp"

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

bool SlotAllocation::IsTxSlot() const {
    return type == SlotType::TX || type == SlotType::DISCOVERY_TX ||
           type == SlotType::CONTROL_TX || type == SlotType::SYNC_BEACON_TX;
}

bool SlotAllocation::IsRxSlot() const {
    return type == SlotType::RX || type == SlotType::DISCOVERY_RX ||
           type == SlotType::CONTROL_RX || type == SlotType::SYNC_BEACON_RX;
}

bool SlotAllocation::IsControlSlot() const {
    return type == SlotType::CONTROL_RX || type == SlotType::CONTROL_TX;
}

bool SlotAllocation::IsDiscoverySlot() const {
    return type == SlotType::DISCOVERY_RX || type == SlotType::DISCOVERY_TX;
}

bool SlotAllocation::IsSyncBeaconSlot() const {
    return type == SlotType::SYNC_BEACON_RX || type == SlotType::SYNC_BEACON_TX;
}

std::string SlotAllocation::GetTypeString() const {
    return slot_utils::SlotTypeToString(type);
}

Result SlotAllocation::Serialize(utils::ByteSerializer& serializer) const {
    serializer.WriteUint16(slot_number);
    serializer.WriteUint8(static_cast<uint8_t>(type));
    serializer.WriteUint16(target_address);

    return Result::Success();
}

std::optional<SlotAllocation> SlotAllocation::Deserialize(
    utils::ByteDeserializer& deserializer) {

    auto slot_number = deserializer.ReadUint16();
    auto type_raw = deserializer.ReadUint8();
    auto target_address = deserializer.ReadUint16();

    if (!slot_number || !type_raw || !target_address) {
        return std::nullopt;
    }

    auto type = static_cast<SlotType>(*type_raw);

    // Validate slot type
    if (!slot_utils::IsValidSlotType(type)) {
        return std::nullopt;
    }

    return SlotAllocation(*slot_number, type, *target_address);
}

bool SlotAllocation::operator==(const SlotAllocation& other) const {
    return slot_number == other.slot_number && type == other.type &&
           target_address == other.target_address;
}

bool SlotAllocation::operator!=(const SlotAllocation& other) const {
    return !(*this == other);
}

bool SlotAllocation::operator<(const SlotAllocation& other) const {
    return slot_number < other.slot_number;
}

namespace slot_utils {

std::string SlotTypeToString(SlotAllocation::SlotType type) {
    switch (type) {
        case SlotAllocation::SlotType::TX:
            return "TX";
        case SlotAllocation::SlotType::RX:
            return "RX";
        case SlotAllocation::SlotType::SLEEP:
            return "SLEEP";
        case SlotAllocation::SlotType::DISCOVERY_RX:
            return "DISCOVERY_RX";
        case SlotAllocation::SlotType::DISCOVERY_TX:
            return "DISCOVERY_TX";
        case SlotAllocation::SlotType::CONTROL_RX:
            return "CONTROL_RX";
        case SlotAllocation::SlotType::CONTROL_TX:
            return "CONTROL_TX";
        case SlotAllocation::SlotType::SYNC_BEACON_TX:
            return "SYNC_BEACON_TX";
        case SlotAllocation::SlotType::SYNC_BEACON_RX:
            return "SYNC_BEACON_RX";
        default:
            return "UNKNOWN";
    }
}

std::optional<SlotAllocation::SlotType> StringToSlotType(
    const std::string& type_str) {
    if (type_str == "TX")
        return SlotAllocation::SlotType::TX;
    if (type_str == "RX")
        return SlotAllocation::SlotType::RX;
    if (type_str == "SLEEP")
        return SlotAllocation::SlotType::SLEEP;
    if (type_str == "DISCOVERY_RX")
        return SlotAllocation::SlotType::DISCOVERY_RX;
    if (type_str == "DISCOVERY_TX")
        return SlotAllocation::SlotType::DISCOVERY_TX;
    if (type_str == "CONTROL_RX")
        return SlotAllocation::SlotType::CONTROL_RX;
    if (type_str == "CONTROL_TX")
        return SlotAllocation::SlotType::CONTROL_TX;
    if (type_str == "SYNC_BEACON_TX")
        return SlotAllocation::SlotType::SYNC_BEACON_TX;
    if (type_str == "SYNC_BEACON_RX")
        return SlotAllocation::SlotType::SYNC_BEACON_RX;

    return std::nullopt;
}

bool IsValidSlotType(SlotAllocation::SlotType type) {
    switch (type) {
        case SlotAllocation::SlotType::TX:
        case SlotAllocation::SlotType::RX:
        case SlotAllocation::SlotType::SLEEP:
        case SlotAllocation::SlotType::DISCOVERY_RX:
        case SlotAllocation::SlotType::DISCOVERY_TX:
        case SlotAllocation::SlotType::CONTROL_RX:
        case SlotAllocation::SlotType::CONTROL_TX:
        case SlotAllocation::SlotType::SYNC_BEACON_TX:
        case SlotAllocation::SlotType::SYNC_BEACON_RX:
            return true;
        default:
            return false;
    }
}

}  // namespace slot_utils

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher