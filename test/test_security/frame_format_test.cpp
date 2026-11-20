/**
 * @file frame_format_test.cpp
 * @brief Type marking, trailer lengths and the size tables of the wire spec
 */

#include <gtest/gtest.h>

#include "frame_codec_test_support.hpp"
#include "types/messages/base_message.hpp"

namespace loramesher {
namespace security {
namespace codec_test {
namespace {

constexpr SecurityMode kOn = SecurityMode::kSecured;
constexpr SecurityMode kOff = SecurityMode::kDisabled;

const MessageType kEveryType[] = {
    MessageType::DATA,          MessageType::DATA_BROADCAST,
    MessageType::DATA_RELIABLE, MessageType::DATA_GROUP,
    MessageType::ACK,           MessageType::PING,
    MessageType::PONG,          MessageType::HELLO,
    MessageType::ROUTE_TABLE,   MessageType::SYNC,
    MessageType::JOIN_REQUEST,  MessageType::JOIN_RESPONSE,
    MessageType::SLOT_REQUEST,  MessageType::SLOT_ALLOCATION,
    MessageType::SYNC_BEACON,   MessageType::NM_CLAIM,
    MessageType::KEY_UPDATE,    MessageType::KEY_UPDATE_ACK,
    MessageType::KEY_REQUEST,   MessageType::KEY_GRANT,
    MessageType::KEY_RELAY,
};

TEST(FrameFormat, WireTypeCarriesTheSecuredBitAndTheEpochParity) {
    EXPECT_EQ(message_type::ToWireType(MessageType::SYNC_BEACON, 1), 0xCE);
    EXPECT_EQ(message_type::ToWireType(MessageType::SYNC_BEACON, 0), 0xC6);
    EXPECT_EQ(message_type::ToWireType(MessageType::SYNC_BEACON, 2), 0xC6);
    EXPECT_EQ(message_type::ToWireType(MessageType::DATA, 1), 0x99);
    EXPECT_EQ(message_type::ToWireType(MessageType::KEY_UPDATE, 1), 0xD9);
    EXPECT_EQ(message_type::ToWireType(MessageType::KEY_REQUEST, 0), 0xD3);
    for (const MessageType type : kEveryType) {
        for (uint8_t epoch = 0; epoch < 4; ++epoch) {
            const uint8_t wire = message_type::ToWireType(type, epoch);
            EXPECT_TRUE(message_type::IsSecuredWireType(wire));
            EXPECT_EQ(message_type::ToPlainType(wire), type);
            EXPECT_EQ(message_type::GetEpochParity(wire), epoch & 1);
            // A node without security rejects the secured type byte
            EXPECT_FALSE(
                message_type::IsValidType(static_cast<MessageType>(wire)));
        }
        EXPECT_TRUE(message_type::IsValidType(type));
        EXPECT_FALSE(
            message_type::IsSecuredWireType(static_cast<uint8_t>(type)));
    }
}

TEST(FrameFormat, SubtypesAreLimitedToOneToSeven) {
    for (uint8_t category = 1; category <= 5; ++category) {
        for (uint8_t subtype = 0; subtype < 16; ++subtype) {
            const auto type =
                static_cast<MessageType>((category << 4) | subtype);
            EXPECT_EQ(message_type::IsValidType(type),
                      subtype >= 1 && subtype <= 7);
        }
    }
    EXPECT_FALSE(message_type::IsValidType(static_cast<MessageType>(0x61)));
    EXPECT_FALSE(message_type::IsValidType(static_cast<MessageType>(0x01)));
    EXPECT_TRUE(message_type::IsKeyMessage(MessageType::KEY_RELAY));
}

TEST(FrameFormat, TrailerLengthByClass) {
    EXPECT_EQ(SecuredTrailerLen(MessageType::SYNC_BEACON), 7u);
    for (const MessageType type :
         {MessageType::ROUTE_TABLE, MessageType::NM_CLAIM,
          MessageType::SLOT_REQUEST, MessageType::SLOT_ALLOCATION,
          MessageType::HELLO, MessageType::SYNC, MessageType::PING,
          MessageType::PONG}) {
        EXPECT_EQ(ClassOf(type), FrameClass::kControl);
        EXPECT_EQ(SecuredTrailerLen(type), 8u);
    }
    for (const MessageType type :
         {MessageType::JOIN_REQUEST, MessageType::JOIN_RESPONSE}) {
        EXPECT_EQ(ClassOf(type), FrameClass::kJoin);
        EXPECT_EQ(SecuredTrailerLen(type), 10u);
    }
    for (const MessageType type :
         {MessageType::DATA, MessageType::DATA_BROADCAST,
          MessageType::DATA_RELIABLE, MessageType::DATA_GROUP, MessageType::ACK,
          MessageType::KEY_UPDATE, MessageType::KEY_UPDATE_ACK,
          MessageType::KEY_RELAY}) {
        EXPECT_EQ(ClassOf(type), FrameClass::kData);
        EXPECT_EQ(SecuredTrailerLen(type), 12u);
    }
    for (const MessageType type :
         {MessageType::KEY_REQUEST, MessageType::KEY_GRANT}) {
        EXPECT_EQ(ClassOf(type), FrameClass::kRecovery);
        EXPECT_EQ(SecuredTrailerLen(type), 0u);
    }
    EXPECT_EQ(ClassOf(MessageType::ANY), FrameClass::kUnsupported);
    EXPECT_EQ(ClassOf(static_cast<MessageType>(0x22)),
              FrameClass::kUnsupported);
    for (const MessageType type : kEveryType) {
        EXPECT_EQ(TrailerLen(kOff, type), 0u);
        EXPECT_EQ(TrailerLen(kOn, type), SecuredTrailerLen(type));
    }
    EXPECT_EQ(DataExtensionLen(MessageType::DATA), 4u);
    EXPECT_EQ(DataExtensionLen(MessageType::DATA_GROUP), 5u);
    EXPECT_EQ(DataExtensionLen(MessageType::KEY_RELAY), 4u);
    EXPECT_EQ(DataExtensionLen(MessageType::ROUTE_TABLE), 0u);
}

// S10.1: bytes today and bytes secured
TEST(FrameFormat, OnAirSizeMatchesThePerTypeTable) {
    struct Row {
        MessageType type;
        size_t today;
        size_t secured;
    };

    const Row rows[] = {
        {MessageType::SYNC_BEACON, 20, 26},
        {MessageType::ROUTE_TABLE, 13, 21},
        {MessageType::ROUTE_TABLE, 13 + 10 * 8, 101},
        {MessageType::JOIN_REQUEST, 12 + 4, 26},  // first hop, with nonce
        {MessageType::JOIN_REQUEST, 12, 22},      // forwarded
        {MessageType::JOIN_RESPONSE, 15, 25},
        {MessageType::NM_CLAIM, 10, 18},
        {MessageType::SLOT_REQUEST, 7, 15},
        {MessageType::SLOT_ALLOCATION, 10, 18},
        {MessageType::DATA, 10, 22},
        {MessageType::DATA_BROADCAST, 10 + 93, 115},
        {MessageType::DATA_RELIABLE, 15, 27},
        {MessageType::DATA_GROUP, 11, 23},
        {MessageType::ACK, 16, 28},
        {MessageType::KEY_UPDATE, 10 + 26, 48},
        {MessageType::KEY_UPDATE_ACK, 10 + 2, 24},
        {MessageType::KEY_RELAY, 10 + 16, 38},
        {MessageType::KEY_RELAY, 10 + 28, 50},
        {MessageType::KEY_REQUEST, 19, 19},
        {MessageType::KEY_GRANT, 31, 31},
    };
    for (const Row& row : rows) {
        EXPECT_EQ(OnAirSize(kOn, row.type, row.today), row.secured)
            << static_cast<int>(row.type);
        EXPECT_EQ(OnAirSize(kOff, row.type, row.today), row.today);
    }
    EXPECT_EQ(kSecuredSyncBeaconSize, 26u);
}

// S10.2: maximum application payload
TEST(FrameFormat, MaximumPayloadMatchesTheTable) {
    struct Row {
        MessageType type;
        bool group_acks;
        size_t today_115;
        size_t secured_115;
        size_t secured_242;
        size_t secured_51;
    };

    const Row rows[] = {
        {MessageType::DATA, false, 105, 93, 220, 29},
        {MessageType::DATA_BROADCAST, false, 105, 93, 220, 29},
        {MessageType::DATA_RELIABLE, false, 100, 88, 215, 24},
        {MessageType::DATA_GROUP, false, 104, 92, 219, 28},
        {MessageType::DATA_GROUP, true, 99, 87, 214, 23},
    };
    for (const Row& row : rows) {
        EXPECT_EQ(MaxApplicationPayload(kOff, row.type, row.group_acks, 115),
                  row.today_115);
        EXPECT_EQ(MaxApplicationPayload(kOn, row.type, row.group_acks, 115),
                  row.secured_115);
        EXPECT_EQ(MaxApplicationPayload(kOn, row.type, row.group_acks, 242),
                  row.secured_242);
        EXPECT_EQ(MaxApplicationPayload(kOn, row.type, row.group_acks, 51),
                  row.secured_51);
    }
    // payload_size is one byte: no radio setting lifts the format limit
    EXPECT_EQ(MaxApplicationPayload(kOn, MessageType::DATA, false, 1000),
              BaseMessage::kMaxPayloadSize - 4);
    EXPECT_EQ(MaxApplicationPayload(kOff, MessageType::DATA, false, 1000),
              BaseMessage::kMaxPayloadSize - 4);
    // Nothing fits, and types that carry no application payload
    EXPECT_EQ(MaxApplicationPayload(kOn, MessageType::DATA, false, 20), 0u);
    EXPECT_EQ(MaxApplicationPayload(kOn, MessageType::ACK, false, 115), 0u);
    EXPECT_EQ(MaxApplicationPayload(kOn, MessageType::ROUTE_TABLE, false, 115),
              0u);
}

// S10.3: routing entries per fragment, by packet size
TEST(FrameFormat, RoutingEntriesPerFragment) {
    EXPECT_EQ(RoutingTableOverhead(kOff), 13u);
    EXPECT_EQ(RoutingTableOverhead(kOn), 21u);
    EXPECT_EQ(MaxRoutingEntriesBySize(kOff, 115), 10u);
    EXPECT_EQ(MaxRoutingEntriesBySize(kOn, 115), 9u);
    EXPECT_EQ(MaxRoutingEntriesBySize(kOff, 51), 3u);
    EXPECT_EQ(MaxRoutingEntriesBySize(kOn, 51), 3u);
    EXPECT_EQ(MaxRoutingEntriesBySize(kOff, 242), 22u);
    EXPECT_EQ(MaxRoutingEntriesBySize(kOn, 242), 22u);
    EXPECT_EQ(MaxRoutingEntriesBySize(kOn, 20), 0u);
}

// The largest payload of every send type seals to exactly max_packet_size
// and one byte more is refused, at the three radio limits of the spec
TEST(FrameFormat, LargestPayloadSealsToExactlyTheLimit) {
    for (const size_t limit : {size_t{115}, size_t{242}, size_t{51}}) {
        TestNode node(kNode, 1, {}, {{1, NetKey1()}}, limit);

        struct Send {
            MessageType type;
            bool group_acks;
        };

        for (const Send send : {Send{MessageType::DATA, false},
                                Send{MessageType::DATA_BROADCAST, false},
                                Send{MessageType::DATA_RELIABLE, false},
                                Send{MessageType::DATA_GROUP, false},
                                Send{MessageType::DATA_GROUP, true}}) {
            const size_t max_payload =
                node.codec.MaxPayloadSize(send.type, send.group_acks);
            const bool prefix =
                send.type == MessageType::DATA_RELIABLE || send.group_acks;
            for (const size_t extra : {size_t{0}, size_t{1}}) {
                const Bytes body(max_payload + extra + (prefix ? 5 : 0), 0x5A);
                const Bytes plain =
                    send.type == MessageType::DATA_GROUP
                        ? PlainGroup(kGroup, kNode, 5, prefix ? 1 : 0, 1, body)
                        : PlainData(send.type == MessageType::DATA_BROADCAST
                                        ? 0xFFFF
                                        : kGateway,
                                    kNode, send.type, kRelay, 5, 1, body);
                const Bytes frame = node.Seal(plain);
                if (extra == 0) {
                    EXPECT_EQ(node.last_seal, SealStatus::kOk);
                    EXPECT_EQ(frame.size(), limit);
                    EXPECT_EQ(frame.size(),
                              node.codec.OnAirSize(send.type, plain.size()));
                } else {
                    EXPECT_EQ(node.last_seal, SealStatus::kTooLarge);
                    EXPECT_TRUE(frame.empty());
                }
            }
        }
    }
}

// Nothing leaves the codec above 115 bytes at the default setting
TEST(FrameFormat, NothingExceedsTheDefaultLimit) {
    TestNode node(kNode);
    const uint32_t control_before =
        node.counters.GetNext(CounterSpace::kControl);
    // A routing table of 9 entries is the largest control frame that fits
    const Bytes nine = PlainFrame(0xFFFF, kNode, MessageType::ROUTE_TABLE,
                                  Bytes(7 + 9 * 10, 0x11));
    EXPECT_EQ(node.Seal(nine).size(), 111u);
    const Bytes ten = PlainFrame(0xFFFF, kNode, MessageType::ROUTE_TABLE,
                                 Bytes(7 + 10 * 10, 0x11));
    EXPECT_TRUE(node.Seal(ten).empty());
    EXPECT_EQ(node.last_seal, SealStatus::kTooLarge);
    // The refused frame took no counter
    EXPECT_EQ(node.counters.GetNext(CounterSpace::kControl),
              control_before + 1);

    for (size_t payload = 0; payload <= 255; ++payload) {
        for (const MessageType type :
             {MessageType::ROUTE_TABLE, MessageType::JOIN_REQUEST,
              MessageType::DATA, MessageType::DATA_GROUP}) {
            const Bytes plain =
                PlainFrame(type == MessageType::DATA ? kGateway : 0xFFFF, kNode,
                           type, Bytes(payload, 0x01));
            const Bytes frame = node.Seal(plain);
            EXPECT_LE(frame.size(), 115u);
            if (node.last_seal == SealStatus::kOk) {
                EXPECT_EQ(frame.size(),
                          node.codec.OnAirSize(type, plain.size()));
            }
        }
    }
}

}  // namespace
}  // namespace codec_test
}  // namespace security
}  // namespace loramesher
