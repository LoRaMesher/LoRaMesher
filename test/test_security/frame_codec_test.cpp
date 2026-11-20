/**
 * @file frame_codec_test.cpp
 * @brief Rejection, replay, relay and join behaviour of the frame codec
 */

#include <gtest/gtest.h>

#include "frame_codec_test_support.hpp"
#include "types/messages/loramesher/broadcast_message.hpp"
#include "types/messages/loramesher/data_message.hpp"
#include "types/messages/loramesher/group_message.hpp"

namespace loramesher {
namespace security {
namespace codec_test {
namespace {

const Bytes kBeaconPayload = FromHex("EFBE28B603010A02D2040000050C");
const Bytes kBody = {'t', 'e', 'm', 'p', '=', '2', '1', '.', '5', 'C'};

Bytes Beacon(AddressType source) {
    return PlainFrame(0xFFFF, source, MessageType::SYNC_BEACON, kBeaconPayload);
}

Bytes Claim(AddressType source) {
    return PlainFrame(0xFFFF, source, MessageType::NM_CLAIM,
                      FromHex("05030000"));
}

Bytes JoinRequest(AddressType joiner, AddressType sponsor, const Bytes& nonce) {
    return PlainFrame(kGateway, joiner, MessageType::JOIN_REQUEST,
                      Concat({{1}, Le16(sponsor), Le16(sponsor), {0}, nonce}));
}

Bytes JoinResponse(AddressType destination) {
    return PlainFrame(destination, kGateway, MessageType::JOIN_RESPONSE,
                      FromHex("EFBE0100030A000002"));
}

NetworkTrafficKeys Keys1() {
    return RefNetKeys(NetKey1(), 1);
}

/// Body keys of an end-to-end frame of 0x0A03 in epoch 1
NodeTrafficKeys NodeKeys() {
    DefaultCryptoBackend backend;
    NodeTrafficKeys keys;
    DeriveNodeTrafficKeys(backend, NodeKey0A03(), 1, keys);
    return keys;
}

/// End-to-end DATA frame of 0x0A03, as a node with step 3 would send it
Bytes EndToEndData(AddressType destination, AddressType next_hop,
                   uint32_t counter, MessageType type = MessageType::DATA) {
    const NodeTrafficKeys node_keys = NodeKeys();
    return RefData(
        Keys1(), 1,
        PlainData(destination, kNode, type, next_hop, 10, 0x37, kBody),
        counter | kEndToEndFlag, 0x04, node_keys.ee, node_keys.em);
}

/// Group-keyed DATA frame of 0x0A03 with a chosen counter
Bytes GroupData(AddressType destination, AddressType next_hop, uint32_t counter,
                uint8_t epoch = 1, const Key128& network_key = NetKey1()) {
    return RefGroupData(RefNetKeys(network_key, epoch), epoch,
                        PlainData(destination, kNode, MessageType::DATA,
                                  next_hop, 10, 0x37, kBody),
                        counter);
}

/**
 * @brief Flip every bit of a frame once and expect each copy to be dropped
 *
 * The receiver must end with no replay state at all, and must still accept
 * the untouched frame afterwards.
 */
void ExpectEveryBitFlipRejected(TestNode& receiver, const Bytes& frame) {
    for (size_t bit = 0; bit < frame.size() * 8; ++bit) {
        Bytes mutated = frame;
        mutated[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));
        const Bytes opened = receiver.Open(mutated);
        ASSERT_TRUE(opened.empty()) << "bit " << bit;
        ASSERT_NE(receiver.last_open, OpenStatus::kOk) << "bit " << bit;
        ASSERT_EQ(receiver.info.role, RxRole::kNone);
    }
    EXPECT_EQ(receiver.codec.GetPeers().Size(), 0u);
    EXPECT_EQ(receiver.codec.GetStats().rx_opened, 0u);
    receiver.Open(frame);
    EXPECT_EQ(receiver.last_open, OpenStatus::kOk);
}

// ---------------------------------------------------------------------------
// Any flipped bit is rejected, per frame class
// ---------------------------------------------------------------------------

TEST(FrameCodecReject, AnyFlippedBitOfABeacon) {
    TestNode sender(kRelay);
    TestNode receiver(kNode);
    ExpectEveryBitFlipRejected(receiver, sender.Seal(Beacon(kRelay)));
}

TEST(FrameCodecReject, AnyFlippedBitOfAControlFrame) {
    TestNode sender(kRelay);
    TestNode receiver(kNode);
    const Bytes table = PlainFrame(0xFFFF, kRelay, MessageType::ROUTE_TABLE,
                                   Bytes(7 + 3 * 10, 0x42));
    ExpectEveryBitFlipRejected(receiver, sender.Seal(table));
}

TEST(FrameCodecReject, AnyFlippedBitOfAJoinRequest) {
    TestNode joiner(kNode);
    TestNode receiver(kRelay);
    ExpectEveryBitFlipRejected(
        receiver, joiner.Seal(JoinRequest(kNode, kRelay, FromHex("C0FFEE42"))));
}

TEST(FrameCodecReject, AnyFlippedBitOfAJoinResponse) {
    TestNode joiner(kNode);
    TestNode sponsor(kRelay);
    sponsor.Open(joiner.Seal(JoinRequest(kNode, kRelay, FromHex("C0FFEE42"))));
    ASSERT_EQ(sponsor.last_open, OpenStatus::kOk);
    ExpectEveryBitFlipRejected(joiner, sponsor.Seal(JoinResponse(kNode)));
}

TEST(FrameCodecReject, AnyFlippedBitOfAUnicastDataFrame) {
    TestNode origin(kNode);
    TestNode destination(kGateway);
    ExpectEveryBitFlipRejected(
        destination, origin.Seal(PlainData(kGateway, kNode, MessageType::DATA,
                                           kGateway, 10, 1, kBody)));
}

TEST(FrameCodecReject, AnyFlippedBitOfAReliableAndAnAckFrame) {
    TestNode origin(kNode);
    TestNode relay(kRelay);
    ExpectEveryBitFlipRejected(
        relay, origin.Seal(PlainData(kGateway, kNode,
                                     MessageType::DATA_RELIABLE, kRelay, 10, 2,
                                     Concat({FromHex("0711223344"), kBody}))));
    TestNode destination(kGateway);
    ExpectEveryBitFlipRejected(
        destination,
        origin.Seal(PlainData(kGateway, kNode, MessageType::ACK, kGateway, 10,
                              3, FromHex("070011223344"))));
}

TEST(FrameCodecReject, AnyFlippedBitOfABroadcastAndAGroupFrame) {
    TestNode origin(kNode);
    TestNode first(kRelay);
    ExpectEveryBitFlipRejected(
        first, origin.Seal(PlainData(0xFFFF, kNode, MessageType::DATA_BROADCAST,
                                     0xFFFF, 10, 4, kBody)));
    TestNode second(kGateway);
    ExpectEveryBitFlipRejected(
        second, origin.Seal(PlainGroup(kGroup, kNode, 10, 0x01, 5, kBody)));
}

TEST(FrameCodecReject, AnyFlippedBitOfAForwardedFrame) {
    TestNode origin(kNode);
    TestNode relay(kRelay);
    const Bytes opened = relay.Open(origin.Seal(
        PlainData(kGateway, kNode, MessageType::DATA, kRelay, 10, 1, kBody)));
    ASSERT_EQ(relay.last_open, OpenStatus::kOk);
    const Bytes forwarded = relay.Forward(opened, kGateway);
    ASSERT_EQ(relay.last_seal, SealStatus::kOk);
    TestNode destination(kGateway);
    ExpectEveryBitFlipRejected(destination, forwarded);
}

TEST(FrameCodecReject, TruncatedAndExtendedFrames) {
    TestNode sender(kRelay);
    TestNode origin(kNode);
    const Bytes frames[] = {
        sender.Seal(Beacon(kRelay)),
        sender.Seal(Claim(kRelay)),
        sender.Seal(JoinResponse(kOther)),
        origin.Seal(PlainData(kGateway, kNode, MessageType::DATA, kGateway, 10,
                              1, kBody)),
    };
    TestNode receiver(kGateway);
    for (const Bytes& frame : frames) {
        for (size_t size = 0; size < frame.size(); ++size) {
            receiver.Open(Bytes(frame.begin(), frame.begin() + size));
            EXPECT_EQ(receiver.last_open, OpenStatus::kMalformed);
        }
        Bytes longer = frame;
        longer.push_back(0);
        receiver.Open(longer);
        EXPECT_EQ(receiver.last_open, OpenStatus::kMalformed);
    }
    EXPECT_EQ(receiver.codec.GetPeers().Size(), 0u);
    EXPECT_EQ(receiver.codec.GetStats().rx_malformed, 26u + 18 + 25 + 32 + 4);
}

// ---------------------------------------------------------------------------
// Keys, epoch bit, transmitter
// ---------------------------------------------------------------------------

TEST(FrameCodecReject, WrongNetworkKey) {
    TestNode sender(kRelay);
    TestNode origin(kNode);
    TestNode stranger(kGateway, 1, {}, {{1, NetKey2()}});
    for (const Bytes& frame :
         {sender.Seal(Beacon(kRelay)), sender.Seal(Claim(kRelay)),
          sender.Seal(JoinResponse(kOther)),
          origin.Seal(PlainData(kGateway, kNode, MessageType::DATA, kGateway,
                                10, 1, kBody))}) {
        EXPECT_TRUE(stranger.Open(frame).empty());
    }
    const SecurityStats stats = stranger.codec.GetStats();
    EXPECT_EQ(stats.rx_auth_failed, 3u);
    // A JOIN_RESPONSE for another node is counted apart
    EXPECT_EQ(stats.rx_join_unverified, 1u);
    EXPECT_EQ(stranger.codec.GetPeers().Size(), 0u);
    EXPECT_EQ(stranger.codec.GetLastFailedSource(), kNode);
}

TEST(FrameCodecReject, WrongBodyKeyWithAValidHopLayer) {
    // A frame whose hop layer is right and whose body was tagged with
    // another key: it passes the hop check and fails at the body
    const NetworkTrafficKeys keys = Keys1();
    const NodeTrafficKeys other = NodeKeys();
    const Bytes frame = RefData(
        keys, 1,
        PlainData(kGateway, kNode, MessageType::DATA, kGateway, 10, 1, kBody),
        7, 0x03, other.ee, other.em);
    TestNode destination(kGateway);
    const Bytes opened = destination.Open(frame);
    ASSERT_EQ(destination.last_open, OpenStatus::kOk);
    EXPECT_TRUE(destination.OpenBody(opened).empty());
    EXPECT_EQ(destination.last_open, OpenStatus::kTagFailed);
    EXPECT_EQ(destination.codec.GetStats().rx_tag_failed, 1u);
}

TEST(FrameCodecReject, EpochBitWithoutAHeldEpoch) {
    TestNode sender(kRelay, 2, {}, {{2, NetKey2()}});
    TestNode receiver(kNode);
    EXPECT_TRUE(receiver.Open(sender.Seal(Beacon(kRelay))).empty());
    EXPECT_EQ(receiver.last_open, OpenStatus::kUnknownEpoch);
    EXPECT_TRUE(receiver.Open(sender.Seal(Claim(kRelay))).empty());
    EXPECT_EQ(receiver.last_open, OpenStatus::kUnknownEpoch);
    TestNode origin(kOther, 2, {}, {{2, NetKey2()}});
    EXPECT_TRUE(receiver
                    .Open(origin.Seal(PlainData(
                        kNode, kOther, MessageType::DATA, kNode, 10, 1, kBody)))
                    .empty());
    EXPECT_EQ(receiver.last_open, OpenStatus::kUnknownEpoch);
    EXPECT_EQ(receiver.codec.GetStats().rx_unknown_epoch, 3u);
    EXPECT_EQ(receiver.codec.GetStats().rx_auth_failed, 0u);
}

TEST(FrameCodecReject, EpochBitThatNamesTheOtherHeldKey) {
    // Sealed under the key of epoch 1 and marked with the parity of epoch 2
    TestNode receiver(kGateway, 1, {}, {{1, NetKey1()}, {2, NetKey2()}});
    const NetworkTrafficKeys keys = Keys1();
    receiver.Open(RefBeacon(keys, 2, Beacon(kRelay), 5));
    EXPECT_EQ(receiver.last_open, OpenStatus::kAuthFailed);
    receiver.Open(RefControl(keys, 2, Claim(kRelay), kRelay, 5, false));
    EXPECT_EQ(receiver.last_open, OpenStatus::kAuthFailed);
    receiver.Open(RefGroupData(
        keys, 2,
        PlainData(kGateway, kNode, MessageType::DATA, kGateway, 10, 1, kBody),
        5));
    EXPECT_EQ(receiver.last_open, OpenStatus::kAuthFailed);
    EXPECT_EQ(receiver.codec.GetPeers().Size(), 0u);

    // The same frames with the right parity are accepted
    receiver.Open(RefBeacon(keys, 1, Beacon(kRelay), 5));
    EXPECT_EQ(receiver.last_open, OpenStatus::kOk);
    EXPECT_EQ(receiver.info.epoch, 1);
}

TEST(FrameCodecReject, AKeyOfALowerEpochIsNeverInstalledOverAHigherOne) {
    DefaultCryptoBackend backend;
    KeyRing ring;
    EXPECT_EQ(ring.Install(backend, {3, NetKey1()}), CryptoStatus::kOk);
    EXPECT_EQ(ring.Install(backend, {1, NetKey2()}),
              CryptoStatus::kInvalidArgument);
    EXPECT_EQ(ring.Install(backend, {255, NetKey2()}),
              CryptoStatus::kInvalidArgument);
    EXPECT_EQ(ring.Install(backend, {4, NetKey2()}), CryptoStatus::kOk);
    EXPECT_EQ(ring.Count(), 2u);
    uint8_t epoch = 0;
    ASSERT_NE(ring.FindByParity(1, epoch), nullptr);
    EXPECT_EQ(epoch, 3);
    EXPECT_EQ(ring.FindByEpoch(1), nullptr);
    ring.Retire(3);
    EXPECT_EQ(ring.FindByParity(1, epoch), nullptr);
    EXPECT_EQ(ring.Count(), 1u);
}

TEST(FrameCodecReject, WrongTransmitter) {
    TestNode sender(kRelay);
    TestNode receiver(kGateway);
    // Class C1 and class B name the transmitter by the header source
    for (Bytes frame :
         {sender.Seal(Claim(kRelay)), sender.Seal(Beacon(kRelay))}) {
        frame[2] = 0x04;  // source 0x0A04
        receiver.Open(frame);
        EXPECT_EQ(receiver.last_open, OpenStatus::kAuthFailed);
    }
    // Class C2 names it in the trailer
    Bytes join = sender.Seal(JoinResponse(kGateway));
    join[join.size() - 10] = 0x04;
    receiver.Open(join);
    EXPECT_EQ(receiver.last_open, OpenStatus::kAuthFailed);
    EXPECT_EQ(receiver.codec.GetPeers().Size(), 0u);
}

TEST(FrameCodecReject, SealingAControlFrameOfAnotherSourceIsRefused) {
    // The nonce of class B and C1 is built from the header source. Sealing
    // a foreign source would spend this node's counter under that address
    TestNode node(kRelay);
    const uint32_t beacon_before = node.counters.GetNext(CounterSpace::kBeacon);
    const uint32_t control_before =
        node.counters.GetNext(CounterSpace::kControl);
    EXPECT_TRUE(node.Seal(Beacon(kNode)).empty());
    EXPECT_EQ(node.last_seal, SealStatus::kInvalidParameter);
    EXPECT_TRUE(node.Seal(Claim(kNode)).empty());
    EXPECT_EQ(node.last_seal, SealStatus::kInvalidParameter);
    EXPECT_EQ(node.counters.GetNext(CounterSpace::kBeacon), beacon_before);
    EXPECT_EQ(node.counters.GetNext(CounterSpace::kControl), control_before);
    // A join frame keeps its source across hops and is sealed by any member
    EXPECT_FALSE(node.Seal(JoinResponse(kNode)).empty());
}

TEST(FrameCodecReject, OwnFramesAreDropped) {
    TestNode node(kNode);
    node.Open(node.Seal(Beacon(kNode)));
    EXPECT_EQ(node.last_open, OpenStatus::kOwnFrame);
    node.Open(node.Seal(Claim(kNode)));
    EXPECT_EQ(node.last_open, OpenStatus::kOwnFrame);
    node.Open(node.Seal(JoinRequest(kNode, kRelay, FromHex("01020304"))));
    EXPECT_EQ(node.last_open, OpenStatus::kOwnFrame);
    // Its own flood, heard back from a neighbour
    TestNode relay(kRelay);
    const Bytes opened = relay.Open(node.Seal(PlainData(
        0xFFFF, kNode, MessageType::DATA_BROADCAST, 0xFFFF, 10, 1, kBody)));
    node.Open(relay.Forward(opened, 0xFFFF));
    EXPECT_EQ(node.last_open, OpenStatus::kOwnFrame);
    EXPECT_EQ(node.codec.GetPeers().Size(), 0u);
    // A frame another member forwards with this node's address as its
    // trailer transmitter is dropped before the MIC
    TestNode other(kRelay);
    Bytes join = other.Seal(JoinResponse(kGateway));
    join[join.size() - 10] = 0x03;
    node.Open(join);
    EXPECT_EQ(node.last_open, OpenStatus::kOwnFrame);
}

// ---------------------------------------------------------------------------
// Type marking and modes
// ---------------------------------------------------------------------------

TEST(FrameCodecReject, PlainFrameInSecuredMode) {
    TestNode receiver(kNode);
    receiver.Open(Beacon(kRelay));
    EXPECT_EQ(receiver.last_open, OpenStatus::kPlainDropped);
    receiver.Open(
        PlainData(kNode, kRelay, MessageType::DATA, kNode, 10, 1, kBody));
    EXPECT_EQ(receiver.last_open, OpenStatus::kPlainDropped);
    EXPECT_EQ(receiver.codec.GetStats().rx_plain_dropped, 2u);
}

TEST(FrameCodecReject, DisabledModeKeepsThePlainFormat) {
    TestNode plain_node(kNode, 0, {}, {}, kDefaultMaxPacketSize,
                        SecurityMode::kDisabled);
    TestNode secured_node(kRelay);
    const Bytes beacon = Beacon(kNode);
    // Seal and open are the identity: 2.0.0 frames stay byte for byte
    EXPECT_EQ(plain_node.Seal(beacon), beacon);
    EXPECT_EQ(plain_node.Open(beacon), beacon);
    EXPECT_EQ(plain_node.info.role, RxRole::kLocal);
    EXPECT_EQ(plain_node.codec.TrailerLen(MessageType::SYNC_BEACON), 0u);
    EXPECT_EQ(plain_node.codec.OnAirSize(MessageType::SYNC_BEACON, 20), 20u);
    EXPECT_EQ(plain_node.codec.MaxPayloadSize(MessageType::DATA), 105u);
    EXPECT_EQ(plain_node.counters.GetNext(CounterSpace::kBeacon), 0u);
    // A secured frame is rejected by its type byte
    plain_node.Open(secured_node.Seal(Beacon(kRelay)));
    EXPECT_EQ(plain_node.last_open, OpenStatus::kSecuredDropped);
    // And the plain node cannot forward data in the secured format
    Bytes out(kMaxSealedFrameSize);
    size_t out_len = 0;
    SecurityContext context;
    context.body_state = BodyState::kCipher;
    EXPECT_EQ(plain_node.codec.SealForwarded(
                  PlainData(kGateway, kRelay, MessageType::DATA, kGateway, 5, 1,
                            kBody),
                  context, out, out_len),
              SealStatus::kInvalidState);
}

TEST(FrameCodecReject, UnknownAndRecoveryTypes) {
    TestNode node(kNode);
    // Sealing a type outside the classes B, C1, C2 and D
    EXPECT_TRUE(node.Seal(PlainFrame(kRelay, kNode, MessageType::KEY_REQUEST,
                                     Bytes(13, 0)))
                    .empty());
    EXPECT_EQ(node.last_seal, SealStatus::kUnsupportedType);
    EXPECT_TRUE(
        node.Seal(PlainFrame(kRelay, kNode, static_cast<MessageType>(0x22), {}))
            .empty());
    EXPECT_EQ(node.last_seal, SealStatus::kUnsupportedType);
    // A type byte that already carries the secured or the epoch bit
    EXPECT_TRUE(
        node.Seal(PlainFrame(kRelay, kNode, static_cast<MessageType>(0xC6),
                             kBeaconPayload))
            .empty());
    EXPECT_EQ(node.last_seal, SealStatus::kInvalidParameter);
    EXPECT_EQ(node.codec.GetStats().tx_refused, 3u);

    // KEY_REQUEST (V7) and KEY_GRANT are dropped until their step exists
    node.Open(FromHex("020A030AD30D0107000000261F520C68E88AFD"));
    EXPECT_EQ(node.last_open, OpenStatus::kMalformed);
    node.Open(
        FromHex("030A020AD41903DAC4A8CE528B55C9F0BB2FEB5697BD6BBAC34B14"
                "8A020B19"));
    EXPECT_EQ(node.last_open, OpenStatus::kMalformed);
    // Subtype 0 and an unused category
    node.Open(FromHex("030A020AC000"));
    EXPECT_EQ(node.last_open, OpenStatus::kMalformed);
    node.Open(FromHex("030A020AE100"));
    EXPECT_EQ(node.last_open, OpenStatus::kMalformed);
}

TEST(FrameCodecReject, HopCounterFlagIsDropped) {
    TestNode destination(kGateway);
    const Bytes plain =
        PlainData(kGateway, kNode, MessageType::DATA, kGateway, 10, 1, kBody);
    const NetworkTrafficKeys keys = Keys1();
    destination.Open(
        RefData(keys, 1, plain, 9 | kHopCounterFlag, 0x03, keys.ge, keys.gm));
    EXPECT_EQ(destination.last_open, OpenStatus::kMalformed);
    EXPECT_EQ(destination.codec.GetStats().rx_malformed, 1u);
    EXPECT_EQ(destination.codec.GetStats().rx_auth_failed, 0u);
    EXPECT_EQ(destination.codec.GetPeers().Size(), 0u);
}

TEST(FrameCodecReject, DataFrameShorterThanItsExtension) {
    TestNode origin(kNode);
    EXPECT_TRUE(origin
                    .Seal(PlainFrame(kGateway, kNode, MessageType::DATA,
                                     FromHex("010A0A")))
                    .empty());
    EXPECT_EQ(origin.last_seal, SealStatus::kInvalidParameter);
    EXPECT_TRUE(origin
                    .Seal(PlainFrame(kGroup, kNode, MessageType::DATA_GROUP,
                                     FromHex("FFFF0A00")))
                    .empty());
    EXPECT_EQ(origin.last_seal, SealStatus::kInvalidParameter);
    // An empty body is fine
    const Bytes empty = origin.Seal(
        PlainData(kGateway, kNode, MessageType::DATA, kGateway, 10, 1, {}));
    EXPECT_EQ(empty.size(), 22u);
    TestNode destination(kGateway);
    const Bytes opened = destination.Open(empty);
    EXPECT_EQ(
        destination.OpenBody(opened),
        PlainData(kGateway, kNode, MessageType::DATA, kGateway, 10, 1, {}));
    // A received frame whose payload cannot hold the extension
    Bytes wire = FromHex("010A030A9903");
    wire.resize(6 + 3 + 12, 0);
    destination.Open(wire);
    EXPECT_EQ(destination.last_open, OpenStatus::kMalformed);
}

TEST(FrameCodecReject, MalformedPlainFrame) {
    TestNode node(kNode);
    EXPECT_TRUE(node.Seal(FromHex("FFFF030A")).empty());
    EXPECT_EQ(node.last_seal, SealStatus::kInvalidParameter);
    Bytes claim = Claim(kNode);
    claim.push_back(0);  // longer than payload_size says
    EXPECT_TRUE(node.Seal(claim).empty());
    EXPECT_EQ(node.last_seal, SealStatus::kInvalidParameter);
    // Output buffer too small
    Bytes small(9);
    size_t out_len = 99;
    EXPECT_EQ(node.codec.SealFrame(Claim(kNode), {}, small, out_len),
              SealStatus::kBufferOverflow);
    EXPECT_EQ(out_len, 0u);
    TestNode receiver(kRelay);
    TestNode sender(kGateway);
    const Bytes frame = sender.Seal(Claim(kGateway));
    OpenInfo info;
    EXPECT_EQ(receiver.codec.OpenFrame(frame, small, out_len, info),
              OpenStatus::kBufferTooSmall);
    EXPECT_EQ(receiver.codec.GetPeers().Size(), 0u);
}

// ---------------------------------------------------------------------------
// Beacon
// ---------------------------------------------------------------------------

TEST(FrameCodecBeacon, NonZeroTopDelayByteIsRefused) {
    TestNode node(kRelay);
    const uint32_t before = node.counters.GetNext(CounterSpace::kBeacon);
    Bytes beacon = Beacon(kRelay);
    beacon[6 + 11] = 0x01;
    EXPECT_TRUE(node.Seal(beacon).empty());
    EXPECT_EQ(node.last_seal, SealStatus::kInvalidParameter);
    EXPECT_EQ(node.counters.GetNext(CounterSpace::kBeacon), before);
    // The largest delay of the format still fits
    beacon[6 + 11] = 0x00;
    beacon[6 + 8] = beacon[6 + 9] = beacon[6 + 10] = 0xFF;
    const Bytes frame = node.Seal(beacon);
    EXPECT_EQ(frame.size(), 26u);
    TestNode receiver(kNode);
    EXPECT_EQ(receiver.Open(frame), beacon);
}

TEST(FrameCodecBeacon, WrongPayloadSizeIsRefused) {
    TestNode node(kRelay);
    for (const size_t size : {size_t{13}, size_t{15}, size_t{0}}) {
        EXPECT_TRUE(
            node.Seal(PlainFrame(0xFFFF, kRelay, MessageType::SYNC_BEACON,
                                 Bytes(size, 0)))
                .empty());
        EXPECT_EQ(node.last_seal, SealStatus::kInvalidParameter);
    }
    // A received beacon with another payload size, with a valid MIC
    const NetworkTrafficKeys keys = Keys1();
    Bytes forged = RefControl(
        keys, 1,
        PlainFrame(0xFFFF, kRelay, MessageType::SYNC_BEACON, Bytes(12, 0)),
        kRelay, 1, false);
    forged.pop_back();  // 7-byte trailer length for the length rule
    TestNode receiver(kNode);
    receiver.Open(forged);
    EXPECT_EQ(receiver.last_open, OpenStatus::kMalformed);
}

TEST(FrameCodecBeacon, EveryBeaconTakesANewCounter) {
    TestNode node(kRelay, 1, {7, 0, 0});
    TestNode receiver(kNode);
    for (uint32_t i = 0; i < 100; ++i) {
        const Bytes frame = node.Seal(Beacon(kRelay));
        ASSERT_EQ(frame.size(), 26u);
        EXPECT_EQ(frame, RefBeacon(Keys1(), 1, Beacon(kRelay), 7 + i));
        receiver.Open(frame);
        ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
        EXPECT_EQ(receiver.info.counter, 7 + i);
    }
    // Beacons spend only the beacon space
    EXPECT_EQ(node.counters.GetNext(CounterSpace::kControl), 0u);
    EXPECT_EQ(node.counters.GetNext(CounterSpace::kData), 0u);
}

// ---------------------------------------------------------------------------
// Replay
// ---------------------------------------------------------------------------

TEST(FrameCodecReplay, BeaconAndControlAreStrictPerTransmitter) {
    const NetworkTrafficKeys keys = Keys1();
    TestNode receiver(kGateway);
    const auto beacon = [&](AddressType source, uint32_t counter) {
        receiver.Open(RefBeacon(keys, 1, Beacon(source), counter));
        return receiver.last_open;
    };
    const auto control = [&](AddressType source, uint32_t counter) {
        receiver.Open(
            RefControl(keys, 1, Claim(source), source, counter, false));
        return receiver.last_open;
    };
    EXPECT_EQ(beacon(kRelay, 10), OpenStatus::kOk);
    EXPECT_EQ(beacon(kRelay, 10), OpenStatus::kReplay);
    EXPECT_EQ(beacon(kRelay, 9), OpenStatus::kReplay);
    EXPECT_EQ(beacon(kRelay, 11), OpenStatus::kOk);
    // The control space of the same peer is separate
    EXPECT_EQ(control(kRelay, 3), OpenStatus::kOk);
    EXPECT_EQ(control(kRelay, 3), OpenStatus::kReplay);
    EXPECT_EQ(control(kRelay, 2), OpenStatus::kReplay);
    EXPECT_EQ(control(kRelay, 1000), OpenStatus::kOk);
    EXPECT_EQ(control(kRelay, 999), OpenStatus::kReplay);
    // Another transmitter has its own state
    EXPECT_EQ(beacon(kNode, 1), OpenStatus::kOk);
    EXPECT_EQ(control(kNode, 1), OpenStatus::kOk);
    EXPECT_EQ(receiver.codec.GetStats().rx_replay, 5u);
    EXPECT_EQ(receiver.codec.GetStats().rx_opened, 6u);

    const HopReplayState* state = receiver.codec.GetPeers().Find(kRelay);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->beacon.GetLast(), 11u);
    EXPECT_EQ(state->control.GetLast(), 1000u);
    EXPECT_FALSE(state->group.HasSeenAny());
}

TEST(FrameCodecReplay, JoinFramesUseTheControlCounterOfTheTrailerTransmitter) {
    const NetworkTrafficKeys keys = Keys1();
    TestNode receiver(kGateway);
    // Two join requests of the same joiner, forwarded by two members
    const Bytes request = JoinRequest(kNode, kRelay, {});
    receiver.Open(RefControl(keys, 1, request, kRelay, 5, true));
    EXPECT_EQ(receiver.last_open, OpenStatus::kOk);
    receiver.Open(RefControl(keys, 1, request, kOther, 5, true));
    EXPECT_EQ(receiver.last_open, OpenStatus::kOk);
    receiver.Open(RefControl(keys, 1, request, kRelay, 5, true));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    // The joiner named in the header has no state: it sent nothing here
    EXPECT_EQ(receiver.codec.GetPeers().Find(kNode), nullptr);
    // A join frame and a routing table of one transmitter share C_ctl
    receiver.Open(RefControl(keys, 1, Claim(kRelay), kRelay, 5, false));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    receiver.Open(RefControl(keys, 1, Claim(kRelay), kRelay, 6, false));
    EXPECT_EQ(receiver.last_open, OpenStatus::kOk);
}

TEST(FrameCodecReplay, DataWindowAtTheDestination) {
    TestNode destination(kGateway);
    const auto open = [&](uint32_t counter) {
        destination.Open(GroupData(kGateway, kGateway, counter));
        return destination.last_open;
    };
    EXPECT_EQ(open(100), OpenStatus::kOk);
    EXPECT_EQ(open(100), OpenStatus::kReplay);
    // Nothing older than the first accepted frame
    EXPECT_EQ(open(99), OpenStatus::kReplay);
    // Shift of 31: the first frame stays in the window
    EXPECT_EQ(open(131), OpenStatus::kOk);
    EXPECT_EQ(open(100), OpenStatus::kReplay);
    // A frame that was overtaken is accepted once
    EXPECT_EQ(open(101), OpenStatus::kOk);
    EXPECT_EQ(open(101), OpenStatus::kReplay);
    // Shift of 32: only the new counter is marked
    EXPECT_EQ(open(163), OpenStatus::kOk);
    EXPECT_EQ(open(132), OpenStatus::kOk);
    EXPECT_EQ(open(131), OpenStatus::kReplay);
    // Shift of 33
    EXPECT_EQ(open(196), OpenStatus::kOk);
    EXPECT_EQ(open(165), OpenStatus::kOk);
    EXPECT_EQ(open(164), OpenStatus::kReplay);
    EXPECT_EQ(open(163), OpenStatus::kReplay);

    const HopReplayState* state = destination.codec.GetPeers().Find(kNode);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->group.GetHigh(), 196u);
    EXPECT_FALSE(state->relayed.HasSeenAny());
}

TEST(FrameCodecReplay, RelayUsesWindowGForGroupKeyedFrames) {
    TestNode relay(kRelay);
    relay.Open(GroupData(kGateway, kRelay, 40));
    EXPECT_EQ(relay.last_open, OpenStatus::kOk);
    EXPECT_EQ(relay.info.role, RxRole::kRelay);
    relay.Open(GroupData(kGateway, kRelay, 40));
    EXPECT_EQ(relay.last_open, OpenStatus::kReplay);
    const HopReplayState* state = relay.codec.GetPeers().Find(kNode);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->group.GetHigh(), 40u);
    EXPECT_FALSE(state->relayed.HasSeenAny());
}

