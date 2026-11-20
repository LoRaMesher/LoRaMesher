/**
 * @file frame_codec_vectors_test.cpp
 * @brief Worked vectors of the wire security spec through the frame codec
 *
 * Each test hands the codec a plain frame as the message classes serialize
 * it and expects the exact on-air bytes of the specification (S12), then
 * opens those bytes at a second node and expects the plain frame back.
 */

#include <gtest/gtest.h>

#include "frame_codec_test_support.hpp"
#include "types/messages/loramesher/broadcast_message.hpp"
#include "types/messages/loramesher/data_message.hpp"
#include "types/messages/loramesher/join_request_message.hpp"
#include "types/messages/loramesher/join_response_message.hpp"
#include "types/messages/loramesher/routing_table_message.hpp"
#include "types/messages/loramesher/sync_beacon_message.hpp"

namespace loramesher {
namespace security {
namespace codec_test {
namespace {

Bytes SealMessage(TestNode& node, const BaseMessage& message) {
    node.Refill();
    Bytes out(kMaxSealedFrameSize);
    size_t out_len = 0;
    node.last_seal = node.codec.Seal(message, out, out_len);
    out.resize(out_len);
    return out;
}

// V1: sync beacon forwarded by 0x0A02, C_bcn = 0x000123
TEST(FrameCodecVectors, V1SyncBeacon) {
    const Bytes payload = FromHex("EFBE28B603010A02D2040000050C");
    const Bytes plain =
        PlainFrame(0xFFFF, kRelay, MessageType::SYNC_BEACON, payload);
    const Bytes expected =
        FromHex("FFFF020ACE0DD42A669BCCD9CDAFE4FB2AFE8823010086E3AF6E");

    TestNode relay(kRelay, 1, {0x123, 0, 0});
    const Bytes frame = relay.Seal(plain);
    ASSERT_EQ(relay.last_seal, SealStatus::kOk);
    EXPECT_EQ(frame, expected);
    EXPECT_EQ(frame.size(), 26u);
    EXPECT_EQ(frame.size(),
              relay.codec.OnAirSize(MessageType::SYNC_BEACON, plain.size()));

    TestNode node(kNode);
    const Bytes opened = node.Open(expected);
    ASSERT_EQ(node.last_open, OpenStatus::kOk);
    EXPECT_EQ(opened, plain);
    EXPECT_EQ(node.info.role, RxRole::kLocal);
    EXPECT_EQ(node.info.frame_class, FrameClass::kBeacon);
    EXPECT_EQ(node.info.transmitter, kRelay);
    EXPECT_EQ(node.info.counter, 0x123u);
    EXPECT_EQ(node.info.epoch, 1);
    EXPECT_EQ(node.info.on_air_size, 26u);

    // The existing parser reads the opened frame
    const auto beacon = SyncBeaconMessage::CreateFromSerialized(opened);
    ASSERT_TRUE(beacon.has_value());
    EXPECT_EQ(beacon->GetNetworkId(), 0xBEEF);
    EXPECT_EQ(beacon->GetTotalSlots(), 40);
    EXPECT_EQ(beacon->GetSlotDuration(), 950);
    EXPECT_EQ(beacon->GetNetworkManager(), kGateway);
    EXPECT_EQ(beacon->GetHopCount(), 2);
    EXPECT_EQ(beacon->GetPropagationDelay(), 1234u);
}

// V1 through the message level entry points
TEST(FrameCodecVectors, V1SyncBeaconMessageLevel) {
    const Bytes payload = FromHex("EFBE28B603010A02D2040000050C");
    const BaseMessage message(0xFFFF, kRelay, MessageType::SYNC_BEACON,
                              payload);
    TestNode relay(kRelay, 1, {0x123, 0, 0});
    const Bytes frame = SealMessage(relay, message);
    EXPECT_EQ(frame,
              FromHex("FFFF020ACE0DD42A669BCCD9CDAFE4FB2AFE8823010086E3AF6E"));

    TestNode node(kNode);
    const auto opened = node.codec.Open(frame);
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened->GetType(), MessageType::SYNC_BEACON);
    EXPECT_EQ(opened->Serialize(), message.Serialize());
}

// V2: routing table fragment from 0x0A02, two entries, C_ctl = 0x457
TEST(FrameCodecVectors, V2RoutingTable) {
    RoutingTableEntry first(kGateway, 1, 230, 2, 0x01, 0);
    first.reception_quality = 220;
    first.next_hop = kGateway;
    RoutingTableEntry second(kNode, 1, 200, 1, 0, 2);
    second.reception_quality = 190;
    second.next_hop = kNode;
    const auto table = RoutingTableMessage::Create(0xFFFF, kRelay, kGateway, 7,
                                                   {first, second}, 0, 2, 1);
    ASSERT_TRUE(table.has_value());
    const BaseMessage message = table->ToBaseMessage();
    const Bytes plain = *message.Serialize();
    EXPECT_EQ(plain,
              Concat({FromHex("FFFF020A321B"),
                      FromHex("010A0702000201010A01E6020100DC010A030A01C80100"
                              "02BE030A")}));
    const Bytes expected = FromHex(
        "FFFF020ABA1BD2FDA910C18F3255A05278A8B3F3C6BA949FBCD5619BA24EC88CF9570"
        "4000074482AA8");

    TestNode relay(kRelay, 1, {0, 0x457, 0});
    EXPECT_EQ(SealMessage(relay, message), expected);
    EXPECT_EQ(expected.size(), 41u);

    TestNode node(kNode);
    const Bytes opened = node.Open(expected);
    ASSERT_EQ(node.last_open, OpenStatus::kOk);
    EXPECT_EQ(opened, plain);
    EXPECT_EQ(node.info.transmitter, kRelay);
    EXPECT_EQ(node.info.counter, 0x457u);
    EXPECT_EQ(node.info.frame_class, FrameClass::kControl);
}

// V3: end-to-end DATA from 0x0A03 to the gateway. The relay verifies hop 1
// and produces hop 2 without any key of the node
TEST(FrameCodecVectors, V3EndToEndDataBothHopsAtTheRelay) {
    const Bytes hop1 = FromHex(
        "010A030A990E2366EEB6204E67B19EF7FDA87A3A290000805E7BC82CCF6288E4");
    const Bytes hop2 = FromHex(
        "010A030A990E58BA570C204E67B19EF7FDA87A3A290000805E7BC82C7F2F90E2");

    TestNode relay(kRelay);
    const Bytes opened = relay.Open(hop1);
    ASSERT_EQ(relay.last_open, OpenStatus::kOk);
    EXPECT_EQ(relay.info.role, RxRole::kRelay);
    EXPECT_TRUE(relay.info.end_to_end);
    EXPECT_EQ(relay.info.counter, 0x29u);
    EXPECT_EQ(relay.info.context.ctr_field, 0x80000029u);
    EXPECT_EQ(relay.info.context.body_state, BodyState::kCipher);
    // Readable routing bytes, body still the received ciphertext
    EXPECT_EQ(opened, Concat({FromHex("010A030A110E"), FromHex("020A0A37"),
                              FromHex("204E67B19EF7FDA87A3A")}));

    const uint32_t data_counter_before =
        relay.counters.GetNext(CounterSpace::kData);
    EXPECT_EQ(relay.Forward(opened, kGateway), hop2);
    EXPECT_EQ(relay.last_seal, SealStatus::kOk);
    // Forwarding spends no counter of the relay and asks for no node key
    EXPECT_EQ(relay.counters.GetNext(CounterSpace::kData), data_counter_before);
    EXPECT_EQ(relay.provider->node_key_calls, 0);

    // The relay cannot open the body
    relay.OpenBody(opened);
    EXPECT_EQ(relay.last_open, OpenStatus::kKeyUnavailable);

    // The hop layer of hop 2 verifies at the gateway
    TestNode gateway(kGateway);
    const Bytes at_gateway = gateway.Open(hop2);
    ASSERT_EQ(gateway.last_open, OpenStatus::kOk);
    EXPECT_EQ(gateway.info.role, RxRole::kDestination);
    EXPECT_EQ(at_gateway, Concat({FromHex("010A030A110E"), FromHex("010A0937"),
                                  FromHex("204E67B19EF7FDA87A3A")}));
}

// V3 through the message classes: Open, DataMessage, CreateForwarded, Seal
TEST(FrameCodecVectors, V3ForwardedThroughDataMessage) {
    const Bytes hop1 = FromHex(
        "010A030A990E2366EEB6204E67B19EF7FDA87A3A290000805E7BC82CCF6288E4");
    const Bytes hop2 = FromHex(
        "010A030A990E58BA570C204E67B19EF7FDA87A3A290000805E7BC82C7F2F90E2");
    TestNode relay(kRelay);
    const auto received = relay.codec.Open(hop1);
    ASSERT_TRUE(received.has_value());
    const auto data = DataMessage::CreateFromBaseMessage(*received);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data->GetNextHop(), kRelay);
    EXPECT_EQ(data->GetTTL(), 10);
    EXPECT_EQ(data->GetSeqNum(), 0x37);
    const auto forwarded = DataMessage::CreateForwarded(*data, kGateway);
    ASSERT_TRUE(forwarded.has_value());
    EXPECT_EQ(forwarded->GetSecurityContext(), received->GetSecurityContext());
    EXPECT_EQ(SealMessage(relay, forwarded->ToBaseMessage()), hop2);
}

// V4: DATA_BROADCAST from 0x0A03, E = 0, C_data = 0x2A
TEST(FrameCodecVectors, V4Broadcast) {
    const Bytes body = {'h', 'e', 'l', 'l', 'o', ' ', 'm', 'e', 's', 'h'};
    const auto broadcast = BroadcastMessage::Create(kNode, 10, 0x38, body);
    ASSERT_TRUE(broadcast.has_value());
    const BaseMessage message = broadcast->ToBaseMessage();
    const Bytes plain = *message.Serialize();
    const Bytes expected = FromHex(
        "FFFF030A9A0EDA3C6BB5E1BFFE005EF6AF1B37A12A0000004F6D5274BFD0F592");

    TestNode node(kNode, 1, {0, 0, 0x2A});
    EXPECT_EQ(SealMessage(node, message), expected);
    EXPECT_EQ(expected.size(), 32u);

    TestNode relay(kRelay);
    const Bytes opened = relay.Open(expected);
    ASSERT_EQ(relay.last_open, OpenStatus::kOk);
    EXPECT_EQ(relay.info.role, RxRole::kFlood);
    EXPECT_FALSE(relay.info.end_to_end);
    EXPECT_EQ(relay.info.counter, 0x2Au);
    EXPECT_EQ(Bytes(opened.begin() + 10, opened.end()),
              FromHex("E1BFFE005EF6AF1B37A1"));
    // Delivery copy
    EXPECT_EQ(relay.OpenBody(opened), plain);
    EXPECT_EQ(relay.last_open, OpenStatus::kOk);

    // The flood copy the relay sends on carries the same body bytes, the
    // same counter field and the same tag
    const Bytes forwarded = relay.Forward(opened, 0xFFFF);
    ASSERT_EQ(relay.last_seal, SealStatus::kOk);
    EXPECT_EQ(Bytes(forwarded.begin() + 10, forwarded.end() - 4),
              Bytes(expected.begin() + 10, expected.end() - 4));
    EXPECT_EQ(Bytes(forwarded.begin(), forwarded.begin() + 6),
              Bytes(expected.begin(), expected.begin() + 6));

    TestNode gateway(kGateway);
    const Bytes second_hop = gateway.Open(forwarded);
    ASSERT_EQ(gateway.last_open, OpenStatus::kOk);
    const auto delivered =
        BroadcastMessage::CreateFromSerialized(gateway.OpenBody(second_hop));
    ASSERT_TRUE(delivered.has_value());
    EXPECT_EQ(delivered->GetPayload(), body);
    EXPECT_EQ(delivered->GetTTL(), 9);
    EXPECT_EQ(delivered->GetSeqNum(), 0x38);
}

// V5a: JOIN_REQUEST with nonce, sent by the joiner 0x0A03, C_ctl = 0
TEST(FrameCodecVectors, V5aJoinRequestFirstHop) {
    const auto request = JoinRequestMessage::Create(
        kGateway, kNode, 1, FromHex("C0FFEE42"), kRelay, kRelay, 0);
    ASSERT_TRUE(request.has_value());
    const BaseMessage message = request->ToBaseMessage();
    const Bytes plain = *message.Serialize();
    EXPECT_EQ(plain, FromHex("010A030A420A01020A020A00C0FFEE42"));
    const Bytes expected =
        FromHex("010A030ACA0A566DDDC4E6B7A23E0525030A00000000241A769C");

    TestNode joiner(kNode);
    EXPECT_EQ(SealMessage(joiner, message), expected);
    EXPECT_EQ(expected.size(), kSecuredJoinRequestFirstHopSize);
    EXPECT_EQ(joiner.codec.GetOwnJoinNonceCount(), 1u);

    TestNode sponsor(kRelay);
    const Bytes opened = sponsor.Open(expected);
    ASSERT_EQ(sponsor.last_open, OpenStatus::kOk);
    EXPECT_EQ(opened, plain);
    EXPECT_EQ(sponsor.info.frame_class, FrameClass::kJoin);
    EXPECT_EQ(sponsor.info.transmitter, kNode);
    EXPECT_EQ(sponsor.info.counter, 0u);
}

// V5b: the request forwarded by 0x0A02 without the nonce, C_ctl = 2
TEST(FrameCodecVectors, V5bJoinRequestForwarded) {
    const auto request =
        JoinRequestMessage::Create(kGateway, kNode, 1, {}, kGateway, kRelay, 1);
    ASSERT_TRUE(request.has_value());
    const BaseMessage message = request->ToBaseMessage();
    const Bytes plain = *message.Serialize();
    const Bytes expected =
        FromHex("010A030ACA060173303374AC020A02000000789342C9");

    TestNode relay(kRelay, 1, {0, 2, 0});
    EXPECT_EQ(SealMessage(relay, message), expected);
    EXPECT_EQ(expected.size(), kSecuredJoinRequestForwardedSize);
    // A forwarder is no joiner, so it remembers no nonce of its own
    EXPECT_EQ(relay.codec.GetOwnJoinNonceCount(), 0u);

    TestNode gateway(kGateway);
    const Bytes opened = gateway.Open(expected);
    ASSERT_EQ(gateway.last_open, OpenStatus::kOk);
    EXPECT_EQ(opened, plain);
    EXPECT_EQ(gateway.info.transmitter, kRelay);
    EXPECT_EQ(gateway.info.counter, 2u);
}

// V5a, V5b and V5c as one exchange: the sponsor learns the nonce from the
// request it hears, forwards the request, and binds the nonce into the
// final response. The joiner accepts the response
TEST(FrameCodecVectors, V5JoinExchange) {
    TestNode joiner(kNode);
    TestNode sponsor(kRelay, 1, {0, 2, 0});
    TestNode gateway(kGateway);

    const auto request = JoinRequestMessage::Create(
        kGateway, kNode, 1, FromHex("C0FFEE42"), kRelay, kRelay, 0);
    const Bytes first_hop = SealMessage(joiner, request->ToBaseMessage());
    EXPECT_EQ(first_hop, FromHex("010A030ACA0A566DDDC4E6B7A23E0525030A00000000"
                                 "241A769C"));
    const auto heard = sponsor.codec.Open(first_hop);
    ASSERT_TRUE(heard.has_value());
    const auto parsed = JoinRequestMessage::CreateFromBaseMessage(*heard);
    ASSERT_TRUE(parsed.has_value());

    // Forwarded as the network service builds it: no additional info
    const auto forwarded_request = JoinRequestMessage::Create(
        parsed->GetDestination(), parsed->GetSource(),
        parsed->GetRequestedSlots(), {}, kGateway,
        parsed->GetHeader().GetSponsorAddress(), parsed->GetHopCount() + 1);
    const Bytes second_hop =
        SealMessage(sponsor, forwarded_request->ToBaseMessage());
    EXPECT_EQ(second_hop,
              FromHex("010A030ACA060173303374AC020A02000000789342C9"));
    ASSERT_TRUE(gateway.codec.Open(second_hop).has_value());

    // V5c: final JOIN_RESPONSE from the sponsor to the joiner, C_ctl = 3
    const auto response = JoinResponseMessage::Create(
        kNode, kGateway, 0xBEEF, 1, JoinResponseHeader::ACCEPTED, {}, kNode, 0,
        2);
    ASSERT_TRUE(response.has_value());
    const BaseMessage response_message = response->ToBaseMessage();
    const Bytes response_plain = *response_message.Serialize();
    EXPECT_EQ(response_plain, FromHex("030A010A4309EFBE0100030A000002"));
    const Bytes response_frame = SealMessage(sponsor, response_message);
    EXPECT_EQ(response_frame, FromHex("030A010ACB09E8BDEC55C0D6D23B05020A030000"
                                      "00C3D5D4EB"));
    EXPECT_EQ(response_frame.size(), kSecuredJoinResponseSize);

    const Bytes accepted = joiner.Open(response_frame);
    ASSERT_EQ(joiner.last_open, OpenStatus::kOk);
    EXPECT_EQ(accepted, response_plain);
    EXPECT_EQ(joiner.info.transmitter, kRelay);
    EXPECT_EQ(joiner.info.counter, 3u);
}

// V5c stand-alone: the joiner verifies the vector bytes with its nonce
TEST(FrameCodecVectors, V5cJoinResponseAtTheJoiner) {
    TestNode joiner(kNode);
    const auto request = JoinRequestMessage::Create(
        kGateway, kNode, 1, FromHex("C0FFEE42"), kRelay, kRelay, 0);
    SealMessage(joiner, request->ToBaseMessage());
    const Bytes opened = joiner.Open(
        FromHex("030A010ACB09E8BDEC55C0D6D23B05020A03000000C3D5D4EB"));
    ASSERT_EQ(joiner.last_open, OpenStatus::kOk);
    EXPECT_EQ(opened, FromHex("030A010A4309EFBE0100030A000002"));
}

// V6: KEY_UPDATE from the gateway to 0x0A03 over the relay 0x0A02. A relay
// of this step verifies the hop layer and forwards the frame like DATA
TEST(FrameCodecVectors, V6KeyUpdateAtTheRelay) {
    const Bytes frame = FromHex(
        "030A010AD91E4246256A892C29831AFBF2DD1512B9FA14EEB8B1852CD4578B2822E8BA"
        "5C000100808CC2C9BCFC9B89C8");
    ASSERT_EQ(frame.size(), 48u);
    TestNode relay(kRelay);
    const Bytes opened = relay.Open(frame);
    ASSERT_EQ(relay.last_open, OpenStatus::kOk);
    EXPECT_EQ(relay.info.plain_type, MessageType::KEY_UPDATE);
    EXPECT_EQ(relay.info.role, RxRole::kRelay);
    EXPECT_TRUE(relay.info.end_to_end);
    EXPECT_EQ(relay.info.counter, 0x100u);
    EXPECT_EQ(Bytes(opened.begin() + 6, opened.begin() + 10),
              FromHex("020A0A05"));
    // No hop window for a key type: nothing was recorded for the gateway
    EXPECT_EQ(relay.codec.GetPeers().Find(kGateway), nullptr);

    const Bytes forwarded = relay.Forward(opened, kNode);
    ASSERT_EQ(relay.last_seal, SealStatus::kOk);
    EXPECT_EQ(Bytes(forwarded.begin() + 10, forwarded.end() - 4),
              Bytes(frame.begin() + 10, frame.end() - 4));

    // The destination verifies the hop layer. Its body needs the node key
    TestNode node(kNode);
    const Bytes at_node = node.Open(forwarded);
    ASSERT_EQ(node.last_open, OpenStatus::kOk);
    EXPECT_EQ(node.info.role, RxRole::kDestination);
    EXPECT_EQ(Bytes(at_node.begin() + 6, at_node.begin() + 10),
              FromHex("030A0905"));
    node.OpenBody(at_node);
    EXPECT_EQ(node.last_open, OpenStatus::kKeyUnavailable);
}

}  // namespace
}  // namespace codec_test
}  // namespace security
}  // namespace loramesher
