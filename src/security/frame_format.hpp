/**
 * @file frame_format.hpp
 * @brief Sizes and classes of secured frames
 *
 * Every size and airtime check of the protocol takes its numbers from this
 * file, so that the trailer of a secured frame is counted in one place.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "security/security_config.hpp"
#include "security/security_context.hpp"
#include "types/messages/base_header.hpp"
#include "types/messages/message_type.hpp"

namespace loramesher {
namespace security {

/**
 * @brief How a frame type is protected on the wire
 */
enum class FrameClass : uint8_t {
    kUnsupported,  ///< Not a type a secured node sends or accepts
    kBeacon,       ///< Class B: sync beacon with the 3-byte delay
    kControl,      ///< Class C1: source is the transmitter
    kJoin,         ///< Class C2: transmitter address in the trailer
    kData,         ///< Class D: body layer, hop MIC and header mask
    kRecovery,     ///< Class R: the payload protects itself
};

/// Size of the BaseHeader, in bytes
constexpr size_t kBaseHeaderSize = 6;
/// Largest payload_size value
constexpr size_t kMaxFramePayload = 255;
/// Largest plain frame: header and payload
constexpr size_t kMaxPlainFrameSize = kBaseHeaderSize + kMaxFramePayload;

/// Class B trailer: ctr(3) and mic(4)
constexpr size_t kBeaconTrailerSize = 3 + kMicSize;
/// Class C1 trailer: ctr(4) and mic(4)
constexpr size_t kControlTrailerSize = 4 + kMicSize;
/// Class C2 trailer: tx(2), ctr(4) and mic(4)
constexpr size_t kJoinTrailerSize = 2 + 4 + kMicSize;
/// Class D trailer: ctr(4), tag(4) and hmic(4)
constexpr size_t kDataTrailerSize = 4 + kMicSize + kMicSize;
/// Longest trailer of any class
constexpr size_t kMaxTrailerSize = kDataTrailerSize;
/// Largest buffer a sealed frame can need
constexpr size_t kMaxSealedFrameSize = kMaxPlainFrameSize + kMaxTrailerSize;

/// Payload of a sync beacon in memory, as in 2.0.0
constexpr size_t kBeaconPlainPayloadSize = 14;
/// Payload of a secured sync beacon on the wire, the delay takes 3 bytes
constexpr size_t kBeaconWirePayloadSize = 13;
/// Offset of the most significant delay byte in the plain beacon payload
constexpr size_t kBeaconDelayHighOffset = 11;
/// On-air size of a secured sync beacon. The subslot layout depends on it
constexpr size_t kSecuredSyncBeaconSize =
    kBaseHeaderSize + kBeaconWirePayloadSize + kBeaconTrailerSize;
static_assert(kSecuredSyncBeaconSize == 26,
              "A secured sync beacon above 26 bytes leaves its subslot");

/// Join fields of a JOIN_REQUEST payload
constexpr size_t kJoinRequestFieldsSize = 6;
/// Join nonce that follows the join fields on the first hop
constexpr size_t kJoinNonceSize = 4;
/// Join fields of a JOIN_RESPONSE payload
constexpr size_t kJoinResponseFieldsSize = 9;

/// Fixed fields of a ROUTE_TABLE payload
constexpr size_t kRoutingTableFieldsSize = 7;
/// One routing entry
constexpr size_t kRoutingEntrySize = 10;

/// Prefix of a reliable message: msg_seq(1) and send_timestamp(4)
constexpr size_t kReliablePrefixSize = 5;
/// DataHeader extension of DATA, DATA_BROADCAST, DATA_RELIABLE, ACK and
/// the key types: next_hop(2), ttl(1), seq_num(1)
constexpr size_t kDataExtensionSize = 4;
/// DataHeader extension of DATA_GROUP: next_hop(2), ttl(1), flags(1),
/// seq_num(1)
constexpr size_t kGroupExtensionSize = 5;
/// Leading extension bytes a relay rewrites: next_hop and ttl
constexpr size_t kMutableExtensionSize = 3;

/// E flag of the data counter field: the body is under a node key
constexpr uint32_t kEndToEndFlag = 0x80000000u;
/// H flag of the data counter field, reserved, must be 0
constexpr uint32_t kHopCounterFlag = 0x40000000u;
/// Bits of the data counter field that hold the counter
constexpr uint32_t kDataCounterMask = 0x3FFFFFFFu;

/**
 * @brief Class of a plain message type in secured mode
 */
constexpr FrameClass ClassOf(MessageType plain_type) {
    switch (plain_type) {
        case MessageType::SYNC_BEACON:
            return FrameClass::kBeacon;
        case MessageType::ROUTE_TABLE:
        case MessageType::NM_CLAIM:
        case MessageType::SLOT_REQUEST:
        case MessageType::SLOT_ALLOCATION:
        case MessageType::HELLO:
        case MessageType::SYNC:
        case MessageType::PING:
        case MessageType::PONG:
            return FrameClass::kControl;
        case MessageType::JOIN_REQUEST:
        case MessageType::JOIN_RESPONSE:
            return FrameClass::kJoin;
        case MessageType::DATA:
        case MessageType::DATA_BROADCAST:
        case MessageType::DATA_RELIABLE:
        case MessageType::DATA_GROUP:
        case MessageType::ACK:
        case MessageType::KEY_UPDATE:
        case MessageType::KEY_UPDATE_ACK:
        case MessageType::KEY_RELAY:
            return FrameClass::kData;
        case MessageType::KEY_REQUEST:
        case MessageType::KEY_GRANT:
            return FrameClass::kRecovery;
        default:
            return FrameClass::kUnsupported;
    }
}