TEST(FrameCodecReplay, RelayUsesWindowRForEndToEndFrames) {
    TestNode relay(kRelay);
    for (const MessageType type :
         {MessageType::DATA, MessageType::DATA_RELIABLE, MessageType::ACK}) {
        const uint32_t counter = 50 + static_cast<uint32_t>(type);
        relay.Open(EndToEndData(kGateway, kRelay, counter, type));
        EXPECT_EQ(relay.last_open, OpenStatus::kOk);
        EXPECT_EQ(relay.info.role, RxRole::kRelay);
        relay.Open(EndToEndData(kGateway, kRelay, counter, type));
        EXPECT_EQ(relay.last_open, OpenStatus::kReplay);
    }
    const HopReplayState* state = relay.codec.GetPeers().Find(kNode);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->relayed.HasSeenAny());
    EXPECT_FALSE(state->group.HasSeenAny());
    // Window G of the same origin is independent: the same counter value
    // of a group-keyed frame is still new
    relay.Open(GroupData(kGateway, kRelay, 50 + 0x11));
    EXPECT_EQ(relay.last_open, OpenStatus::kOk);
    EXPECT_EQ(relay.codec.GetStats().rx_replay, 3u);
}

TEST(FrameCodecReplay, DestinationUsesNoHopWindowForEndToEndFrames) {
    // A hop window is driven by the network key alone. At a destination an
    // end-to-end frame is judged only by the end-to-end window
    TestNode destination(kGateway);
    for (int i = 0; i < 2; ++i) {
        const Bytes opened =
            destination.Open(EndToEndData(kGateway, kGateway, 77));
        EXPECT_EQ(destination.last_open, OpenStatus::kOk);
        EXPECT_EQ(destination.info.role, RxRole::kDestination);
        EXPECT_TRUE(destination.info.end_to_end);
        // Without a node key for the origin the body stays closed
        EXPECT_TRUE(destination.OpenBody(opened).empty());
        EXPECT_EQ(destination.last_open, OpenStatus::kKeyUnavailable);
    }
    EXPECT_EQ(destination.codec.GetPeers().Find(kNode), nullptr);
    EXPECT_EQ(destination.codec.GetStats().rx_key_unavailable, 2u);
    EXPECT_EQ(destination.provider->node_key_calls, 0);

    // A forged group-keyed frame with the maximal counter pushes window G
    // and does not block a later end-to-end frame of the same origin
    destination.Open(GroupData(kGateway, kGateway, 0x3FFFFFFE));
    EXPECT_EQ(destination.last_open, OpenStatus::kOk);
    destination.Open(EndToEndData(kGateway, kGateway, 78));
    EXPECT_EQ(destination.last_open, OpenStatus::kOk);
}

