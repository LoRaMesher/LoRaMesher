/**
 * @file resume_snapshot_codec_test.cpp
 * @brief Tests of the ResumeSnapshot encoding
 */

#include <gtest/gtest.h>

#include "types/storage/resume_snapshot_codec.hpp"
#include "types/storage/snapshot_codec.hpp"

namespace loramesher {
namespace storage {
namespace {

using types::protocols::lora_mesh::NetworkNodeRoute;

constexpr AddressType kNode = 0x2001;
constexpr AddressType kManager = 0x1000;

/// Offset of the first route record
constexpr size_t kScheduleSlots = 3;
constexpr size_t kRoutesOffset =
    4 + 1 + 1 + 21 + 30 + 31 + 2 + 2 + kScheduleSlots * 5 + 4 + 1;
/// Size of everything but the route and stream records
constexpr size_t kFixedSize =
    4 + 1 + 1 + 21 + 30 + 31 + 2 + 2 + 4 + 1 + 1 + 1 + 1 + 4;

NetworkNodeRoute MakeOwnEntry() {
    NetworkNodeRoute route(kNode, 1000, false, 0x03, 2, 0);
    route.next_hop = 0;
    route.is_active = true;
    return route;
}

NetworkNodeRoute MakeDirectNeighbour() {
    NetworkNodeRoute route(kManager, kManager, 1, 230, 123456);
    route.is_network_manager = true;
    route.is_active = true;
    route.last_seen = 0xFFFFFF00u;
    route.routing_entry.allocated_data_slots = 1;
    route.routing_entry.capabilities = 0x81;
    route.routing_entry.control_slot_index = 0;
    route.routing_entry.reception_quality = 199;
    route.routing_entry.next_hop = 0x2002;
    route.control_slot_index = 7;

    auto& stats = route.link_stats;
    stats.messages_expected = 70000;
    stats.messages_received = 65000;
    stats.last_message_time = 123400;
    stats.remote_link_quality = 210;
    stats.remote_absent_streak = 1;
    stats.local_broadcasts = 5;
    stats.consecutive_missed = 2;
    stats.ewma_quality = 222;
    stats.recovery_counter = 3;
    stats.inactive_probe_count = 4;
    stats.last_rssi = -97.25f;
    stats.last_snr = 6.5f;
    EXPECT_TRUE(stats.window.SetState({0xA5A5, 9, 16}));

    route.path_rtt.srtt_ms = 4321;
    route.path_rtt.rttvar_ms = 765;
    return route;
}

NetworkNodeRoute MakeMultiHopRoute() {
    NetworkNodeRoute route(0x3003, kManager, 2, 180, 120000);
    route.is_active = false;
    route.last_seen = 119000;
    return route;
}

ResumeSnapshot MakeSnapshot() {
    ResumeSnapshot snapshot;
    snapshot.network.node_address = kNode;
    snapshot.network.network_id = 0xBEEF;
    snapshot.network.last_sequence = 42;
    snapshot.network.network_depth = 2;
    snapshot.network.superframe_duration_ms = 20 * 1000;

    snapshot.timing.tick_at_save_ms = 0xFFFFF000u;
    snapshot.timing.persistent_time_at_save_us = 0x123456789ABCull;
    snapshot.timing.wake_deadline_ms = 0x00000400u;
    snapshot.timing.superframe_start_ms = 0xFFFFE000u;
    snapshot.timing.superframes_completed = 77777;
    snapshot.timing.slot_duration_ms = 1000;
    snapshot.timing.total_slots = 20;

    snapshot.member.network_manager = kManager;
    snapshot.member.slots_per_superframe = 20;
    snapshot.member.beacon_node_count = 3;
    snapshot.member.control_slot_index = 2;
    snapshot.member.allocated_data_slots = 1;
    snapshot.member.table_version = 250;
    snapshot.member.last_sync_time_ms = 0xFFFFE010u;
    snapshot.member.last_sync_beacon_ms = 0xFFFFE011u;
    snapshot.member.last_route_cleanup_ms = 0xFFFF0000u;
    snapshot.member.light_sleep_clock = power::SleepClockCalibration(3312, 14);
    snapshot.member.deep_sleep_clock = power::SleepClockCalibration(-7123, 9);
    snapshot.member.schedule_offset_ms = -17;

    using SlotType = types::protocols::lora_mesh::SlotAllocation::SlotType;
    snapshot.schedule.slots = {{0, SlotType::SYNC_BEACON_RX, 0},
                               {1, SlotType::CONTROL_TX, 0},
                               {2, SlotType::TX, kManager}};
    snapshot.schedule.control_slots = 3;
    snapshot.schedule.discovery_slots = 260;
    snapshot.schedule.rebuild_pending = true;

    snapshot.routes = {MakeOwnEntry(), MakeDirectNeighbour(),
                       MakeMultiHopRoute()};
    snapshot.sequence_streams = {{kManager, 17}, {0x3003, 0}};
    snapshot.group_next_sequence = 200;
    snapshot.delivery_streams = {{kManager, false, 9, 0x80000001u, 0xDEAD},
                                 {kManager, true, 250, 0x3u, 7},
                                 {0x3003, false, 0, 0x1u, 0}};
    snapshot.seen_messages = {{0x3003, 5}, {kManager, 255}, {0x3003, 6}};
    return snapshot;
}

std::vector<uint8_t> EncodeOrFail(const ResumeSnapshot& snapshot) {
    auto blob = ResumeSnapshotCodec::Encode(snapshot);
    EXPECT_TRUE(blob.has_value());
    return blob.value_or(std::vector<uint8_t>{});
}

void ExpectSameRoute(const NetworkNodeRoute& actual,
                     const NetworkNodeRoute& expected) {
    SCOPED_TRACE(testing::Message()
                 << "route 0x" << std::hex << expected.GetAddress());
    const auto& a = actual.routing_entry;
    const auto& e = expected.routing_entry;
    EXPECT_EQ(a.destination, e.destination);
    EXPECT_EQ(a.hop_count, e.hop_count);
    EXPECT_EQ(a.link_quality, e.link_quality);
    EXPECT_EQ(a.allocated_data_slots, e.allocated_data_slots);
    EXPECT_EQ(a.capabilities, e.capabilities);
    EXPECT_EQ(a.control_slot_index, e.control_slot_index);
    EXPECT_EQ(a.reception_quality, e.reception_quality);
    EXPECT_EQ(a.next_hop, e.next_hop);
    EXPECT_EQ(actual.last_seen, expected.last_seen);
    EXPECT_EQ(actual.is_network_manager, expected.is_network_manager);
    EXPECT_EQ(actual.next_hop, expected.next_hop);
    EXPECT_EQ(actual.last_updated, expected.last_updated);
    EXPECT_EQ(actual.is_active, expected.is_active);
    EXPECT_EQ(actual.control_slot_index, expected.control_slot_index);
    EXPECT_EQ(actual.path_rtt.srtt_ms, expected.path_rtt.srtt_ms);
    EXPECT_EQ(actual.path_rtt.rttvar_ms, expected.path_rtt.rttvar_ms);

    const auto& as = actual.link_stats;
    const auto& es = expected.link_stats;
    EXPECT_EQ(as.messages_expected, es.messages_expected);
    EXPECT_EQ(as.messages_received, es.messages_received);
    EXPECT_EQ(as.last_message_time, es.last_message_time);
    EXPECT_EQ(as.remote_link_quality, es.remote_link_quality);
    EXPECT_EQ(as.remote_absent_streak, es.remote_absent_streak);
    EXPECT_EQ(as.local_broadcasts, es.local_broadcasts);
    EXPECT_EQ(as.consecutive_missed, es.consecutive_missed);
    EXPECT_EQ(as.ewma_quality, es.ewma_quality);
    EXPECT_EQ(as.recovery_counter, es.recovery_counter);
    EXPECT_EQ(as.inactive_probe_count, es.inactive_probe_count);
    EXPECT_EQ(as.last_rssi, es.last_rssi);
    EXPECT_EQ(as.last_snr, es.last_snr);
    EXPECT_EQ(as.window.GetState().window, es.window.GetState().window);
    EXPECT_EQ(as.window.GetState().position, es.window.GetState().position);
    EXPECT_EQ(as.window.GetState().total_expected,
              es.window.GetState().total_expected);
    EXPECT_EQ(as.CalculateQuality(), es.CalculateQuality());
}

void ExpectSameSnapshot(const ResumeSnapshot& actual,
                        const ResumeSnapshot& expected) {
    EXPECT_EQ(actual.network, expected.network);
    EXPECT_EQ(actual.timing, expected.timing);
    EXPECT_EQ(actual.member, expected.member);
    EXPECT_EQ(actual.sequence_streams, expected.sequence_streams);
    EXPECT_EQ(actual.group_next_sequence, expected.group_next_sequence);
    EXPECT_EQ(actual.delivery_streams, expected.delivery_streams);
    EXPECT_EQ(actual.seen_messages, expected.seen_messages);
    EXPECT_EQ(actual.schedule.slots, expected.schedule.slots);
    EXPECT_EQ(actual.schedule.control_slots, expected.schedule.control_slots);
    EXPECT_EQ(actual.schedule.discovery_slots,
              expected.schedule.discovery_slots);
    EXPECT_EQ(actual.schedule.rebuild_pending,
              expected.schedule.rebuild_pending);
    ASSERT_EQ(actual.routes.size(), expected.routes.size());
    for (size_t i = 0; i < expected.routes.size(); ++i) {
        ExpectSameRoute(actual.routes[i], expected.routes[i]);
    }
}

/// Recompute the trailing CRC after a deliberate edit of @p blob
void FixCrc(std::vector<uint8_t>& blob) {
    const size_t payload = blob.size() - 4;
    const uint32_t crc =
        SnapshotCodec::Crc32(std::span<const uint8_t>(blob.data(), payload));
    for (size_t i = 0; i < 4; ++i) {
        blob[payload + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
}

TEST(ResumeSnapshotCodecTest, SnapshotRoundTrips) {
    const ResumeSnapshot snapshot = MakeSnapshot();
    const auto blob = EncodeOrFail(snapshot);

    auto decoded = ResumeSnapshotCodec::Decode(blob);
    ASSERT_TRUE(decoded.has_value());
    ExpectSameSnapshot(*decoded, snapshot);
}

TEST(ResumeSnapshotCodecTest, OnlyRoutesWithHistoryCarryStatistics) {
    const ResumeSnapshot snapshot = MakeSnapshot();
    const auto blob = EncodeOrFail(snapshot);

    // Own entry and multi-hop route: plain records; the neighbour carries its
    // link statistics and round-trip time
    const size_t expected = kFixedSize + kScheduleSlots * 5 + 3 * 22 + 33 + 8 +
                            2 * 3 + 3 * 12 + 3 * 3;
    EXPECT_EQ(blob.size(), expected);
    EXPECT_EQ(ResumeSnapshotCodec::EncodedSize(snapshot), expected);
}

TEST(ResumeSnapshotCodecTest, EmptyTablesRoundTrip) {
    ResumeSnapshot snapshot = MakeSnapshot();
    snapshot.routes.clear();
    snapshot.sequence_streams.clear();
    snapshot.group_next_sequence.reset();
    snapshot.delivery_streams.clear();
    snapshot.seen_messages.clear();
    snapshot.schedule = {};
    const auto blob = EncodeOrFail(snapshot);
    EXPECT_EQ(blob.size(), kFixedSize);

    auto decoded = ResumeSnapshotCodec::Decode(blob);
    ASSERT_TRUE(decoded.has_value());
    ExpectSameSnapshot(*decoded, snapshot);
}

TEST(ResumeSnapshotCodecTest, FiftyNeighboursFitInRtcMemoryBudget) {
    ResumeSnapshot snapshot = MakeSnapshot();
    snapshot.routes.clear();
    snapshot.sequence_streams.clear();
    for (AddressType i = 0; i < 50; ++i) {
        NetworkNodeRoute route = MakeDirectNeighbour();
        route.routing_entry.destination = static_cast<AddressType>(0x4000 + i);
        route.next_hop = route.routing_entry.destination;
        snapshot.routes.push_back(route);
        snapshot.sequence_streams.push_back({route.GetAddress(), 1});
    }
    snapshot.delivery_streams.clear();
    for (AddressType i = 0; i < ResumeSnapshotCodec::kMaxDeliveryStreams; ++i) {
        snapshot.delivery_streams.push_back(
            {static_cast<AddressType>(0x4000 + i), false, 1, 1, 1});
    }
    snapshot.seen_messages.assign(ResumeSnapshotCodec::kMaxSeenMessages,
                                  {0x4000, 1});
    const auto blob = EncodeOrFail(snapshot);
    EXPECT_LE(blob.size(), 4096u);

    auto decoded = ResumeSnapshotCodec::Decode(blob);
    ASSERT_TRUE(decoded.has_value());
    ExpectSameSnapshot(*decoded, snapshot);
}

TEST(ResumeSnapshotCodecTest, IsDistinguishedFromNetworkSnapshot) {
    const auto resume_blob = EncodeOrFail(MakeSnapshot());
    const auto network_blob = SnapshotCodec::Encode(MakeSnapshot().network);
    ASSERT_TRUE(network_blob.has_value());

    EXPECT_TRUE(ResumeSnapshotCodec::HasMagic(resume_blob));
    EXPECT_FALSE(ResumeSnapshotCodec::HasMagic(*network_blob));
    EXPECT_FALSE(ResumeSnapshotCodec::Decode(*network_blob).has_value());
    EXPECT_FALSE(SnapshotCodec::Decode(resume_blob).has_value());
}

TEST(ResumeSnapshotCodecTest, EveryFlippedBitIsRejected) {
    const auto blob = EncodeOrFail(MakeSnapshot());
    for (size_t byte = 0; byte < blob.size(); ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            auto corrupted = blob;
            corrupted[byte] ^= static_cast<uint8_t>(1u << bit);
            EXPECT_FALSE(ResumeSnapshotCodec::Decode(corrupted).has_value())
                << "byte " << byte << " bit " << bit;
        }
    }
}

TEST(ResumeSnapshotCodecTest, TruncatedOrExtendedBlobIsRejected) {
    const auto blob = EncodeOrFail(MakeSnapshot());
    for (size_t size = 0; size < blob.size(); ++size) {
        std::vector<uint8_t> truncated(blob.begin(), blob.begin() + size);
        EXPECT_FALSE(ResumeSnapshotCodec::Decode(truncated).has_value())
            << "size " << size;
    }
    auto extended = blob;
    extended.push_back(0);
    EXPECT_FALSE(ResumeSnapshotCodec::Decode(extended).has_value());
    // A valid CRC over the extra byte does not make the length consistent
    FixCrc(extended);
    EXPECT_FALSE(ResumeSnapshotCodec::Decode(extended).has_value());
}

TEST(ResumeSnapshotCodecTest, ImpossibleReceptionWindowIsRejected) {
    auto blob = EncodeOrFail(MakeSnapshot());
    // Window position of the neighbour: after the own entry and the
    // neighbour's record, at offset 31 of its statistics
    const size_t position = kRoutesOffset + 22 + 22 + 31;
    ASSERT_EQ(blob[position], 9u);
    blob[position] = 16;
    FixCrc(blob);
    EXPECT_FALSE(ResumeSnapshotCodec::Decode(blob).has_value());
}

TEST(ResumeSnapshotCodecTest, InvalidContentIsNotEncoded) {
    const auto rejects = [](auto&& edit) {
        ResumeSnapshot snapshot = MakeSnapshot();
        edit(snapshot);
        return !ResumeSnapshotCodec::Encode(snapshot).has_value() &&
               !ResumeSnapshotCodec::IsValid(snapshot);
    };

    EXPECT_TRUE(ResumeSnapshotCodec::IsValid(MakeSnapshot()));
    EXPECT_TRUE(rejects(
        [](ResumeSnapshot& s) { s.network.was_network_manager = true; }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) { s.network.node_address = 0; }));
    EXPECT_TRUE(
        rejects([](ResumeSnapshot& s) { s.member.network_manager = 0; }));
    EXPECT_TRUE(
        rejects([](ResumeSnapshot& s) { s.member.network_manager = kNode; }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) { s.timing.total_slots = 0; }));
    EXPECT_TRUE(
        rejects([](ResumeSnapshot& s) { s.timing.slot_duration_ms = 0; }));
    EXPECT_TRUE(rejects(
        [](ResumeSnapshot& s) { s.network.superframe_duration_ms += 1; }));
    EXPECT_TRUE(rejects(
        [](ResumeSnapshot& s) { s.routes.push_back(s.routes.back()); }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) {
        s.routes.back().routing_entry.destination = kBroadcastAddress;
    }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) {
        s.sequence_streams.push_back(s.sequence_streams.front());
    }));
    EXPECT_TRUE(rejects(
        [](ResumeSnapshot& s) { s.sequence_streams.push_back({0, 1}); }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) {
        s.delivery_streams.push_back(s.delivery_streams.front());
    }));
    EXPECT_TRUE(rejects(
        [](ResumeSnapshot& s) { s.delivery_streams.front().bitmap = 2; }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) {
        s.delivery_streams.front().source = kBroadcastAddress;
    }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) {
        s.delivery_streams.resize(ResumeSnapshotCodec::kMaxDeliveryStreams + 1);
        for (size_t i = 0; i < s.delivery_streams.size(); ++i) {
            s.delivery_streams[i] = {static_cast<AddressType>(0x5000 + i),
                                     false, 0, 1, 0};
        }
    }));
    EXPECT_TRUE(
        rejects([](ResumeSnapshot& s) { s.seen_messages.push_back({0, 1}); }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) {
        s.schedule.slots.front().type =
            static_cast<types::protocols::lora_mesh::SlotAllocation::SlotType>(
                0);
    }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) {
        s.schedule.slots.resize(ResumeSnapshotCodec::kMaxSlots + 1,
                                s.schedule.slots.front());
    }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) {
        s.seen_messages.assign(ResumeSnapshotCodec::kMaxSeenMessages + 1,
                               {0x3003, 1});
    }));
    EXPECT_TRUE(rejects([](ResumeSnapshot& s) {
        s.routes.assign(ResumeSnapshotCodec::kMaxRoutes + 1, s.routes.back());
        for (size_t i = 0; i < s.routes.size(); ++i) {
            s.routes[i].routing_entry.destination =
                static_cast<AddressType>(0x5000 + i);
        }
    }));
}

