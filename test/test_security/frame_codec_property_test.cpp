/**
 * @file frame_codec_property_test.cpp
 * @brief Random frames of every type and length through seal and open
 *
 * Properties:
 * - seal then open gives the input back, for every sealable type and every
 *   payload length the format allows
 * - the sealed bytes equal the independent reference construction
 * - a frame that differs from a sealed one in any way is never accepted
 */

#include <gtest/gtest.h>

#include "frame_codec_test_support.hpp"

namespace loramesher {
namespace security {
namespace codec_test {
namespace {

/// Small deterministic generator, so a failure can be reproduced
class Random {
   public:
    explicit Random(uint64_t seed) : state_(seed) {}

    uint32_t Next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return static_cast<uint32_t>(state_ >> 16);
    }

    /// Uniform enough value in [0, bound)
    uint32_t Below(uint32_t bound) { return Next() % bound; }

    Bytes Fill(size_t size) {
        Bytes bytes(size);
        for (uint8_t& byte : bytes) {
            byte = static_cast<uint8_t>(Next());
        }
        return bytes;
    }

   private:
    uint64_t state_;
};

const MessageType kSealableTypes[] = {
    MessageType::SYNC_BEACON,
    MessageType::ROUTE_TABLE,
    MessageType::NM_CLAIM,
    MessageType::SLOT_REQUEST,
    MessageType::SLOT_ALLOCATION,
    MessageType::HELLO,
    MessageType::SYNC,
    MessageType::PING,
    MessageType::PONG,
    MessageType::JOIN_REQUEST,
    MessageType::JOIN_RESPONSE,
    MessageType::DATA,
    MessageType::DATA_BROADCAST,
    MessageType::DATA_RELIABLE,
    MessageType::DATA_GROUP,
    MessageType::ACK,
};

constexpr AddressType kSender = 0x0A10;
constexpr AddressType kReceiver = 0x0A11;

struct Sample {
    MessageType type;
    Bytes plain;
};

/// A random plain frame the sender may seal, addressed so that the
/// receiver is its destination or one of its flood listeners
Sample RandomPlain(Random& random, size_t max_packet_size) {
    const MessageType type =
        kSealableTypes[random.Below(std::size(kSealableTypes))];
    const size_t trailer = SecuredTrailerLen(type);
    const size_t room = max_packet_size - kBaseHeaderSize - trailer;
    const size_t max_payload = room < 255 ? room : 255;
    switch (ClassOf(type)) {
        case FrameClass::kBeacon: {
            Bytes payload = random.Fill(kBeaconPlainPayloadSize);
            payload[kBeaconDelayHighOffset] = 0;
            return {type, PlainFrame(0xFFFF, kSender, type, payload)};
        }
        case FrameClass::kControl:
            return {
                type,
                PlainFrame(random.Below(2) ? 0xFFFF : kReceiver, kSender, type,
                           random.Fill(random.Below(
                               static_cast<uint32_t>(max_payload + 1))))};
        case FrameClass::kJoin: {
            // The source of a join frame is preserved across hops, so it
            // is any node. The sender must not look like the joiner of a
            // response it seals, and the receiver is not joining
            const AddressType source =
                static_cast<AddressType>(0x0C00 + random.Below(16));
            return {
                type,
                PlainFrame(random.Below(2) ? kReceiver : kGateway, source, type,
                           random.Fill(random.Below(
                               static_cast<uint32_t>(max_payload + 1))))};
        }
        default: {
            const size_t extension = DataExtensionLen(type);
            const Bytes body = random.Fill(random.Below(
                static_cast<uint32_t>(max_payload - extension + 1)));
            const uint8_t ttl = static_cast<uint8_t>(1 + random.Below(255));
            const uint8_t seq = static_cast<uint8_t>(random.Next());
            if (type == MessageType::DATA_GROUP) {
                return {type, PlainGroup(static_cast<AddressType>(
                                             0x8000 + random.Below(0x7FFE)),
                                         kSender, ttl,
                                         static_cast<uint8_t>(random.Below(2)),
                                         seq, body)};
            }
            if (type == MessageType::DATA_BROADCAST) {
                return {type, PlainData(0xFFFF, kSender, type, 0xFFFF, ttl, seq,
                                        body)};
            }
            return {type, PlainData(kReceiver, kSender, type,
                                    random.Below(2) ? kReceiver : 0xFFFF, ttl,
                                    seq, body)};
        }
    }
}

/// The same frame built without the codec, from the counter it will take
Bytes Reference(const NetworkTrafficKeys& keys, const Sample& sample,
                TestNode& sender) {
    switch (ClassOf(sample.type)) {
        case FrameClass::kBeacon:
            return RefBeacon(keys, 1, sample.plain,
                             sender.counters.GetNext(CounterSpace::kBeacon));
        case FrameClass::kControl:
            return RefControl(keys, 1, sample.plain, kSender,
                              sender.counters.GetNext(CounterSpace::kControl),
                              false);
        case FrameClass::kJoin:
            return RefControl(keys, 1, sample.plain, kSender,
                              sender.counters.GetNext(CounterSpace::kControl),
                              true);
        default:
            return RefGroupData(keys, 1, sample.plain,
                                sender.counters.GetNext(CounterSpace::kData));
    }
}

/// A frame that differs from the sealed one
Bytes Mutate(Random& random, const Bytes& frame) {
    Bytes mutated = frame;
    switch (random.Below(6)) {
        case 0:
        case 1:
        case 2: {  // one flipped bit
            const size_t bit =
                random.Below(static_cast<uint32_t>(mutated.size() * 8));
            mutated[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));
            break;
        }
        case 3: {  // one byte replaced by another value
            const size_t index =
                random.Below(static_cast<uint32_t>(mutated.size()));
            mutated[index] ^= static_cast<uint8_t>(1 + random.Below(255));
            break;
        }
        case 4:  // cut short
            mutated.resize(random.Below(static_cast<uint32_t>(mutated.size())));
            break;
        default: {  // extended
            const Bytes extra = random.Fill(1 + random.Below(12));
            mutated.insert(mutated.end(), extra.begin(), extra.end());
            break;
        }
    }
    return mutated;
}

void RunRoundTrip(uint64_t seed, size_t max_packet_size, int iterations) {
    Random random(seed);
    const std::vector<NetworkKey> keys_list = {{1, NetKey1()}};
    TestNode sender(kSender, 1, {}, keys_list, max_packet_size);
    TestNode receiver(kReceiver, 1, {}, keys_list, max_packet_size);
    // Never sees a genuine frame, so it must never accept anything
    TestNode victim(kReceiver, 1, {}, keys_list, max_packet_size);
    const NetworkTrafficKeys keys = RefNetKeys(NetKey1(), 1);
    size_t class_count[6] = {0, 0, 0, 0, 0, 0};

    for (int i = 0; i < iterations; ++i) {
        const Sample sample = RandomPlain(random, max_packet_size);
        ++class_count[static_cast<size_t>(ClassOf(sample.type))];
        sender.Refill();
        const Bytes expected = Reference(keys, sample, sender);
        const Bytes frame = sender.Seal(sample.plain);
        ASSERT_EQ(sender.last_seal, SealStatus::kOk)
            << "seed " << seed << " iteration " << i;
        ASSERT_EQ(frame, expected) << "seed " << seed << " iteration " << i;
        ASSERT_LE(frame.size(), max_packet_size);
        ASSERT_EQ(frame.size(), OnAirSize(SecurityMode::kSecured, sample.type,
                                          sample.plain.size()));

        // Anything else is rejected and leaves no trace
        for (int m = 0; m < 3; ++m) {
            const Bytes mutated = Mutate(random, frame);
            ASSERT_NE(mutated, frame);
            ASSERT_TRUE(victim.Open(mutated).empty())
                << "seed " << seed << " iteration " << i;
            ASSERT_NE(victim.last_open, OpenStatus::kOk);
        }

        // The genuine frame opens to the input
        Bytes opened = receiver.Open(frame);
        ASSERT_EQ(receiver.last_open, OpenStatus::kOk)
            << "seed " << seed << " iteration " << i << " type "
            << static_cast<int>(sample.type);
        ASSERT_EQ(receiver.info.plain_type, sample.type);
        ASSERT_EQ(receiver.info.on_air_size, frame.size());
        if (ClassOf(sample.type) == FrameClass::kData) {
            ASSERT_EQ(receiver.info.role, IsFloodType(sample.type)
                                              ? RxRole::kFlood
                                              : RxRole::kDestination);
            opened = receiver.OpenBody(opened);
            ASSERT_EQ(receiver.last_open, OpenStatus::kOk);
        } else {
            ASSERT_EQ(receiver.info.role, RxRole::kLocal);
        }
        ASSERT_EQ(opened, sample.plain)
            << "seed " << seed << " iteration " << i;

        // And only once
        ASSERT_TRUE(receiver.Open(frame).empty());
        ASSERT_EQ(receiver.last_open, OpenStatus::kReplay);
    }
    EXPECT_EQ(victim.codec.GetPeers().Size(), 0u);
    EXPECT_EQ(victim.codec.GetStats().rx_opened, 0u);
    EXPECT_EQ(receiver.codec.GetStats().rx_opened,
              static_cast<uint32_t>(iterations));
    // Every class was exercised
    for (const FrameClass frame_class :
         {FrameClass::kBeacon, FrameClass::kControl, FrameClass::kJoin,
          FrameClass::kData}) {
        EXPECT_GT(class_count[static_cast<size_t>(frame_class)], 0u);
    }
}

TEST(FrameCodecProperty, SealThenOpenReturnsTheInputAtTheDefaultLimit) {
    RunRoundTrip(0x1234567ULL, 115, 1500);
}

TEST(FrameCodecProperty, SealThenOpenReturnsTheInputAtTheSmallLimit) {
    RunRoundTrip(0xBADC0FFEEULL, 51, 600);
}

TEST(FrameCodecProperty, SealThenOpenReturnsTheInputAtEveryFormatLength) {
    // No radio limit: payload lengths up to 255 bytes
    RunRoundTrip(0x5EEDULL, kMaxSealedFrameSize, 900);
}

TEST(FrameCodecProperty, RandomBytesAreNeverAccepted) {
    Random random(0xFEEDFACEULL);
    TestNode victim(kReceiver);
    for (int i = 0; i < 4000; ++i) {
        Bytes frame = random.Fill(random.Below(130));
        if (frame.size() >= 6 && random.Below(2) == 0) {
            // Make the envelope look right: a secured type of a real
            // class and a payload size that satisfies the length rule
            const MessageType type =
                kSealableTypes[random.Below(std::size(kSealableTypes))];
            const size_t trailer = SecuredTrailerLen(type);
            if (frame.size() >= 6 + trailer) {
                frame[4] = message_type::ToWireType(type, 1);
                frame[5] = static_cast<uint8_t>(frame.size() - 6 - trailer);
            }
        }
        ASSERT_TRUE(victim.Open(frame).empty()) << "iteration " << i;
        ASSERT_NE(victim.last_open, OpenStatus::kOk);
    }
    EXPECT_EQ(victim.codec.GetPeers().Size(), 0u);
    const SecurityStats stats = victim.codec.GetStats();
    EXPECT_EQ(stats.rx_opened, 0u);
    EXPECT_GT(stats.rx_auth_failed, 500u);
}

/// A relay chain: every hop forwards the received bytes and the last hop
/// reads the origin's payload
TEST(FrameCodecProperty, RelayChainsDeliverTheOriginPayload) {
    Random random(0xC4A1ULL);
    const NetworkTrafficKeys keys = RefNetKeys(NetKey1(), 1);
    TestNode origin(kSender);
    constexpr size_t kHops = 5;
    std::vector<std::unique_ptr<TestNode>> nodes;
    for (size_t i = 0; i < kHops; ++i) {
        nodes.push_back(
            std::make_unique<TestNode>(static_cast<AddressType>(0x0D00 + i)));
    }
    const AddressType final_destination =
        static_cast<AddressType>(0x0D00 + kHops - 1);
    for (int i = 0; i < 200; ++i) {
        const MessageType type =
            i % 3 == 0
                ? MessageType::DATA
                : (i % 3 == 1 ? MessageType::DATA_RELIABLE : MessageType::ACK);
        const Bytes body = random.Fill(random.Below(89));
        const uint8_t seq = static_cast<uint8_t>(i);
        const Bytes plain =
            PlainData(final_destination, kSender, type, 0x0D00, 10, seq, body);
        Bytes frame = origin.Seal(plain);
        ASSERT_EQ(origin.last_seal, SealStatus::kOk);
        const Bytes sealed_body(frame.begin() + 10, frame.end() - 4);
        for (size_t hop = 0; hop < kHops; ++hop) {
            TestNode& node = *nodes[hop];
            const Bytes opened = node.Open(frame);
            ASSERT_EQ(node.last_open, OpenStatus::kOk) << "hop " << hop;
            if (hop + 1 < kHops) {
                ASSERT_EQ(node.info.role, RxRole::kRelay);
                frame = node.Forward(
                    opened, static_cast<AddressType>(0x0D00 + hop + 1));
                ASSERT_EQ(node.last_seal, SealStatus::kOk);
                // Body, counter field and tag are the origin's bytes
                ASSERT_EQ(Bytes(frame.begin() + 10, frame.end() - 4),
                          sealed_body);
            } else {
                ASSERT_EQ(node.info.role, RxRole::kDestination);
                ASSERT_EQ(node.OpenBody(opened),
                          PlainData(final_destination, kSender, type,
                                    final_destination,
                                    static_cast<uint8_t>(10 - hop), seq, body));
            }
        }
    }
    // No relay spent a data counter
    for (const auto& node : nodes) {
        EXPECT_EQ(node->counters.GetNext(CounterSpace::kData), 0u);
    }
    (void)keys;
}

}  // namespace
}  // namespace codec_test
}  // namespace security
}  // namespace loramesher