TEST(FrameCodecReplay, KeyTypesUseNoHopWindowAtRelayOrDestination) {
    const NetworkTrafficKeys keys = Keys1();
    const NodeTrafficKeys node_keys = NodeKeys();
    for (const MessageType type :
         {MessageType::KEY_UPDATE, MessageType::KEY_UPDATE_ACK,
          MessageType::KEY_RELAY}) {
        const Bytes frame = RefData(
            keys, 1, PlainData(kGateway, kNode, type, kRelay, 10, 1, kBody),
            5 | kEndToEndFlag, 0x05, node_keys.ke, node_keys.km);
        TestNode relay(kRelay);
        for (int i = 0; i < 3; ++i) {
            relay.Open(frame);
            EXPECT_EQ(relay.last_open, OpenStatus::kOk);
            EXPECT_EQ(relay.info.role, RxRole::kRelay);
        }
        EXPECT_EQ(relay.codec.GetPeers().Size(), 0u);

        // With E = 0 a key type is not valid
        relay.Open(RefGroupData(
            keys, 1, PlainData(kGateway, kNode, type, kRelay, 10, 1, kBody),
            6));
        EXPECT_EQ(relay.last_open, OpenStatus::kE2eRequired);
        EXPECT_EQ(relay.codec.GetStats().rx_e2e_required, 1u);

        // This step cannot originate a key message
        TestNode origin(kNode);
        EXPECT_TRUE(
            origin.Seal(PlainData(kGateway, kNode, type, kRelay, 10, 1, kBody))
                .empty());
        EXPECT_EQ(origin.last_seal, SealStatus::kKeyUnavailable);
        EXPECT_EQ(origin.counters.GetNext(CounterSpace::kData), 0u);
    }
}

