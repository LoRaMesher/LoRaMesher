/**
 * @file snapshot_codec.cpp
 * @brief Binary encoding of a NetworkSnapshot
 */

#include "types/storage/snapshot_codec.hpp"

#include <bitset>
#include <set>

#include "utils/byte_operations.h"

namespace loramesher {
namespace storage {

namespace {

constexpr uint8_t kFlagWasNetworkManager = 0x01;
constexpr uint8_t kNoControlSlot = 0xFF;

bool IsUnicast(AddressType address) {
    return address != 0 && address != kBroadcastAddress;
}

}  // namespace

size_t SnapshotCodec::EncodedSize(const NetworkSnapshot& snapshot) {
    return kHeaderSize + snapshot.reservations.size() * kReservationSize +
           kCrcSize;
}

bool SnapshotCodec::IsValid(const NetworkSnapshot& snapshot) {
    if (!IsUnicast(snapshot.node_address)) {
        return false;
    }
    if (snapshot.reservations.size() > kMaxReservations) {
        return false;
    }
    if (!snapshot.was_network_manager && !snapshot.reservations.empty()) {
        return false;
    }

    std::bitset<256> used_indices;
    std::set<AddressType> used_addresses;
    for (const auto& reservation : snapshot.reservations) {
        if (!IsUnicast(reservation.address) ||
            reservation.address == snapshot.node_address) {
            return false;
        }
        if (reservation.control_slot_index == 0 ||
            reservation.control_slot_index == kNoControlSlot) {
            return false;
        }
        if (used_indices.test(reservation.control_slot_index) ||
            !used_addresses.insert(reservation.address).second) {
            return false;
        }
        used_indices.set(reservation.control_slot_index);
    }
    return true;
}

std::optional<std::vector<uint8_t>> SnapshotCodec::Encode(
    const NetworkSnapshot& snapshot) {
    if (!IsValid(snapshot)) {
        return std::nullopt;
    }

    std::vector<uint8_t> blob(EncodedSize(snapshot));
    utils::ByteSerializer serializer(blob);
    serializer.WriteUint32(kMagic);
    serializer.WriteUint8(kFormatVersion);
    serializer.WriteUint8(snapshot.was_network_manager ? kFlagWasNetworkManager
                                                       : 0);
    serializer.WriteUint16(snapshot.node_address);
    serializer.WriteUint16(snapshot.network_id);
    serializer.WriteUint8(snapshot.last_sequence);
    serializer.WriteUint8(snapshot.network_depth);
    serializer.WriteUint32(snapshot.superframe_duration_ms);
    serializer.WriteUint8(static_cast<uint8_t>(snapshot.reservations.size()));
    for (const auto& reservation : snapshot.reservations) {
        serializer.WriteUint16(reservation.address);
        serializer.WriteUint8(reservation.control_slot_index);
    }

    const size_t payload_size = serializer.getOffset();
    serializer.WriteUint32(
        Crc32(std::span<const uint8_t>(blob.data(), payload_size)));
    return blob;
}

std::optional<NetworkSnapshot> SnapshotCodec::Decode(
    std::span<const uint8_t> data) {
    if (data.size() < kHeaderSize + kCrcSize) {
        return std::nullopt;
    }

    utils::ByteDeserializer deserializer(data);
    if (deserializer.ReadUint32() != kMagic ||
        deserializer.ReadUint8() != kFormatVersion) {
        return std::nullopt;
    }

    NetworkSnapshot snapshot;
    const uint8_t flags = *deserializer.ReadUint8();
    if ((flags & ~kFlagWasNetworkManager) != 0) {
        return std::nullopt;
    }
    snapshot.was_network_manager = (flags & kFlagWasNetworkManager) != 0;
    snapshot.node_address = *deserializer.ReadUint16();
    snapshot.network_id = *deserializer.ReadUint16();
    snapshot.last_sequence = *deserializer.ReadUint8();
    snapshot.network_depth = *deserializer.ReadUint8();
    snapshot.superframe_duration_ms = *deserializer.ReadUint32();
    const uint8_t reservation_count = *deserializer.ReadUint8();

    // The blob must hold exactly the announced reservations and the CRC
    const size_t payload_size =
        kHeaderSize + reservation_count * kReservationSize;
    if (data.size() != payload_size + kCrcSize) {
        return std::nullopt;
    }

    snapshot.reservations.reserve(reservation_count);
    for (uint8_t i = 0; i < reservation_count; ++i) {
        ControlSlotReservation reservation;
        reservation.address = *deserializer.ReadUint16();
        reservation.control_slot_index = *deserializer.ReadUint8();
        snapshot.reservations.push_back(reservation);
    }

    const uint32_t stored_crc = *deserializer.ReadUint32();
    if (stored_crc != Crc32(data.first(payload_size))) {
        return std::nullopt;
    }
    if (!IsValid(snapshot)) {
        return std::nullopt;
    }
    return snapshot;
}

uint32_t SnapshotCodec::Crc32(std::span<const uint8_t> data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

}  // namespace storage
}  // namespace loramesher