TEST(ResumeSnapshotCodecTest, UnknownDeliveryStreamKindIsRejected) {
    auto blob = EncodeOrFail(MakeSnapshot());
    // Kind byte of the first delivery stream: it follows the stream counts
    // and records, which end 1 + 3 * 3 + 4 bytes before the end
    const size_t first_delivery = blob.size() - 4 - 1 - 3 * 3 - 3 * 12;
    ASSERT_EQ(blob[first_delivery + 2], 0u);
    blob[first_delivery + 2] = 2;
    FixCrc(blob);
    EXPECT_FALSE(ResumeSnapshotCodec::Decode(blob).has_value());
}

TEST(ResumeSnapshotCodecTest, OtherFormatVersionIsRejected) {
    for (const uint8_t version : {1, 2, 4}) {
        auto blob = EncodeOrFail(MakeSnapshot());
        blob[4] = version;
        FixCrc(blob);
        EXPECT_FALSE(ResumeSnapshotCodec::Decode(blob).has_value())
            << static_cast<int>(version);
    }
}

TEST(ResumeSnapshotCodecTest, ImplausibleSleepClockErrorIsRejected) {
    // Light-sleep clock error: after the member's manager (2), five 1-byte
    // and three 4-byte fields; the deep-sleep one follows its samples byte
    const size_t light_ppm_offset = 4 + 1 + 1 + 21 + 30 + 2 + 5 + 12;
    for (const size_t ppm_offset : {light_ppm_offset, light_ppm_offset + 5}) {
        SCOPED_TRACE(ppm_offset);
        auto blob = EncodeOrFail(MakeSnapshot());
        const int32_t too_large = power::SleepClockCalibration::kMaxPpm + 1;
        for (size_t i = 0; i < 4; ++i) {
            blob[ppm_offset + i] = static_cast<uint8_t>(too_large >> (8 * i));
        }
        FixCrc(blob);
        EXPECT_FALSE(ResumeSnapshotCodec::Decode(blob).has_value());
    }
}