TEST(FrameCodecReplay, EndToEndFlagOnAFloodTypeIsDropped) {
    const NetworkTrafficKeys keys = Keys1();
    const NodeTrafficKeys node_keys = NodeKeys();
    TestNode receiver(kRelay);
    receiver.Open(RefData(keys, 1,
                          PlainData(0xFFFF, kNode, MessageType::DATA_BROADCAST,
                                    0xFFFF, 10, 1, kBody),
                          5 | kEndToEndFlag, 0x04, node_keys.ee, node_keys.em));
    EXPECT_EQ(receiver.last_open, OpenStatus::kKeyUnavailable);
    receiver.Open(RefData(keys, 1, PlainGroup(kGroup, kNode, 10, 0, 1, kBody),
                          6 | kEndToEndFlag, 0x04, node_keys.ee, node_keys.em));
    EXPECT_EQ(receiver.last_open, OpenStatus::kKeyUnavailable);
    EXPECT_EQ(receiver.codec.GetStats().rx_key_unavailable, 2u);
    EXPECT_EQ(receiver.codec.GetPeers().Size(), 0u);
}

TEST(FrameCodecReplay, AllOnesCounterIsDroppedAndClearsNothing) {
    const NetworkTrafficKeys keys = Keys1();
    TestNode receiver(kGateway);
    // Beacon
    receiver.Open(RefBeacon(keys, 1, Beacon(kRelay), 5));
    ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
    receiver.Open(RefBeacon(keys, 1, Beacon(kRelay), 0xFFFFFF));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    receiver.Open(RefBeacon(keys, 1, Beacon(kRelay), 5));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    receiver.Open(RefBeacon(keys, 1, Beacon(kRelay), 0xFFFFFE));
    EXPECT_EQ(receiver.last_open, OpenStatus::kOk);
    // Control
    receiver.Open(RefControl(keys, 1, Claim(kRelay), kRelay, 5, false));
    ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
    receiver.Open(
        RefControl(keys, 1, Claim(kRelay), kRelay, 0xFFFFFFFF, false));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    receiver.Open(RefControl(keys, 1, Claim(kRelay), kRelay, 5, false));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    // Data, group keyed and end to end, at destination and at a relay
    receiver.Open(GroupData(kGateway, kGateway, 5));
    ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
    receiver.Open(GroupData(kGateway, kGateway, 0x3FFFFFFF));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    receiver.Open(GroupData(kGateway, kGateway, 5));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    receiver.Open(EndToEndData(kGateway, kGateway, 0x3FFFFFFF));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    TestNode relay(kRelay);
    relay.Open(EndToEndData(kGateway, kRelay, 0x3FFFFFFF));
    EXPECT_EQ(relay.last_open, OpenStatus::kReplay);
    EXPECT_EQ(relay.codec.GetPeers().Size(), 0u);
    // A fresh receiver does not take the all-ones value as its first frame
    TestNode cold(kOther);
    cold.Open(RefBeacon(keys, 1, Beacon(kRelay), 0xFFFFFF));
    EXPECT_EQ(cold.last_open, OpenStatus::kReplay);
    EXPECT_EQ(cold.codec.GetPeers().Size(), 0u);
}

