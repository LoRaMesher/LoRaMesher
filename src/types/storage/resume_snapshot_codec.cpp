/**
 * @file resume_snapshot_codec.cpp
 * @brief Binary encoding of a ResumeSnapshot
 */

#include "types/storage/resume_snapshot_codec.hpp"

#include <array>
#include <cstring>
#include <set>
#include <utility>

#include "types/storage/snapshot_codec.hpp"
#include "utils/byte_operations.h"

namespace loramesher {
namespace storage {

namespace {

using types::protocols::lora_mesh::NetworkNodeRoute;
using types::protocols::lora_mesh::PathRtt;
using LinkStats = NetworkNodeRoute::LinkQualityStats;

constexpr uint8_t kFlagNetworkManager = 0x01;
constexpr uint8_t kFlagActive = 0x02;
constexpr uint8_t kFlagLinkStats = 0x04;
constexpr uint8_t kFlagPathRtt = 0x08;
constexpr uint8_t kKnownRouteFlags =
    kFlagNetworkManager | kFlagActive | kFlagLinkStats | kFlagPathRtt;

/// Magic, version and embedded snapshot length
constexpr size_t kPrefixSize = 6;
/// Timing, member state, group stream, slot count and slot band sizes
constexpr size_t kStateSize = 30 + 26 + 2 + 2 + 4;
constexpr uint8_t kFlagRebuildPending = 0x01;

using types::protocols::lora_mesh::SlotAllocation;

bool IsKnownSlotType(SlotAllocation::SlotType type) {
    const auto value = static_cast<uint8_t>(type);
    return value >= static_cast<uint8_t>(SlotAllocation::SlotType::TX) &&
           value <=
               static_cast<uint8_t>(SlotAllocation::SlotType::SYNC_BEACON_RX);
}

constexpr size_t kCrcSize = 4;

uint32_t FloatBits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float FloatFromBits(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void WriteLinkStats(utils::ByteSerializer& out, const LinkStats& stats) {
    out.WriteUint32(stats.messages_expected);
    out.WriteUint32(stats.messages_received);
    out.WriteUint32(stats.last_message_time);
    out.WriteUint8(stats.remote_link_quality);
    out.WriteUint8(stats.remote_absent_streak);
    out.WriteUint8(stats.local_broadcasts);
    out.WriteUint8(stats.consecutive_missed);
    out.WriteUint8(stats.ewma_quality);
    out.WriteUint8(stats.recovery_counter);
    out.WriteUint8(stats.inactive_probe_count);
    out.WriteUint32(FloatBits(stats.last_rssi));
    out.WriteUint32(FloatBits(stats.last_snr));
    const auto window = stats.window.GetState();
    out.WriteUint32(window.window);
    out.WriteUint8(window.position);
    out.WriteUint8(window.total_expected);
}

using LinkStatsBytes = std::array<uint8_t, ResumeSnapshotCodec::kLinkStatsSize>;

LinkStatsBytes EncodeLinkStats(const LinkStats& stats) {
    LinkStatsBytes bytes{};
    utils::ByteSerializer out(bytes.data(), bytes.size());
    WriteLinkStats(out, stats);
    return bytes;
}

/// True if @p stats hold anything beyond the values of a new route
bool HasLinkHistory(const LinkStats& stats) {
    static const LinkStatsBytes kInitial = EncodeLinkStats(LinkStats{});
    return EncodeLinkStats(stats) != kInitial;
}

bool HasPathRtt(const PathRtt& rtt) {
    return rtt.srtt_ms != 0 || rtt.rttvar_ms != 0;
}

/// Reads little-endian fields; after the first failed read every read
/// returns 0 and ok() is false
class FieldReader {
   public:
    explicit FieldReader(std::span<const uint8_t> data) : in_(data) {}

    uint8_t U8() { return Take(in_.ReadUint8()); }

    uint16_t U16() { return Take(in_.ReadUint16()); }

    uint32_t U32() { return Take(in_.ReadUint32()); }

    std::span<const uint8_t> Bytes(size_t length) {
        return Take(in_.ReadBytesAsSpan(length));
    }

    bool ok() const { return ok_; }

    /// True if every read succeeded and no byte is left
    bool AtEnd() { return ok_ && !in_.ReadUint8().has_value(); }

   private:
    template <typename T>
    T Take(std::optional<T> value) {
        if (!ok_ || !value) {
            ok_ = false;
            return T{};
        }
        return *value;
    }

    utils::ByteDeserializer in_;
    bool ok_ = true;
};

std::optional<LinkStats> ReadLinkStats(FieldReader& in) {
    LinkStats stats;
    stats.messages_expected = in.U32();
    stats.messages_received = in.U32();
    stats.last_message_time = in.U32();
    stats.remote_link_quality = in.U8();
    stats.remote_absent_streak = in.U8();
    stats.local_broadcasts = in.U8();
    stats.consecutive_missed = in.U8();
    stats.ewma_quality = in.U8();
    stats.recovery_counter = in.U8();
    stats.inactive_probe_count = in.U8();
    stats.last_rssi = FloatFromBits(in.U32());
    stats.last_snr = FloatFromBits(in.U32());
    SlidingWindowPDR<16>::State window;
    window.window = in.U32();
    window.position = in.U8();
    window.total_expected = in.U8();
    if (!in.ok() || !stats.window.SetState(window)) {
        return std::nullopt;
    }
    return stats;
}

size_t RouteSize(const NetworkNodeRoute& route) {
    size_t size = ResumeSnapshotCodec::kRouteSize;
    if (HasLinkHistory(route.link_stats)) {
        size += ResumeSnapshotCodec::kLinkStatsSize;
    }
    if (HasPathRtt(route.path_rtt)) {
        size += ResumeSnapshotCodec::kPathRttSize;
    }
    return size;
}

void WriteRoute(utils::ByteSerializer& out, const NetworkNodeRoute& route) {
    const auto& entry = route.routing_entry;
    const bool link_stats = HasLinkHistory(route.link_stats);
    const bool path_rtt = HasPathRtt(route.path_rtt);
    uint8_t flags = 0;
    flags |= route.is_network_manager ? kFlagNetworkManager : 0;
    flags |= route.is_active ? kFlagActive : 0;
    flags |= link_stats ? kFlagLinkStats : 0;
    flags |= path_rtt ? kFlagPathRtt : 0;

    out.WriteUint16(entry.destination);
    out.WriteUint16(route.next_hop);
    out.WriteUint8(entry.hop_count);
    out.WriteUint8(entry.link_quality);
    out.WriteUint8(entry.allocated_data_slots);
    out.WriteUint8(entry.capabilities);
    out.WriteUint8(entry.control_slot_index);
    out.WriteUint8(entry.reception_quality);
    out.WriteUint16(entry.next_hop);
    out.WriteUint8(route.control_slot_index);
    out.WriteUint8(flags);
    out.WriteUint32(route.last_seen);
    out.WriteUint32(route.last_updated);
    if (link_stats) {
        WriteLinkStats(out, route.link_stats);
    }
    if (path_rtt) {
        out.WriteUint32(route.path_rtt.srtt_ms);
        out.WriteUint32(route.path_rtt.rttvar_ms);
    }
}

std::optional<NetworkNodeRoute> ReadRoute(FieldReader& in) {
    NetworkNodeRoute route;
    auto& entry = route.routing_entry;
    entry.destination = in.U16();
    route.next_hop = in.U16();
    entry.hop_count = in.U8();
    entry.link_quality = in.U8();
    entry.allocated_data_slots = in.U8();
    entry.capabilities = in.U8();
    entry.control_slot_index = in.U8();
    entry.reception_quality = in.U8();
    entry.next_hop = in.U16();
    route.control_slot_index = in.U8();
    const uint8_t flags = in.U8();
    route.last_seen = in.U32();
    route.last_updated = in.U32();
    if (!in.ok() || (flags & ~kKnownRouteFlags) != 0) {
        return std::nullopt;
    }
    route.is_network_manager = (flags & kFlagNetworkManager) != 0;
    route.is_active = (flags & kFlagActive) != 0;

    // Initial statistics and round-trip times are never encoded
    if (flags & kFlagLinkStats) {
        auto stats = ReadLinkStats(in);
        if (!stats || !HasLinkHistory(*stats)) {
            return std::nullopt;
        }
        route.link_stats = *stats;
    }
    if (flags & kFlagPathRtt) {
        route.path_rtt.srtt_ms = in.U32();
        route.path_rtt.rttvar_ms = in.U32();
        if (!in.ok() || !HasPathRtt(route.path_rtt)) {
            return std::nullopt;
        }
    }
    return route;
}

}  // namespace

size_t ResumeSnapshotCodec::EncodedSize(const ResumeSnapshot& snapshot) {
    size_t size = kPrefixSize + SnapshotCodec::EncodedSize(snapshot.network) +
                  kStateSize + 1 + 1 + kCrcSize;
    for (const auto& route : snapshot.routes) {
        size += RouteSize(route);
    }
    size += snapshot.schedule.slots.size() * kSlotSize;
    return size + 1 + 1 +
           snapshot.sequence_streams.size() * kSequenceStreamSize +
           snapshot.delivery_streams.size() * kDeliveryStreamSize +
           snapshot.seen_messages.size() * kSeenMessageSize;
}

bool ResumeSnapshotCodec::HasMagic(std::span<const uint8_t> data) {
    if (data.size() < sizeof(kMagic)) {
        return false;
    }
    utils::ByteDeserializer deserializer(data);
    return deserializer.ReadUint32() == kMagic;
}

bool ResumeSnapshotCodec::IsValid(const ResumeSnapshot& snapshot) {
    const NetworkSnapshot& network = snapshot.network;
    if (!SnapshotCodec::IsValid(network) || network.was_network_manager) {
        return false;
    }
    if (!IsUnicastAddress(snapshot.member.network_manager) ||
        snapshot.member.network_manager == network.node_address) {
        return false;
    }
    const ResumeTiming& timing = snapshot.timing;
    if (timing.total_slots == 0 || timing.slot_duration_ms == 0 ||
        static_cast<uint64_t>(timing.total_slots) * timing.slot_duration_ms !=
            network.superframe_duration_ms) {
        return false;
    }

    if (snapshot.schedule.slots.size() > kMaxSlots) {
        return false;
    }
    for (const auto& slot : snapshot.schedule.slots) {
        if (!IsKnownSlotType(slot.type)) {
            return false;
        }
    }

    if (snapshot.routes.size() > kMaxRoutes) {
        return false;
    }
    std::set<AddressType> destinations;
    for (const auto& route : snapshot.routes) {
        if (!IsUnicastAddress(route.GetAddress()) ||
            !destinations.insert(route.GetAddress()).second) {
            return false;
        }
    }

    if (snapshot.sequence_streams.size() > kMaxSequenceStreams) {
        return false;
    }
    std::set<AddressType> stream_destinations;
    for (const auto& stream : snapshot.sequence_streams) {
        if (!IsUnicastAddress(stream.destination) ||
            !stream_destinations.insert(stream.destination).second) {
            return false;
        }
    }

    if (snapshot.delivery_streams.size() > kMaxDeliveryStreams) {
        return false;
    }
    std::set<std::pair<AddressType, bool>> delivery_sources;
    for (const auto& stream : snapshot.delivery_streams) {
        if (!IsUnicastAddress(stream.source) || (stream.bitmap & 1u) == 0 ||
            !delivery_sources.insert({stream.source, stream.group}).second) {
            return false;
        }
    }

    if (snapshot.seen_messages.size() > kMaxSeenMessages) {
        return false;
    }
    for (const auto& message : snapshot.seen_messages) {
        if (!IsUnicastAddress(message.source)) {
            return false;
        }
    }
    return true;
}

std::optional<std::vector<uint8_t>> ResumeSnapshotCodec::Encode(
    const ResumeSnapshot& snapshot) {
    if (!IsValid(snapshot)) {
        return std::nullopt;
    }
    auto network_blob = SnapshotCodec::Encode(snapshot.network);
    if (!network_blob || network_blob->size() > UINT8_MAX) {
        return std::nullopt;
    }

    std::vector<uint8_t> blob(EncodedSize(snapshot));
    utils::ByteSerializer out(blob);
    out.WriteUint32(kMagic);
    out.WriteUint8(kFormatVersion);
    out.WriteUint8(static_cast<uint8_t>(network_blob->size()));
    out.WriteBytes(network_blob->data(), network_blob->size());

    const ResumeTiming& timing = snapshot.timing;
    out.WriteUint32(timing.tick_at_save_ms);
    out.WriteUint32(static_cast<uint32_t>(timing.persistent_time_at_save_us));
    out.WriteUint32(
        static_cast<uint32_t>(timing.persistent_time_at_save_us >> 32));
    out.WriteUint32(timing.wake_deadline_ms);
    out.WriteUint32(timing.superframe_start_ms);
    out.WriteUint32(timing.superframes_completed);
    out.WriteUint32(timing.slot_duration_ms);
    out.WriteUint16(timing.total_slots);

    const MemberState& member = snapshot.member;
    out.WriteUint16(member.network_manager);
    out.WriteUint8(member.slots_per_superframe);
    out.WriteUint8(member.beacon_node_count);
    out.WriteUint8(member.control_slot_index);
    out.WriteUint8(member.allocated_data_slots);
    out.WriteUint8(member.table_version);
    out.WriteUint32(member.last_sync_time_ms);
    out.WriteUint32(member.last_sync_beacon_ms);
    out.WriteUint32(member.last_route_cleanup_ms);
    out.WriteUint32(static_cast<uint32_t>(member.sleep_clock.GetPpm()));
    out.WriteUint8(member.sleep_clock.GetSamples());
    out.WriteUint16(static_cast<uint16_t>(member.schedule_offset_ms));

    out.WriteUint8(snapshot.group_next_sequence ? 1 : 0);
    out.WriteUint8(snapshot.group_next_sequence.value_or(0));

    const SlotSchedule& schedule = snapshot.schedule;
    out.WriteUint16(static_cast<uint16_t>(schedule.slots.size()));
    for (const auto& slot : schedule.slots) {
        out.WriteUint16(slot.slot_number);
        out.WriteUint8(static_cast<uint8_t>(slot.type));
        out.WriteUint16(slot.target_address);
    }
    out.WriteUint8(schedule.control_slots);
    out.WriteUint16(schedule.discovery_slots);
    out.WriteUint8(schedule.rebuild_pending ? kFlagRebuildPending : 0);

    out.WriteUint8(static_cast<uint8_t>(snapshot.routes.size()));
    for (const auto& route : snapshot.routes) {
        WriteRoute(out, route);
    }
    out.WriteUint8(static_cast<uint8_t>(snapshot.sequence_streams.size()));
    for (const auto& stream : snapshot.sequence_streams) {
        out.WriteUint16(stream.destination);
        out.WriteUint8(stream.next);
    }
    out.WriteUint8(static_cast<uint8_t>(snapshot.delivery_streams.size()));
    for (const auto& stream : snapshot.delivery_streams) {
        out.WriteUint16(stream.source);
        out.WriteUint8(stream.group ? 1 : 0);
        out.WriteUint8(stream.highest);
        out.WriteUint32(stream.bitmap);
        out.WriteUint32(stream.last_send_ts);
    }
    out.WriteUint8(static_cast<uint8_t>(snapshot.seen_messages.size()));
    for (const auto& message : snapshot.seen_messages) {
        out.WriteUint16(message.source);
        out.WriteUint8(message.sequence);
    }

    const size_t payload_size = out.getOffset();
    out.WriteUint32(SnapshotCodec::Crc32(
        std::span<const uint8_t>(blob.data(), payload_size)));
    return blob;
}

std::optional<ResumeSnapshot> ResumeSnapshotCodec::Decode(
    std::span<const uint8_t> data) {
    if (data.size() < kPrefixSize + kCrcSize) {
        return std::nullopt;
    }
    // Check the CRC first: the content is parsed only when it is intact
    const size_t payload_size = data.size() - kCrcSize;
    utils::ByteDeserializer crc_reader(data.subspan(payload_size));
    if (crc_reader.ReadUint32() !=
        SnapshotCodec::Crc32(data.first(payload_size))) {
        return std::nullopt;
    }

    FieldReader in(data.first(payload_size));
    if (in.U32() != kMagic || in.U8() != kFormatVersion) {
        return std::nullopt;
    }
    const uint8_t network_size = in.U8();
    auto network = SnapshotCodec::Decode(in.Bytes(network_size));
    if (!in.ok() || !network) {
        return std::nullopt;
    }

    ResumeSnapshot snapshot;
    snapshot.network = *network;

    ResumeTiming& timing = snapshot.timing;
    timing.tick_at_save_ms = in.U32();
    timing.persistent_time_at_save_us = in.U32();
    timing.persistent_time_at_save_us |= static_cast<uint64_t>(in.U32()) << 32;
    timing.wake_deadline_ms = in.U32();
    timing.superframe_start_ms = in.U32();
    timing.superframes_completed = in.U32();
    timing.slot_duration_ms = in.U32();
    timing.total_slots = in.U16();

    MemberState& member = snapshot.member;
    member.network_manager = in.U16();
    member.slots_per_superframe = in.U8();
    member.beacon_node_count = in.U8();
    member.control_slot_index = in.U8();
    member.allocated_data_slots = in.U8();
    member.table_version = in.U8();
    member.last_sync_time_ms = in.U32();
    member.last_sync_beacon_ms = in.U32();
    member.last_route_cleanup_ms = in.U32();
    const auto sleep_clock_ppm = static_cast<int32_t>(in.U32());
    const uint8_t sleep_clock_samples = in.U8();
    member.schedule_offset_ms = static_cast<int16_t>(in.U16());
    if (sleep_clock_ppm < -power::SleepClockCalibration::kMaxPpm ||
        sleep_clock_ppm > power::SleepClockCalibration::kMaxPpm) {
        return std::nullopt;
    }
    member.sleep_clock =
        power::SleepClockCalibration(sleep_clock_ppm, sleep_clock_samples);

    const uint8_t group_started = in.U8();
    const uint8_t group_next = in.U8();
    if (group_started > 1 || (group_started == 0 && group_next != 0)) {
        return std::nullopt;
    }
    if (group_started) {
        snapshot.group_next_sequence = group_next;
    }

    SlotSchedule& schedule = snapshot.schedule;
    const uint16_t slot_count = in.U16();
    if (slot_count > kMaxSlots) {
        return std::nullopt;
    }
    schedule.slots.reserve(slot_count);
    for (uint16_t i = 0; i < slot_count && in.ok(); ++i) {
        SlotAllocation slot;
        slot.slot_number = in.U16();
        slot.type = static_cast<SlotAllocation::SlotType>(in.U8());
        slot.target_address = in.U16();
        schedule.slots.push_back(slot);
    }
    schedule.control_slots = in.U8();
    schedule.discovery_slots = in.U16();
    const uint8_t schedule_flags = in.U8();
    if ((schedule_flags & ~kFlagRebuildPending) != 0) {
        return std::nullopt;
    }
    schedule.rebuild_pending = (schedule_flags & kFlagRebuildPending) != 0;

    const uint8_t route_count = in.U8();
    snapshot.routes.reserve(route_count);
    for (uint8_t i = 0; i < route_count && in.ok(); ++i) {
        auto route = ReadRoute(in);
        if (!route) {
            return std::nullopt;
        }
        snapshot.routes.push_back(*route);
    }

    const uint8_t stream_count = in.U8();
    snapshot.sequence_streams.reserve(stream_count);
    for (uint8_t i = 0; i < stream_count && in.ok(); ++i) {
        SequenceStream stream;
        stream.destination = in.U16();
        stream.next = in.U8();
        snapshot.sequence_streams.push_back(stream);
    }

    const uint8_t delivery_count = in.U8();
    snapshot.delivery_streams.reserve(delivery_count);
    for (uint8_t i = 0; i < delivery_count && in.ok(); ++i) {
        DeliveryStream stream;
        stream.source = in.U16();
        const uint8_t group = in.U8();
        stream.group = group != 0;
        stream.highest = in.U8();
        stream.bitmap = in.U32();
        stream.last_send_ts = in.U32();
        if (group > 1) {
            return std::nullopt;
        }
        snapshot.delivery_streams.push_back(stream);
    }

    const uint8_t seen_count = in.U8();
    snapshot.seen_messages.reserve(seen_count);
    for (uint8_t i = 0; i < seen_count && in.ok(); ++i) {
        SeenMessage message;
        message.source = in.U16();
        message.sequence = in.U8();
        snapshot.seen_messages.push_back(message);
    }

    // The blob must end exactly after the announced content
    if (!in.AtEnd() || !IsValid(snapshot)) {
        return std::nullopt;
    }
    return snapshot;
}

}  // namespace storage
}  // namespace loramesher
