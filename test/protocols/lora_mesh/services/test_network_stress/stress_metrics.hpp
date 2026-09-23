/**
 * @file stress_metrics.hpp
 * @brief Measurement + presentation helpers for the mixed-traffic stress test.
 *
 * Two measurement planes are kept strictly separate:
 *   - App plane  : true PDR and end-to-end latency, from DeliveryResult
 *                  (reliable RTT) and the DataReceivedEx callback (one-way).
 *   - Link plane : per-address radio counters (received/dropped/collision)
 *                  that explain *why* the app plane looks the way it does.
 *
 * The suite emits three tiers of output:
 *   A) an aligned ASCII scorecard + HEALTHY/COLLAPSED verdict to stdout,
 *   B) GoogleTest RecordProperty() rows + one "##METRICS## {json}" line, and
 *   C) (via the normal logger, captured by CI) the log_analyzer.html timeline.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "types/messages/base_message.hpp"

namespace loramesher {
namespace test {

/// Traffic classes carried in the stress payload header.
enum class TrafficClass : uint8_t {
    kNonReliable = 0,
    kReliable = 1,
    kGroup = 2
};

/// Fixed 14-byte self-describing payload so every reception can be attributed
/// to a send without any shared lookup table.
///   [0]      magic 0xA5
///   [1]      TrafficClass
///   [2..3]   sender address (big-endian)
///   [4..5]   destination / group address (big-endian)
///   [6..9]   monotonic sequence (big-endian)
///   [10..13] send timestamp in virtual ms (big-endian)
struct StressPayload {
    static constexpr uint8_t kMagic = 0xA5;
    static constexpr size_t kSize = 14;

    static std::vector<uint8_t> Encode(TrafficClass cls, AddressType sender,
                                       AddressType dest, uint32_t seq,
                                       uint32_t send_time_ms) {
        std::vector<uint8_t> p(kSize, 0);
        p[0] = kMagic;
        p[1] = static_cast<uint8_t>(cls);
        p[2] = static_cast<uint8_t>(sender >> 8);
        p[3] = static_cast<uint8_t>(sender & 0xFF);
        p[4] = static_cast<uint8_t>(dest >> 8);
        p[5] = static_cast<uint8_t>(dest & 0xFF);
        p[6] = static_cast<uint8_t>((seq >> 24) & 0xFF);
        p[7] = static_cast<uint8_t>((seq >> 16) & 0xFF);
        p[8] = static_cast<uint8_t>((seq >> 8) & 0xFF);
        p[9] = static_cast<uint8_t>(seq & 0xFF);
        p[10] = static_cast<uint8_t>((send_time_ms >> 24) & 0xFF);
        p[11] = static_cast<uint8_t>((send_time_ms >> 16) & 0xFF);
        p[12] = static_cast<uint8_t>((send_time_ms >> 8) & 0xFF);
        p[13] = static_cast<uint8_t>(send_time_ms & 0xFF);
        return p;
    }

    bool valid = false;
    TrafficClass cls = TrafficClass::kNonReliable;
    AddressType sender = 0;
    AddressType dest = 0;
    uint32_t seq = 0;
    uint32_t send_time_ms = 0;

    static StressPayload Decode(const std::vector<uint8_t>& p) {
        StressPayload out;
        if (p.size() < kSize || p[0] != kMagic) {
            return out;
        }
        out.valid = true;
        out.cls = static_cast<TrafficClass>(p[1]);
        out.sender = static_cast<AddressType>((p[2] << 8) | p[3]);
        out.dest = static_cast<AddressType>((p[4] << 8) | p[5]);
        out.seq = (static_cast<uint32_t>(p[6]) << 24) |
                  (static_cast<uint32_t>(p[7]) << 16) |
                  (static_cast<uint32_t>(p[8]) << 8) | p[9];
        out.send_time_ms = (static_cast<uint32_t>(p[10]) << 24) |
                           (static_cast<uint32_t>(p[11]) << 16) |
                           (static_cast<uint32_t>(p[12]) << 8) | p[13];
        return out;
    }
};

/// Nearest-rank percentile over an integer sample set (p in [0,1]).
inline uint32_t Percentile(std::vector<uint32_t> v, double p) {
    if (v.empty()) {
        return 0;
    }
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(std::ceil(p * v.size()));
    if (idx > 0) {
        idx--;
    }
    if (idx >= v.size()) {
        idx = v.size() - 1;
    }
    return v[idx];
}

inline double Ratio(uint64_t num, uint64_t den) {
    return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
}

/// Aggregated per-run measurements. Populated by the fixture callbacks and the
/// per-round samplers, then rendered at the end of the run.
struct StressMetrics {
    // ---- App plane: reliable unicast ----
    uint64_t reliable_sent = 0;
    uint64_t reliable_delivered = 0;  // distinct Delivered outcomes
    uint64_t reliable_failed = 0;
    std::vector<uint32_t> reliable_rtt_ms;  // RTT samples

    // ---- App plane: non-reliable unicast ----
    uint64_t non_reliable_sent = 0;
    std::set<uint64_t> non_reliable_seen;  // dedup key = sender<<32 | seq
    std::vector<uint32_t> non_reliable_latency_ms;  // one-way

    // ---- App plane: group ----
    uint64_t group_sent = 0;           // reliable group sends
    uint64_t group_expected_acks = 0;  // summed expected responders
    uint64_t group_actual_acks = 0;    // summed window ack_count
    std::vector<uint8_t> group_window_counts;
    std::vector<uint32_t> group_recv_latency_ms;  // one-way, member deliveries
    uint64_t group_member_receipts = 0;

    // ---- Link plane / capacity ----
    // Per-relay (backbone head) TX-queue depth samples over the run.
    std::map<AddressType, std::vector<uint32_t>> relay_queue_samples;
    // Reliable RTT samples tagged with the virtual time of delivery, so a
    // rising-p95-slope collapse signal can be computed across time quartiles.
    std::vector<std::pair<uint32_t, uint32_t>> rtt_timeline;  // (time_ms, rtt)

    // ---- Link plane: radio counters (filled at teardown) ----
    uint64_t link_received = 0;
    uint64_t link_dropped = 0;
    uint64_t link_collisions = 0;

    // ---- Schedule: data TX slots not matched by a neighbour's RX slot ----
    size_t tdma_misalignments = 0;

    // ---- Timing ----
    uint32_t convergence_ms = 0;
    uint32_t superframe_ms = 0;

    double ReliablePdr() const {
        return Ratio(reliable_delivered, reliable_sent);
    }

    double NonReliablePdr() const {
        return Ratio(non_reliable_seen.size(), non_reliable_sent);
    }

    double GroupAckCompleteness() const {
        return Ratio(group_actual_acks, group_expected_acks);
    }

    double CollisionRate() const {
        return Ratio(link_collisions,
                     link_received + link_dropped + link_collisions);
    }

    uint32_t MaxRelayQueue() const {
        uint32_t m = 0;
        for (const auto& kv : relay_queue_samples) {
            for (uint32_t s : kv.second) {
                m = std::max(m, s);
            }
        }
        return m;
    }

    /// Mean queue depth across relays over the final quarter of the samples —
    /// a persistently high value means the queue never drains (collapse).
    double FinalQuartileRelayQueue() const {
        uint64_t sum = 0;
        uint64_t n = 0;
        for (const auto& kv : relay_queue_samples) {
            const auto& v = kv.second;
            size_t start = (v.size() * 3) / 4;
            for (size_t i = start; i < v.size(); i++) {
                sum += v[i];
                n++;
            }
        }
        return Ratio(sum, n);
    }

    /// Reliable RTT p95 slope across time: p95 of the last time-quartile
    /// divided by p95 of the first. >1 means queueing delay is growing.
    double RttP95Slope() const {
        if (rtt_timeline.size() < 8) {
            return 0.0;
        }
        std::vector<std::pair<uint32_t, uint32_t>> t = rtt_timeline;
        std::sort(t.begin(), t.end());
        size_t q = t.size() / 4;
        std::vector<uint32_t> first, last;
        for (size_t i = 0; i < q; i++) {
            first.push_back(t[i].second);
        }
        for (size_t i = t.size() - q; i < t.size(); i++) {
            last.push_back(t[i].second);
        }
        uint32_t p_first = Percentile(first, 0.95);
        uint32_t p_last = Percentile(last, 0.95);
        return p_first == 0
                   ? 0.0
                   : static_cast<double>(p_last) / static_cast<double>(p_first);
    }
};

}  // namespace test
}  // namespace loramesher