TEST(FrameCodecReplay, OverheardDataFrameLeavesStateUntouched) {
    TestNode origin(kNode);
    TestNode relay(kRelay);
    TestNode bystander(kOther);
    const Bytes plain =
        PlainData(kGateway, kNode, MessageType::DATA, kRelay, 10, 1, kBody);
    const Bytes hop1 = origin.Seal(plain);

    // The bystander hears the frame meant for the relay
    EXPECT_TRUE(bystander.Open(hop1).empty());
    EXPECT_EQ(bystander.last_open, OpenStatus::kOverheard);
    EXPECT_EQ(bystander.info.role, RxRole::kNone);
    EXPECT_EQ(bystander.codec.GetPeers().Size(), 0u);
    const SecurityStats stats = bystander.codec.GetStats();
    EXPECT_EQ(stats.rx_opened, 0u);
    EXPECT_EQ(stats.rx_replay, 0u);
    EXPECT_EQ(stats.rx_auth_failed, 0u);

    // The same origin counter then arrives from the proper relay
    const Bytes opened = relay.Open(hop1);
    ASSERT_EQ(relay.last_open, OpenStatus::kOk);
    const Bytes hop2 = relay.Forward(opened, kOther);
    const Bytes at_bystander = bystander.Open(hop2);
    ASSERT_EQ(bystander.last_open, OpenStatus::kOk);
    EXPECT_EQ(bystander.info.role, RxRole::kRelay);

    // Even the destination drops a copy that names another next hop
    TestNode gateway(kGateway);
    EXPECT_TRUE(gateway.Open(hop1).empty());
    EXPECT_EQ(gateway.last_open, OpenStatus::kOverheard);
    EXPECT_EQ(gateway.codec.GetPeers().Size(), 0u);

    // An overheard end-to-end frame touches window R of no one
    bystander.Open(EndToEndData(kGateway, kRelay, 9));
    EXPECT_EQ(bystander.last_open, OpenStatus::kOverheard);
    const HopReplayState* state = bystander.codec.GetPeers().Find(kNode);
    ASSERT_NE(state, nullptr);
    EXPECT_FALSE(state->relayed.HasSeenAny());
}

TEST(FrameCodecReplay, FloodCopyThatArrivesTwiceEndsAtTheHopWindow) {
    TestNode origin(kNode);
    TestNode first(kRelay);
    TestNode second(kOther);
    TestNode listener(kGateway);
    const Bytes frame = origin.Seal(PlainGroup(kGroup, kNode, 10, 0, 1, kBody));
    // The listener hears the origin and then two relayed copies
    const Bytes direct = listener.Open(frame);
    ASSERT_EQ(listener.last_open, OpenStatus::kOk);
    EXPECT_EQ(listener.info.role, RxRole::kFlood);
    EXPECT_EQ(listener.OpenBody(direct),
              PlainGroup(kGroup, kNode, 10, 0, 1, kBody));
    for (TestNode* relay : {&first, &second}) {
        const Bytes opened = relay->Open(frame);
        ASSERT_EQ(relay->last_open, OpenStatus::kOk);
        listener.Open(relay->Forward(opened, 0xFFFF));
        EXPECT_EQ(listener.last_open, OpenStatus::kReplay);
    }
    EXPECT_EQ(listener.codec.GetStats().rx_replay, 2u);
}

TEST(FrameCodecReplay, NewerEpochClearsThePeerAndOlderEpochIsDropped) {
    TestNode receiver(kGateway, 1, {}, {{1, NetKey1()}, {2, NetKey2()}});
    const NetworkTrafficKeys keys1 = Keys1();
    const NetworkTrafficKeys keys2 = RefNetKeys(NetKey2(), 2);
    receiver.Open(RefBeacon(keys1, 1, Beacon(kRelay), 500));
    ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
    receiver.Open(GroupData(kGateway, kGateway, 500));
    ASSERT_EQ(receiver.last_open, OpenStatus::kOk);

    // The counters restart in the new epoch
    receiver.Open(RefBeacon(keys2, 2, Beacon(kRelay), 0));
    EXPECT_EQ(receiver.last_open, OpenStatus::kOk);
    EXPECT_EQ(receiver.info.epoch, 2);
    receiver.Open(RefBeacon(keys2, 2, Beacon(kRelay), 0));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    // A frame of that peer under the epoch it left is dropped
    receiver.Open(RefBeacon(keys1, 1, Beacon(kRelay), 501));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    receiver.Open(RefControl(keys1, 1, Claim(kRelay), kRelay, 1, false));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    EXPECT_EQ(receiver.codec.GetPeers().Find(kRelay)->epoch, 2);

    // Another peer is still in the old epoch and is accepted there
    receiver.Open(GroupData(kGateway, kGateway, 501));
    EXPECT_EQ(receiver.last_open, OpenStatus::kOk);
    receiver.Open(GroupData(kGateway, kGateway, 3, 2, NetKey2()));
    EXPECT_EQ(receiver.last_open, OpenStatus::kOk);
    receiver.Open(GroupData(kGateway, kGateway, 502));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
}

TEST(FrameCodecReplay, PeerTableReplacesTheLeastRecentlyHeardUnroutedPeer) {
    const NetworkTrafficKeys keys = Keys1();
    TestNode receiver(kGateway);  // capacity of 8 peers
    for (AddressType peer = 0x0B01; peer <= 0x0B08; ++peer) {
        receiver.Open(RefBeacon(keys, 1, Beacon(peer), 100));
        ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
    }
    EXPECT_EQ(receiver.codec.GetPeers().Size(), 8u);
    // 0x0B01 is the oldest. It is in the routing table and stays
    receiver.codec.GetPeers().SetIsRouted(
        [](AddressType peer) { return peer == 0x0B01; });
    receiver.Open(RefBeacon(keys, 1, Beacon(0x0B09), 100));
    ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
    EXPECT_EQ(receiver.codec.GetPeers().Size(), 8u);
    EXPECT_NE(receiver.codec.GetPeers().Find(0x0B01), nullptr);
    EXPECT_EQ(receiver.codec.GetPeers().Find(0x0B02), nullptr);
    EXPECT_NE(receiver.codec.GetPeers().Find(0x0B09), nullptr);
    // The routed peer kept its counter
    receiver.Open(RefBeacon(keys, 1, Beacon(0x0B01), 100));
    EXPECT_EQ(receiver.last_open, OpenStatus::kReplay);
    // A dropped frame of an unknown peer takes no entry
    receiver.Open(RefBeacon(keys, 1, Beacon(0x0B0A), 0xFFFFFF));
    EXPECT_EQ(receiver.codec.GetPeers().Find(0x0B0A), nullptr);
    EXPECT_NE(receiver.codec.GetPeers().Find(0x0B03), nullptr);
}

// ---------------------------------------------------------------------------
// Relay
// ---------------------------------------------------------------------------

TEST(FrameCodecRelay, ForwardsTheReceivedBytesAndRewritesOnlyRoutingFields) {
    TestNode origin(kNode);
    TestNode relay(kRelay);
    TestNode destination(kGateway);
    const Bytes plain =
        PlainData(kGateway, kNode, MessageType::DATA_RELIABLE, kRelay, 10, 0x21,
                  Concat({FromHex("0711223344"), kBody}));
    const Bytes hop1 = origin.Seal(plain);
    const Bytes opened = relay.Open(hop1);
    ASSERT_EQ(relay.last_open, OpenStatus::kOk);
    const uint32_t data_before = relay.counters.GetNext(CounterSpace::kData);
    const Bytes hop2 = relay.Forward(opened, kGateway);
    ASSERT_EQ(relay.last_seal, SealStatus::kOk);
    EXPECT_EQ(relay.counters.GetNext(CounterSpace::kData), data_before);

    ASSERT_EQ(hop2.size(), hop1.size());
    // Header, ciphertext, counter field and tag are byte for byte the same
    EXPECT_EQ(Bytes(hop2.begin(), hop2.begin() + 6),
              Bytes(hop1.begin(), hop1.begin() + 6));
    EXPECT_EQ(Bytes(hop2.begin() + 10, hop2.end() - 4),
              Bytes(hop1.begin() + 10, hop1.end() - 4));
    // The hop MIC is new
    EXPECT_NE(Bytes(hop2.end() - 4, hop2.end()),
              Bytes(hop1.end() - 4, hop1.end()));

    const Bytes at_destination = destination.Open(hop2);
    ASSERT_EQ(destination.last_open, OpenStatus::kOk);
    EXPECT_EQ(destination.info.context, relay.info.context);
    const Bytes delivered = destination.OpenBody(at_destination);
    ASSERT_EQ(destination.last_open, OpenStatus::kOk);
    EXPECT_EQ(delivered,
              PlainData(kGateway, kNode, MessageType::DATA_RELIABLE, kGateway,
                        9, 0x21, Concat({FromHex("0711223344"), kBody})));
}

TEST(FrameCodecRelay, VerifyHopReadsAndChangesNoState) {
    TestNode origin(kNode);
    TestNode relay(kRelay);
    const Bytes hop1 = origin.Seal(
        PlainData(kGateway, kNode, MessageType::DATA, kRelay, 10, 1, kBody));
    Bytes out(kMaxPlainFrameSize);
    size_t out_len = 0;
    OpenInfo info;
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(relay.codec.VerifyHop(hop1, out, out_len, info),
                  OpenStatus::kOk);
        EXPECT_EQ(info.context.body_state, BodyState::kCipher);
        EXPECT_EQ(info.role, RxRole::kNone);
    }
    EXPECT_EQ(relay.codec.GetPeers().Size(), 0u);
    Bytes bad = hop1;
    bad[7] ^= 1;
    EXPECT_EQ(relay.codec.VerifyHop(bad, out, out_len, info),
              OpenStatus::kAuthFailed);
    EXPECT_EQ(out_len, 0u);
    // It is a data frame check only
    TestNode sender(kGateway);
    EXPECT_EQ(
        relay.codec.VerifyHop(sender.Seal(Claim(kGateway)), out, out_len, info),
        OpenStatus::kMalformed);
}

TEST(FrameCodecRelay, RewriteForForward) {
    Bytes data =
        PlainData(kGateway, kNode, MessageType::DATA, kRelay, 10, 0x21, kBody);
    const Bytes before = data;
    ASSERT_TRUE(FrameCodec::RewriteForForward(data, kOther));
    EXPECT_EQ(data, PlainData(kGateway, kNode, MessageType::DATA, kOther, 9,
                              0x21, kBody));
    // A flood frame keeps its next hop
    Bytes group = PlainGroup(kGroup, kNode, 2, 0x01, 7, kBody);
    ASSERT_TRUE(FrameCodec::RewriteForForward(group, kOther));
    EXPECT_EQ(group, PlainGroup(kGroup, kNode, 1, 0x01, 7, kBody));
    // ttl of 1 means the frame ends here
    EXPECT_FALSE(FrameCodec::RewriteForForward(group, kOther));
    EXPECT_EQ(group, PlainGroup(kGroup, kNode, 1, 0x01, 7, kBody));
    Bytes spent =
        PlainData(kGateway, kNode, MessageType::DATA, kRelay, 0, 1, kBody);
    EXPECT_FALSE(FrameCodec::RewriteForForward(spent, kOther));
    // No data frame, too short, or a wire type byte
    Bytes claim = Claim(kNode);
    EXPECT_FALSE(FrameCodec::RewriteForForward(claim, kOther));
    Bytes cut(before.begin(), before.begin() + 8);
    EXPECT_FALSE(FrameCodec::RewriteForForward(cut, kOther));
    Bytes wire = before;
    wire[4] = 0x99;
    EXPECT_FALSE(FrameCodec::RewriteForForward(wire, kOther));
}