TEST(ResumeSnapshotCodecTest, CraftedBlobWithValidCrcIsChecked) {
    // A blob with a recomputed CRC still goes through the content rules
    auto blob = EncodeOrFail(MakeSnapshot());
    const size_t manager_offset = 4 + 1 + 1 + 21 + 30;
    ASSERT_EQ(blob[manager_offset], kManager & 0xFF);
    blob[manager_offset] = kNode & 0xFF;
    blob[manager_offset + 1] = kNode >> 8;
    FixCrc(blob);
    EXPECT_FALSE(ResumeSnapshotCodec::Decode(blob).has_value());
}

TEST(SlidingWindowPdrStateTest, StateRoundTripsAndInvalidStateIsRefused) {
    SlidingWindowPDR<16> window;
    window.Expect();
    window.Received();
    window.Expect();
    const auto state = window.GetState();

    SlidingWindowPDR<16> copy;
    ASSERT_TRUE(copy.SetState(state));
    EXPECT_EQ(copy.GetPDR(), window.GetPDR());
    EXPECT_EQ(copy.IsReady(), window.IsReady());

    EXPECT_FALSE(copy.SetState({0, 16, 0}));
    EXPECT_FALSE(copy.SetState({0, 0, 17}));
    EXPECT_FALSE(copy.SetState({0x10000, 0, 0}));
    // A refused state leaves the window unchanged
    EXPECT_EQ(copy.GetPDR(), window.GetPDR());

    SlidingWindowPDR<32> wide;
    EXPECT_TRUE(wide.SetState({0xFFFFFFFFu, 31, 32}));
}

}  // namespace
}  // namespace storage
}  // namespace loramesher