/// True for KEY_UPDATE, KEY_UPDATE_ACK and KEY_RELAY
constexpr bool IsKeyDataType(MessageType plain_type) {
    return plain_type == MessageType::KEY_UPDATE ||
           plain_type == MessageType::KEY_UPDATE_ACK ||
           plain_type == MessageType::KEY_RELAY;
}

/// True for the flood types DATA_BROADCAST and DATA_GROUP
constexpr bool IsFloodType(MessageType plain_type) {
    return plain_type == MessageType::DATA_BROADCAST ||
           plain_type == MessageType::DATA_GROUP;
}

/**
 * @brief Trailer length of a secured frame of this type, in bytes
 *
 * @return 0 for a type without a trailer or an unsupported type
 */
constexpr size_t SecuredTrailerLen(MessageType plain_type) {
    switch (ClassOf(plain_type)) {
        case FrameClass::kBeacon:
            return kBeaconTrailerSize;
        case FrameClass::kControl:
            return kControlTrailerSize;
        case FrameClass::kJoin:
            return kJoinTrailerSize;
        case FrameClass::kData:
            return kDataTrailerSize;
        default:
            return 0;
    }
}

/**
 * @brief Trailer length of a frame of this type in the given mode
 *
 * @return 0 when security is disabled
 */
constexpr size_t TrailerLen(SecurityMode mode, MessageType plain_type) {
    return mode == SecurityMode::kSecured ? SecuredTrailerLen(plain_type) : 0;
}

/**
 * @brief Length of the DataHeader extension of a class D type
 *
 * @return 0 for a type outside class D
 */
constexpr size_t DataExtensionLen(MessageType plain_type) {
    if (ClassOf(plain_type) != FrameClass::kData) {
        return 0;
    }
    return plain_type == MessageType::DATA_GROUP ? kGroupExtensionSize
                                                 : kDataExtensionSize;
}

/**
 * @brief On-air length of a frame, from the size of its plain form
 *
 * In secured mode the trailer is added and the sync beacon loses one byte.
 * Every size and airtime check of the protocol uses this function.
 *
 * @param mode Security mode of the node
 * @param plain_type Message type
 * @param plain_total_size Header and payload as BaseMessage serializes them
 */
constexpr size_t OnAirSize(SecurityMode mode, MessageType plain_type,
                           size_t plain_total_size) {
    if (mode != SecurityMode::kSecured) {
        return plain_total_size;
    }
    const size_t with_trailer =
        plain_total_size + SecuredTrailerLen(plain_type);
    return plain_type == MessageType::SYNC_BEACON && with_trailer > 0
               ? with_trailer - 1
               : with_trailer;
}

/**
 * @brief Largest application payload of a send type
 *
 * max_packet_size - 6 - extension - reliable prefix - trailer.
 *
 * @param mode Security mode of the node
 * @param type DATA, DATA_BROADCAST, DATA_RELIABLE or DATA_GROUP
 * @param group_acks DATA_GROUP only: the reliable prefix is present
 * @param max_packet_size Largest on-air frame of the radio setting
 * @return Bytes, 0 for any other type or when nothing fits
 */
constexpr size_t MaxApplicationPayload(SecurityMode mode, MessageType type,
                                       bool group_acks,
                                       size_t max_packet_size) {
    size_t overhead = kBaseHeaderSize + TrailerLen(mode, type);
    switch (type) {
        case MessageType::DATA:
        case MessageType::DATA_BROADCAST:
            overhead += kDataExtensionSize;
            break;
        case MessageType::DATA_RELIABLE:
            overhead += kDataExtensionSize + kReliablePrefixSize;
            break;
        case MessageType::DATA_GROUP:
            overhead +=
                kGroupExtensionSize + (group_acks ? kReliablePrefixSize : 0);
            break;
        default:
            return 0;
    }
    // payload_size is one byte, so a frame never exceeds this length
    const size_t format_limit = kMaxPlainFrameSize + TrailerLen(mode, type);
    const size_t limit =
        max_packet_size < format_limit ? max_packet_size : format_limit;
    return limit > overhead ? limit - overhead : 0;
}

/**
 * @brief Bytes of a ROUTE_TABLE frame that are no routing entries
 *
 * 13 without security and 21 in secured mode.
 */
constexpr size_t RoutingTableOverhead(SecurityMode mode) {
    return kBaseHeaderSize + kRoutingTableFieldsSize +
           TrailerLen(mode, MessageType::ROUTE_TABLE);
}

/**
 * @brief Routing entries that fit one fragment by packet size alone
 *
 * The slot airtime check of the protocol may lower the result further.
 */
constexpr size_t MaxRoutingEntriesBySize(SecurityMode mode,
                                         size_t max_packet_size) {
    const size_t overhead = RoutingTableOverhead(mode);
    return max_packet_size > overhead
               ? (max_packet_size - overhead) / kRoutingEntrySize
               : 0;
}

/// On-air size of a secured JOIN_REQUEST on its first hop, with the nonce
constexpr size_t kSecuredJoinRequestFirstHopSize =
    kBaseHeaderSize + kJoinRequestFieldsSize + kJoinNonceSize +
    kJoinTrailerSize;
/// On-air size of a secured JOIN_REQUEST that a member forwards
constexpr size_t kSecuredJoinRequestForwardedSize =
    kBaseHeaderSize + kJoinRequestFieldsSize + kJoinTrailerSize;
/// On-air size of a secured JOIN_RESPONSE
constexpr size_t kSecuredJoinResponseSize =
    kBaseHeaderSize + kJoinResponseFieldsSize + kJoinTrailerSize;

static_assert(kSecuredJoinRequestFirstHopSize == 26, "S10.1");
static_assert(kSecuredJoinRequestForwardedSize == 22, "S10.1");
static_assert(kSecuredJoinResponseSize == 25, "S10.1");

}  // namespace security
}  // namespace loramesher