TEST(FrameCodecRelay, NoPathEncryptsABodyUnderAnotherNodesCounter) {
    TestNode relay(kRelay);
    const Bytes foreign =
        PlainData(kGateway, kNode, MessageType::DATA, kGateway, 9, 1, kBody);
    const uint32_t data_before = relay.counters.GetNext(CounterSpace::kData);
    // A plain body of another source through the general entry point
    EXPECT_TRUE(relay.Seal(foreign).empty());
    EXPECT_EQ(relay.last_seal, SealStatus::kInvalidState);
    // And through the relay entry point
    Bytes out(kMaxSealedFrameSize);
    size_t out_len = 0;
    EXPECT_EQ(
        relay.codec.SealForwarded(foreign, SecurityContext{}, out, out_len),
        SealStatus::kInvalidState);
    EXPECT_EQ(relay.counters.GetNext(CounterSpace::kData), data_before);

    // A decrypted delivery copy cannot be forwarded either
    TestNode origin(kNode);
    TestNode flood_relay(kOther);
    const auto received = flood_relay.codec.Open(origin.Seal(PlainData(
        0xFFFF, kNode, MessageType::DATA_BROADCAST, 0xFFFF, 10, 1, kBody)));
    ASSERT_TRUE(received.has_value());
    const auto copy = flood_relay.codec.OpenBody(*received);
    ASSERT_TRUE(copy.has_value());
    EXPECT_EQ(copy->GetSecurityContext().body_state, BodyState::kPlain);
    EXPECT_EQ(received->GetSecurityContext().body_state, BodyState::kCipher);
    EXPECT_EQ(flood_relay.codec.Seal(*copy, out, out_len),
              SealStatus::kInvalidState);
    EXPECT_EQ(flood_relay.codec.Seal(*received, out, out_len), SealStatus::kOk);
    // The opened body cannot be opened twice
    OpenStatus status = OpenStatus::kOk;
    EXPECT_FALSE(flood_relay.codec.OpenBody(*copy, &status).has_value());
    EXPECT_EQ(status, OpenStatus::kInvalidState);

    // The relay entry point refuses a frame of this node, and the H flag
    SecurityContext cipher_context = received->GetSecurityContext();
    EXPECT_EQ(
        relay.codec.SealForwarded(PlainData(kGateway, kRelay, MessageType::DATA,
                                            kGateway, 9, 1, kBody),
                                  cipher_context, out, out_len),
        SealStatus::kInvalidParameter);
    cipher_context.ctr_field |= kHopCounterFlag;
    EXPECT_EQ(relay.codec.SealForwarded(foreign, cipher_context, out, out_len),
              SealStatus::kInvalidParameter);
}

TEST(FrameCodecRelay, RelayChangingAFieldItMayNotChangeFailsAtTheBody) {
    // A relay holds the network key, so it can always produce a valid hop
    // layer. What it may not change is covered by the body tag
    const NetworkTrafficKeys keys = Keys1();
    TestNode origin(kNode);
    const Bytes plain = PlainGroup(kGroup, kNode, 10, 0x00, 0x21, kBody);
    const Bytes genuine = origin.Seal(plain);
    TestNode relay(kRelay);
    const Bytes opened = relay.Open(genuine);
    ASSERT_EQ(relay.last_open, OpenStatus::kOk);
    const Bytes ctr = Le32(relay.info.context.ctr_field);
    const Bytes tag(relay.info.context.tag.begin(),
                    relay.info.context.tag.end());

    struct Change {
        const char* what;
        size_t offset;  // in the opened frame
    };

    // destination, source, payload stays, flags, seq_num, first body byte
    for (const Change change :
         {Change{"destination", 0}, Change{"source", 2}, Change{"flags", 9},
          Change{"seq_num", 10}, Change{"body", 11},
          Change{"last body byte", opened.size() - 1}}) {
        Bytes tampered = opened;
        tampered[change.offset] ^= 0x01;
        Bytes header(tampered.begin(), tampered.begin() + 6);
        header[4] = 0x9C;  // DATA_GROUP, secured, epoch 1
        const Bytes forged = RefHopLayer(
            keys, header, Bytes(tampered.begin() + 6, tampered.begin() + 11),
            Bytes(tampered.begin() + 11, tampered.end()), ctr, tag);
        TestNode receiver(kGateway);
        const Bytes hop_ok = receiver.Open(forged);
        ASSERT_EQ(receiver.last_open, OpenStatus::kOk) << change.what;
        EXPECT_TRUE(receiver.OpenBody(hop_ok).empty()) << change.what;
        EXPECT_EQ(receiver.last_open, OpenStatus::kTagFailed) << change.what;
    }
    // The type and the epoch bit are in the header the tag covers
    {
        Bytes header(opened.begin(), opened.begin() + 6);
        header[4] = 0x9C;
        TestNode receiver(kGateway, 1, {}, {{1, NetKey1()}, {2, NetKey2()}});
        const NetworkTrafficKeys keys2 = RefNetKeys(NetKey2(), 2);
        header[4] = 0x94;  // epoch parity changed
        const Bytes forged = RefHopLayer(
            keys2, header, Bytes(opened.begin() + 6, opened.begin() + 11),
            Bytes(opened.begin() + 11, opened.end()), ctr, tag);
        const Bytes hop_ok = receiver.Open(forged);
        ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
        EXPECT_TRUE(receiver.OpenBody(hop_ok).empty());
        EXPECT_EQ(receiver.last_open, OpenStatus::kTagFailed);
    }
    // The counter field and the tag
    for (const size_t index : {size_t{0}, size_t{3}}) {
        Bytes bad_ctr = ctr;
        Bytes bad_tag = tag;
        if (index == 0) {
            bad_ctr[0] ^= 0x01;
        } else {
            bad_tag[3] ^= 0x80;
        }
        Bytes header(opened.begin(), opened.begin() + 6);
        header[4] = 0x9C;
        TestNode receiver(kGateway);
        const Bytes hop_ok = receiver.Open(RefHopLayer(
            keys, header, Bytes(opened.begin() + 6, opened.begin() + 11),
            Bytes(opened.begin() + 11, opened.end()), bad_ctr, bad_tag));
        ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
        EXPECT_TRUE(receiver.OpenBody(hop_ok).empty());
        EXPECT_EQ(receiver.last_open, OpenStatus::kTagFailed);
    }
    // next_hop and ttl are the fields a relay may change
    {
        Bytes allowed = opened;
        allowed[8] = 3;  // ttl
        Bytes header(allowed.begin(), allowed.begin() + 6);
        header[4] = 0x9C;
        TestNode receiver(kGateway);
        const Bytes hop_ok = receiver.Open(RefHopLayer(
            keys, header, Bytes(allowed.begin() + 6, allowed.begin() + 11),
            Bytes(allowed.begin() + 11, allowed.end()), ctr, tag));
        ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
        EXPECT_EQ(receiver.OpenBody(hop_ok),
                  PlainGroup(kGroup, kNode, 3, 0x00, 0x21, kBody));
    }
}

TEST(FrameCodecRelay, ForwardsUnderTheEpochTheFrameArrivedWith) {
    // The relay already transmits under epoch 2 and still holds epoch 1
    TestNode relay(kRelay, 2, {}, {{1, NetKey1()}, {2, NetKey2()}});
    TestNode origin(kNode);
    TestNode destination(kGateway);
    const Bytes opened = relay.Open(origin.Seal(
        PlainData(kGateway, kNode, MessageType::DATA, kRelay, 10, 1, kBody)));
    ASSERT_EQ(relay.last_open, OpenStatus::kOk);
    EXPECT_EQ(relay.info.context.epoch, 1);
    const Bytes hop2 = relay.Forward(opened, kGateway);
    ASSERT_EQ(relay.last_seal, SealStatus::kOk);
    EXPECT_EQ(hop2[4], 0x99);  // DATA, secured, epoch parity 1
    const Bytes at_destination = destination.Open(hop2);
    ASSERT_EQ(destination.last_open, OpenStatus::kOk);
    EXPECT_FALSE(destination.OpenBody(at_destination).empty());
    // Its own frames go out under its transmit epoch
    EXPECT_EQ(relay.Seal(Claim(kRelay))[4], 0xC7);
    EXPECT_EQ(relay.codec.GetStats().tx_epoch, 2);

    // Once the old epoch is gone the frame can no longer be forwarded
    relay.provider->SetKeys({{2, NetKey2()}});
    EXPECT_EQ(relay.codec.LoadKeys(), 1u);
    EXPECT_TRUE(relay.Forward(opened, kGateway).empty());
    EXPECT_EQ(relay.last_seal, SealStatus::kKeyUnavailable);
}

TEST(FrameCodecRelay, TypedMessagesCarryTheContextThroughCreateForwarded) {
    TestNode origin(kNode);
    Bytes out(kMaxSealedFrameSize);
    size_t out_len = 0;

    // DATA through DataMessage
    {
        TestNode relay(kRelay);
        const Bytes hop1 = origin.Seal(PlainData(
            kGateway, kNode, MessageType::DATA, kRelay, 10, 1, kBody));
        const auto received = relay.codec.Open(hop1);
        ASSERT_TRUE(received.has_value());
        const auto message = DataMessage::CreateFromBaseMessage(*received);
        ASSERT_TRUE(message.has_value());
        const auto forwarded = DataMessage::CreateForwarded(*message, kGateway);
        ASSERT_TRUE(forwarded.has_value());
        ASSERT_EQ(relay.codec.Seal(forwarded->ToBaseMessage(), out, out_len),
                  SealStatus::kOk);
        const Bytes hop2(out.begin(), out.begin() + out_len);
        EXPECT_EQ(Bytes(hop2.begin() + 10, hop2.end() - 4),
                  Bytes(hop1.begin() + 10, hop1.end() - 4));
        // A message the application builds has no received ciphertext
        const auto fresh = DataMessage::Create(kGateway, kNode, kGateway, kBody,
                                               5, 1, MessageType::DATA);
        EXPECT_EQ(fresh->GetSecurityContext(), SecurityContext{});
        EXPECT_EQ(relay.codec.Seal(fresh->ToBaseMessage(), out, out_len),
                  SealStatus::kInvalidState);
    }
    // DATA_BROADCAST through BroadcastMessage
    {
        TestNode relay(kRelay);
        const Bytes hop1 = origin.Seal(PlainData(
            0xFFFF, kNode, MessageType::DATA_BROADCAST, 0xFFFF, 10, 2, kBody));
        const auto received = relay.codec.Open(hop1);
        ASSERT_TRUE(received.has_value());
        const auto message = BroadcastMessage::CreateFromBaseMessage(*received);
        ASSERT_TRUE(message.has_value());
        const auto forwarded = BroadcastMessage::CreateForwarded(*message);
        ASSERT_TRUE(forwarded.has_value());
        EXPECT_EQ(forwarded->GetSecurityContext(),
                  received->GetSecurityContext());
        ASSERT_EQ(relay.codec.Seal(forwarded->ToBaseMessage(), out, out_len),
                  SealStatus::kOk);
        const Bytes hop2(out.begin(), out.begin() + out_len);
        EXPECT_EQ(Bytes(hop2.begin() + 10, hop2.end() - 4),
                  Bytes(hop1.begin() + 10, hop1.end() - 4));
        // Delivery reads the decrypted copy
        const auto copy = relay.codec.OpenBody(*received);
        ASSERT_TRUE(copy.has_value());
        EXPECT_EQ(BroadcastMessage::CreateFromBaseMessage(*copy)->GetPayload(),
                  kBody);
    }
    // DATA_GROUP through GroupMessage
    {
        TestNode relay(kRelay);
        const Bytes hop1 =
            origin.Seal(PlainGroup(kGroup, kNode, 10, 0x01, 3, kBody));
        const auto received = relay.codec.Open(hop1);
        ASSERT_TRUE(received.has_value());
        const auto message = GroupMessage::CreateFromBaseMessage(*received);
        ASSERT_TRUE(message.has_value());
        EXPECT_TRUE(message->RequestAcks());
        const auto forwarded = GroupMessage::CreateForwarded(*message);
        ASSERT_TRUE(forwarded.has_value());
        ASSERT_EQ(relay.codec.Seal(forwarded->ToBaseMessage(), out, out_len),
                  SealStatus::kOk);
        const Bytes hop2(out.begin(), out.begin() + out_len);
        EXPECT_EQ(Bytes(hop2.begin() + 11, hop2.end() - 4),
                  Bytes(hop1.begin() + 11, hop1.end() - 4));
        TestNode listener(kGateway);
        const auto second = listener.codec.Open(hop2);
        ASSERT_TRUE(second.has_value());
        const auto copy = listener.codec.OpenBody(*second);
        ASSERT_TRUE(copy.has_value());
        const auto delivered = GroupMessage::CreateFromBaseMessage(*copy);
        EXPECT_EQ(delivered->GetPayload(), kBody);
        EXPECT_EQ(delivered->GetTTL(), 9);
    }
}

// ---------------------------------------------------------------------------
// Counters and keys at the sealing side
// ---------------------------------------------------------------------------

TEST(FrameCodecSeal, CounterProblemsRefuseTheFrameAndRepeatNothing) {
    TestNode node(kNode);
    const Bytes plain =
        PlainData(kGateway, kNode, MessageType::DATA, kGateway, 10, 1, kBody);
    Bytes out(kMaxSealedFrameSize);
    size_t out_len = 0;
    // The first reservation covers one value. Without a refill the next
    // frame waits
    ASSERT_EQ(node.codec.SealFrame(plain, {}, out, out_len), SealStatus::kOk);
    EXPECT_EQ(Bytes(out.begin(), out.begin() + out_len),
              RefGroupData(Keys1(), 1, plain, 0));
    node.store->SetSaveFailure(true);
    EXPECT_EQ(node.codec.SealFrame(plain, {}, out, out_len),
              SealStatus::kCounterStoreError);
    EXPECT_EQ(out_len, 0u);
    EXPECT_EQ(node.counters.GetNext(CounterSpace::kData), 1u);
    node.store->SetSaveFailure(false);
    EXPECT_EQ(node.Seal(plain), RefGroupData(Keys1(), 1, plain, 1));

    // Captured for deep sleep: sealing is locked until the capture is
    // discarded
    node.counters.Capture();
    EXPECT_EQ(node.codec.SealFrame(plain, {}, out, out_len),
              SealStatus::kInvalidState);
    node.counters.DiscardCapture();
    EXPECT_EQ(node.Seal(plain), RefGroupData(Keys1(), 1, plain, 2));
    EXPECT_EQ(node.codec.GetStats().tx_sealed, 3u);
    EXPECT_EQ(node.codec.GetStats().tx_refused, 2u);
    EXPECT_GT(node.codec.GetStats().counter_store_writes, 0u);
}

TEST(FrameCodecSeal, ExhaustedSpaceAndStoppedCounters) {
    DefaultCryptoBackend backend;
    auto provider = std::make_shared<StaticKeyProvider>(
        std::vector<NetworkKey>{{1, NetKey1()}});
    CounterRecord record;
    record.tx_epoch = 1;
    record.ceiling[2] = 1;  // one data counter value in this epoch
    auto store = std::make_shared<MemoryCounterStore>(record);
    FrameCounters counters(store);
    FrameCodecConfig config;
    config.mode = SecurityMode::kSecured;
    config.node_address = kNode;
    FrameCodec codec(config, backend, provider, counters);
    codec.LoadKeys();
    const Bytes plain =
        PlainData(kGateway, kNode, MessageType::DATA, kGateway, 10, 1, kBody);
    Bytes out(kMaxSealedFrameSize);
    size_t out_len = 0;
    // Counters not started
    EXPECT_EQ(codec.SealFrame(plain, {}, out, out_len),
              SealStatus::kInvalidState);
    ASSERT_EQ(counters.Start(), CounterStatus::kOk);
    ASSERT_EQ(counters.EnsureReserved(CounterSpace::kData), CounterStatus::kOk);
    EXPECT_EQ(codec.SealFrame(plain, {}, out, out_len), SealStatus::kOk);
    EXPECT_EQ(codec.SealFrame(plain, {}, out, out_len),
              SealStatus::kCounterExhausted);
}

TEST(FrameCodecSeal, NoKeyForTheTransmitEpoch) {
    // The counter record says epoch 2 and the provider only has epoch 1
    TestNode node(kNode, 2, {}, {{1, NetKey1()}});
    EXPECT_TRUE(node.Seal(Beacon(kNode)).empty());
    EXPECT_EQ(node.last_seal, SealStatus::kKeyUnavailable);
    EXPECT_TRUE(node.Seal(Claim(kNode)).empty());
    EXPECT_EQ(node.last_seal, SealStatus::kKeyUnavailable);
    EXPECT_TRUE(node.Seal(PlainData(kGateway, kNode, MessageType::DATA,
                                    kGateway, 10, 1, kBody))
                    .empty());
    EXPECT_EQ(node.last_seal, SealStatus::kKeyUnavailable);
    for (const CounterSpace space :
         {CounterSpace::kBeacon, CounterSpace::kControl, CounterSpace::kData}) {
        EXPECT_EQ(node.counters.GetNext(space), 0u);
    }
    // Without any key the node cannot open anything either
    node.codec.InvalidateKeys();
    TestNode sender(kRelay);
    node.Open(sender.Seal(Beacon(kRelay)));
    EXPECT_EQ(node.last_open, OpenStatus::kUnknownEpoch);
}

TEST(FrameCodecSeal, FramesOfTheSameContentDifferByTheirCounter) {
    TestNode node(kNode);
    const Bytes plain =
        PlainData(kGateway, kNode, MessageType::DATA, kGateway, 10, 1, kBody);
    const Bytes first = node.Seal(plain);
    const Bytes second = node.Seal(plain);
    EXPECT_NE(first, second);
    // The body is encrypted: no plain byte run survives
    EXPECT_NE(Bytes(first.begin() + 10, first.begin() + 20), kBody);
    // The routing bytes are masked
    EXPECT_NE(Bytes(first.begin() + 6, first.begin() + 10),
              Bytes(plain.begin() + 6, plain.begin() + 10));
    EXPECT_NE(Bytes(first.begin() + 6, first.begin() + 10),
              Bytes(second.begin() + 6, second.begin() + 10));
}

// ---------------------------------------------------------------------------
// Join nonce
// ---------------------------------------------------------------------------

TEST(FrameCodecJoin, JoinerAcceptsItsLastTwoNoncesOnly) {
    TestNode joiner(kNode);
    TestNode sponsor(kRelay);
    const Bytes nonces[] = {FromHex("00000001"), FromHex("00000002"),
                            FromHex("00000003")};
    Bytes responses[3];
    for (size_t i = 0; i < 3; ++i) {
        sponsor.Open(joiner.Seal(JoinRequest(kNode, kRelay, nonces[i])));
        ASSERT_EQ(sponsor.last_open, OpenStatus::kOk);
        responses[i] = sponsor.Seal(JoinResponse(kNode));
    }
    EXPECT_EQ(joiner.codec.GetOwnJoinNonceCount(), 2u);
    // Bound to the request before the previous one: rejected
    joiner.Open(responses[0]);
    EXPECT_EQ(joiner.last_open, OpenStatus::kAuthFailed);
    // Bound to the previous request: accepted (a retry does not void a
    // response in flight)
    joiner.Open(responses[1]);
    EXPECT_EQ(joiner.last_open, OpenStatus::kOk);
    joiner.Open(responses[2]);
    EXPECT_EQ(joiner.last_open, OpenStatus::kOk);
    EXPECT_EQ(joiner.codec.GetStats().rx_auth_failed, 1u);
    EXPECT_EQ(joiner.codec.GetStats().rx_join_unverified, 0u);
}

TEST(FrameCodecJoin, ResponseWithoutTheNonceFailsAtTheJoiner) {
    TestNode joiner(kNode);
    joiner.Seal(JoinRequest(kNode, kRelay, FromHex("C0FFEE42")));
    // A member that never heard the request seals without a nonce
    TestNode member(kOther);
    joiner.Open(member.Seal(JoinResponse(kNode)));
    EXPECT_EQ(joiner.last_open, OpenStatus::kAuthFailed);
    EXPECT_EQ(joiner.codec.GetPeers().Size(), 0u);
    // A recorded response bound to an older nonce fails as well
    joiner.Open(RefControl(Keys1(), 1, JoinResponse(kNode), kRelay, 9, true,
                           FromHex("DEADBEEF")));
    EXPECT_EQ(joiner.last_open, OpenStatus::kAuthFailed);
    // Bound to the right nonce it is accepted
    joiner.Open(RefControl(Keys1(), 1, JoinResponse(kNode), kRelay, 9, true,
                           FromHex("C0FFEE42")));
    EXPECT_EQ(joiner.last_open, OpenStatus::kOk);
}

TEST(FrameCodecJoin, AfterJoiningTheNodeVerifiesWithoutANonce) {
    TestNode node(kNode);
    node.Seal(JoinRequest(kNode, kRelay, FromHex("C0FFEE42")));
    node.codec.ClearOwnJoinNonces();
    EXPECT_EQ(node.codec.GetOwnJoinNonceCount(), 0u);
    // A response relayed toward this node as a sponsor has no bound nonce
    TestNode member(kOther);
    node.Open(member.Seal(JoinResponse(kNode)));
    EXPECT_EQ(node.last_open, OpenStatus::kOk);
}

TEST(FrameCodecJoin, ResealingTheSameRequestKeepsThePreviousNonce) {
    TestNode joiner(kNode);
    joiner.Seal(JoinRequest(kNode, kRelay, FromHex("00000001")));
    joiner.Seal(JoinRequest(kNode, kRelay, FromHex("00000002")));
    joiner.Seal(JoinRequest(kNode, kRelay, FromHex("00000002")));
    joiner.Open(RefControl(Keys1(), 1, JoinResponse(kNode), kRelay, 1, true,
                           FromHex("00000001")));
    EXPECT_EQ(joiner.last_open, OpenStatus::kOk);
    // A request without a nonce, and a request this node only forwards,
    // record nothing
    TestNode other(kOther);
    other.Seal(JoinRequest(kOther, kRelay, {}));
    other.Seal(JoinRequest(kNode, kRelay, FromHex("00000009")));
    EXPECT_EQ(other.codec.GetOwnJoinNonceCount(), 0u);
}

TEST(FrameCodecJoin, OverhearerCountsAnUnverifiableResponseApart) {
    TestNode joiner(kNode);
    TestNode sponsor(kRelay);
    TestNode overhearer(kOther);
    sponsor.Open(joiner.Seal(JoinRequest(kNode, kRelay, FromHex("C0FFEE42"))));
    const Bytes response = sponsor.Seal(JoinResponse(kNode));
    // The overhearer cannot know the nonce of a final response
    EXPECT_TRUE(overhearer.Open(response).empty());
    EXPECT_EQ(overhearer.last_open, OpenStatus::kJoinUnverified);
    const SecurityStats stats = overhearer.codec.GetStats();
    EXPECT_EQ(stats.rx_join_unverified, 1u);
    EXPECT_EQ(stats.rx_auth_failed, 0u);
    EXPECT_EQ(overhearer.codec.GetPeers().Size(), 0u);
    // A response on its way to the sponsor carries no nonce: every
    // receiver verifies it
    TestNode manager(kGateway);
    const Bytes toward_sponsor = manager.Seal(JoinResponse(kRelay));
    overhearer.Open(toward_sponsor);
    EXPECT_EQ(overhearer.last_open, OpenStatus::kOk);
    sponsor.Open(toward_sponsor);
    EXPECT_EQ(sponsor.last_open, OpenStatus::kOk);
}

TEST(FrameCodecJoin, SponsorStoresOnlyRequestsHeardFromTheJoinerItself) {
    const NetworkTrafficKeys keys = Keys1();
    TestNode sponsor(kRelay);
    // Forwarded by another member: the trailer transmitter is not the joiner
    sponsor.Open(RefControl(keys, 1,
                            JoinRequest(kNode, kRelay, FromHex("C0FFEE42")),
                            kOther, 1, true));
    ASSERT_EQ(sponsor.last_open, OpenStatus::kOk);
    EXPECT_EQ(sponsor.Seal(JoinResponse(kNode)),
              RefControl(keys, 1, JoinResponse(kNode), kRelay, 0, true));
    // Heard from the joiner, with another member as its first hop
    sponsor.Open(RefControl(keys, 1,
                            JoinRequest(kNode, kOther, FromHex("C0FFEE42")),
                            kNode, 1, true));
    ASSERT_EQ(sponsor.last_open, OpenStatus::kOk);
    EXPECT_EQ(sponsor.Seal(JoinResponse(kNode)),
              RefControl(keys, 1, JoinResponse(kNode), kRelay, 1, true));
    // Heard from the joiner and addressed to this member: the nonce is bound
    sponsor.Open(RefControl(keys, 1,
                            JoinRequest(kNode, kRelay, FromHex("C0FFEE42")),
                            kNode, 2, true));
    ASSERT_EQ(sponsor.last_open, OpenStatus::kOk);
    EXPECT_EQ(sponsor.Seal(JoinResponse(kNode)),
              RefControl(keys, 1, JoinResponse(kNode), kRelay, 2, true,
                         FromHex("C0FFEE42")));
    // A response for another destination is sealed without a nonce
    EXPECT_EQ(sponsor.Seal(JoinResponse(kOther)),
              RefControl(keys, 1, JoinResponse(kOther), kRelay, 3, true));
    // A newer request of the same joiner replaces the nonce
    sponsor.Open(RefControl(keys, 1,
                            JoinRequest(kNode, kRelay, FromHex("0A0B0C0D")),
                            kNode, 3, true));
    EXPECT_EQ(sponsor.Seal(JoinResponse(kNode)),
              RefControl(keys, 1, JoinResponse(kNode), kRelay, 4, true,
                         FromHex("0A0B0C0D")));
    // A replayed request does not bring the old nonce back
    sponsor.Open(RefControl(keys, 1,
                            JoinRequest(kNode, kRelay, FromHex("C0FFEE42")),
                            kNode, 2, true));
    EXPECT_EQ(sponsor.last_open, OpenStatus::kReplay);
    EXPECT_EQ(sponsor.Seal(JoinResponse(kNode)),
              RefControl(keys, 1, JoinResponse(kNode), kRelay, 5, true,
                         FromHex("0A0B0C0D")));
}

TEST(FrameCodecJoin, StoredNonceExpiresAfterTheJoiningTimeout) {
    const NetworkTrafficKeys keys = Keys1();
    TestNode sponsor(kRelay);
    sponsor.now_ms = 1000;
    sponsor.Open(RefControl(keys, 1,
                            JoinRequest(kNode, kRelay, FromHex("C0FFEE42")),
                            kNode, 0, true));
    sponsor.now_ms = 1000 + 29999;
    EXPECT_EQ(sponsor.Seal(JoinResponse(kNode)),
              RefControl(keys, 1, JoinResponse(kNode), kRelay, 0, true,
                         FromHex("C0FFEE42")));
    sponsor.now_ms = 1000 + 30000;
    EXPECT_EQ(sponsor.Seal(JoinResponse(kNode)),
              RefControl(keys, 1, JoinResponse(kNode), kRelay, 1, true));
}

TEST(FrameCodecJoin, SponsorTableHoldsFourJoiners) {
    SponsorNonceTable table;
    JoinNonce nonce{};
    for (uint8_t i = 0; i < 5; ++i) {
        table.Store(static_cast<AddressType>(0x0B00 + i), {i, i, i, i},
                    100u * i, 30000);
    }
    // The oldest entry made room for the fifth joiner
    EXPECT_FALSE(table.Find(0x0B00, 500, 30000, nonce));
    for (uint8_t i = 1; i < 5; ++i) {
        ASSERT_TRUE(table.Find(static_cast<AddressType>(0x0B00 + i), 500, 30000,
                               nonce));
        EXPECT_EQ(nonce[0], i);
    }
    // An expired entry is reused before a live one is dropped
    table.Store(0x0B09, {9, 9, 9, 9}, 30150, 30000);
    EXPECT_FALSE(table.Find(0x0B01, 30150, 30000, nonce));
    EXPECT_TRUE(table.Find(0x0B02, 30150, 30000, nonce));
    EXPECT_TRUE(table.Find(0x0B09, 30150, 30000, nonce));
    table.Clear();
    EXPECT_FALSE(table.Find(0x0B09, 30150, 30000, nonce));
}

// ---------------------------------------------------------------------------
// Message level entry points
// ---------------------------------------------------------------------------

TEST(FrameCodecMessage, OpenReportsWhyNothingWasReturned) {
    TestNode sender(kRelay);
    TestNode receiver(kNode);
    Bytes frame = sender.Seal(Claim(kRelay));
    OpenInfo info;
    OpenStatus status = OpenStatus::kOk;
    frame.back() ^= 1;
    EXPECT_FALSE(receiver.codec.Open(frame, &info, &status).has_value());
    EXPECT_EQ(status, OpenStatus::kAuthFailed);
    frame.back() ^= 1;
    const auto message = receiver.codec.Open(frame, &info, &status);
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(status, OpenStatus::kOk);
    EXPECT_EQ(info.transmitter, kRelay);
    EXPECT_EQ(message->GetType(), MessageType::NM_CLAIM);
    EXPECT_EQ(message->GetSource(), kRelay);
    EXPECT_EQ(*message->Serialize(), Claim(kRelay));
    // A control message has no body layer fields
    EXPECT_EQ(message->GetSecurityContext(), SecurityContext{});
}

TEST(FrameCodecMessage, CopiesOfAMessageKeepTheContext) {
    TestNode origin(kNode);
    TestNode relay(kRelay);
    const auto received = relay.codec.Open(origin.Seal(
        PlainData(kGateway, kNode, MessageType::DATA, kRelay, 10, 1, kBody)));
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->GetSecurityContext().body_state, BodyState::kCipher);
    BaseMessage copy(*received);
    EXPECT_EQ(copy.GetSecurityContext(), received->GetSecurityContext());
    BaseMessage assigned(0, 0, MessageType::DATA, {});
    assigned = copy;
    EXPECT_EQ(assigned.GetSecurityContext(), received->GetSecurityContext());
    BaseMessage moved(std::move(copy));
    EXPECT_EQ(moved.GetSecurityContext(), received->GetSecurityContext());
    // New content resets it
    ASSERT_TRUE(moved.SetMessage(kGateway, kNode, MessageType::DATA, kBody)
                    .IsSuccess());
    EXPECT_EQ(moved.GetSecurityContext(), SecurityContext{});
}

}  // namespace
}  // namespace codec_test
}  // namespace security
}  // namespace loramesher
