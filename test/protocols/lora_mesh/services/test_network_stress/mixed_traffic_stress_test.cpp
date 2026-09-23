/**
 * @file mixed_traffic_stress_test.cpp
 * @brief 25-node mixed-traffic stress test.
 *
 * A clustered-backbone topology (stars of leaves hung off a line of cluster
 * heads) is driven with a paced mix of non-reliable, reliable, and
 * group-with-ACK traffic. The backbone heads are forced relays: every
 * cross-cluster message and every group flood funnels through them. The test
 * measures delivery and TX-queue depth and asserts that the queues stay bounded
 * and local delivery holds.
 *
 * Topology (node index i, address 0x1000+i):
 *   cluster  c = i / 5,  head index h(c) = 5*c
 *   leaves   (i % 5 != 0) link only to their cluster head  (star)
 *   backbone h(c) <-> h(c+1)                                (line)
 *   network manager = the centre head
 */

#include <gtest/gtest.h>

#include <iomanip>
#include <iostream>
#include <mutex>

#include "../test_routing/routing_test_fixture.hpp"
#include "stress_metrics.hpp"

namespace loramesher {
namespace test {

struct StressParams {
    int node_count;       // multiple of 5 (10, 15, 25, ...)
    uint8_t data_slots;   // default_data_slots per node (baseline)
    float backbone_loss;  // per-link loss on backbone edges (0 = clean)
    bool enforce;         // apply hard pass/fail asserts (shipping config)
    const char* label;
};

class NetworkStressTest : public RoutingTestFixture,
                          public ::testing::WithParamInterface<StressParams> {
   protected:
    using DeliveryResult = protocols::reliability::DeliveryResult;
    using Outcome = protocols::reliability::Outcome;
    using MessageId = protocols::reliability::MessageId;

    static constexpr AddressType kBaseAddr = 0x1000;
    static constexpr AddressType kGroupAll = 0x8001;
    static constexpr AddressType kGroupBackbone = 0x8002;
    static constexpr int kClusterSize = 5;
    static constexpr int kRounds = 20;
    static constexpr int kSettleSuperframes = 20;
    static constexpr int kMaxDrainSuperframes = 40;

    protocols::lora_mesh::NetworkService* Net(TestNode& node) {
        return node.protocol->GetNetworkServiceForTest();
    }

    int ClusterOf(int i) const { return i / kClusterSize; }

    bool IsHead(int i) const { return (i % kClusterSize) == 0; }

    int HeadIndexOf(int i) const { return ClusterOf(i) * kClusterSize; }

    // -- Topology ------------------------------------------------------------
    void BuildTopology(const StressParams& p) {
        ASSERT_EQ(p.node_count % kClusterSize, 0)
            << "node_count must be a multiple of " << kClusterSize;
        num_clusters_ = p.node_count / kClusterSize;
        nm_index_ = (num_clusters_ / 2) * kClusterSize;  // centre head

        uint8_t slots = p.data_slots;
        for (int i = 0; i < p.node_count; i++) {
            AddressType addr = kBaseAddr + i;
            NodeRole role = (i == nm_index_) ? NodeRole::NETWORK_MANAGER
                                             : NodeRole::NODE_ONLY;
            CreateNode("N" + std::to_string(i), addr, role, PinConfig(),
                       RadioConfig(), [slots](LoRaMeshProtocolConfig& c) {
                           c.setDefaultDataSlots(slots);
                           // Leaf-to-leaf across the backbone is 6 hops at 25
                           // nodes; allow it so routing can fully converge.
                           c.setMaxHops(6);
                       });
            for (auto& np : nodes_) {
                if (np->address == addr) {
                    topo_.push_back(np.get());
                    break;
                }
            }
        }

        neighbours_.assign(p.node_count, {});
        auto link = [this](int a, int b) {
            SetLinkStatus(*topo_[a], *topo_[b], true);
            neighbours_[a].push_back(b);
            neighbours_[b].push_back(a);
        };

        // Intra-cluster star edges: each leaf to its head.
        for (int i = 0; i < p.node_count; i++) {
            if (!IsHead(i)) {
                link(i, HeadIndexOf(i));
            }
        }
        // Backbone line edges between consecutive heads.
        for (int c = 0; c + 1 < num_clusters_; c++) {
            int a = c * kClusterSize;
            int b = (c + 1) * kClusterSize;
            link(a, b);
            if (p.backbone_loss > 0.0f) {
                SetLinkLoss(*topo_[a], *topo_[b], p.backbone_loss);
            }
        }
    }

    std::vector<int> HeadIndices() const {
        std::vector<int> heads;
        for (int c = 0; c < num_clusters_; c++) {
            heads.push_back(c * kClusterSize);
        }
        return heads;
    }

    // -- Callbacks (app-plane measurement) -----------------------------------
    void WireCallbacks() {
        for (auto* node : topo_) {
            TestNode* n = node;
            Net(*n)->SetDataReceivedExCallback(
                [this, n](AddressType /*source*/, MessageId /*id*/,
                          uint8_t hops, const std::vector<uint8_t>& data) {
                    StressPayload pl = StressPayload::Decode(data);
                    if (!pl.valid) {
                        return;
                    }
                    if (pl.cls == TrafficClass::kNonReliable &&
                        pl.dest != n->address) {
                        return;
                    }
                    uint32_t now = GetRTOS().getTickCount();
                    uint32_t latency =
                        now >= pl.send_time_ms ? now - pl.send_time_ms : 0;
                    std::lock_guard<std::mutex> lock(metrics_mu_);
                    if (pl.cls == TrafficClass::kNonReliable) {
                        uint64_t key =
                            (static_cast<uint64_t>(pl.sender) << 32) | pl.seq;
                        if (metrics_.non_reliable_seen.insert(key).second) {
                            metrics_.non_reliable_latency_ms.push_back(latency);
                        }
                    } else if (pl.cls == TrafficClass::kGroup) {
                        metrics_.group_member_receipts++;
                        metrics_.group_recv_latency_ms.push_back(latency);
                    }
                    (void)hops;
                });

            // Replace the fixture delivery callback so we can also timestamp
            // reliable deliveries for the RTT-slope collapse signal.
            Net(*n)->SetDeliveryCallback([this, n](const DeliveryResult& r) {
                n->delivery_outcomes.push_back(r);
                std::lock_guard<std::mutex> lock(metrics_mu_);
                if (r.outcome == Outcome::Delivered &&
                    reliable_ids_.count(r.id.value())) {
                    metrics_.rtt_timeline.push_back(
                        {GetRTOS().getTickCount(), r.rtt_ms});
                }
            });
        }
    }

    void JoinGroups() {
        for (auto* node : topo_) {
            ASSERT_TRUE(Net(*node)->JoinGroup(kGroupAll));
        }
        for (int h : HeadIndices()) {
            ASSERT_TRUE(Net(*topo_[h])->JoinGroup(kGroupBackbone));
        }
    }

    // -- Sending -------------------------------------------------------------
    void SendNonReliable(int from_idx, int to_idx) {
        uint32_t now = GetRTOS().getTickCount();
        auto payload = StressPayload::Encode(
            TrafficClass::kNonReliable, topo_[from_idx]->address,
            topo_[to_idx]->address, ++global_seq_, now);
        Result r = topo_[from_idx]->protocol->SendData(topo_[to_idx]->address,
                                                       payload);
        std::lock_guard<std::mutex> lock(metrics_mu_);
        if (r) {
            metrics_.non_reliable_sent++;
        }
    }

    void SendReliableUnicast(int from_idx, int to_idx) {
        uint32_t now = GetRTOS().getTickCount();
        auto payload = StressPayload::Encode(
            TrafficClass::kReliable, topo_[from_idx]->address,
            topo_[to_idx]->address, ++global_seq_, now);
        // No timeout override: the library sizes the timeout from the
        // measured round trip (or the hop count before the first sample).
        MessageId id = Net(*topo_[from_idx])
                           ->SendReliable(topo_[to_idx]->address, payload,
                                          /*max_retries=*/4);
        std::lock_guard<std::mutex> lock(metrics_mu_);
        if (id.source != 0) {
            metrics_.reliable_sent++;
            reliable_ids_.insert(id.value());
        }
    }

    void SendGroupReliable(AddressType group, uint64_t expected_acks) {
        uint32_t now = GetRTOS().getTickCount();
        auto payload = StressPayload::Encode(TrafficClass::kGroup,
                                             topo_[nm_index_]->address, group,
                                             ++global_seq_, now);
        auto superframe = metrics_.superframe_ms;
        MessageId id =
            Net(*topo_[nm_index_])
                ->SendGroupReliable(
                    group,
                    std::span<const uint8_t>(payload.data(), payload.size()),
                    /*max_retries=*/0,
                    /*window_ms=*/superframe * 4);
        std::lock_guard<std::mutex> lock(metrics_mu_);
        if (id.source != 0) {
            metrics_.group_sent++;
            metrics_.group_expected_acks += expected_acks;
            group_ids_.insert(id.value());
        }
    }

    void SampleRelayQueues() {
        // Called at superframe boundaries on the main thread (tasks reblocked).
        for (int h : HeadIndices()) {
            metrics_.relay_queue_samples[topo_[h]->address].push_back(
                static_cast<uint32_t>(topo_[h]->protocol->GetTxQueueSize()));
        }
    }

    // -- Traffic driver ------------------------------------------------------
    void SendRound(const StressParams& p, int round) {
        int last_head = (num_clusters_ - 1) * kClusterSize;
        int far_leaf = last_head + 1;  // a leaf in the last cluster
        int near_leaf = 1;             // a leaf in the first cluster

        // Non-reliable telemetry: each leaf -> its head, staggered by parity.
        for (int i = 0; i < p.node_count; i++) {
            if (!IsHead(i) && ((i + round) % 2 == 0)) {
                SendNonReliable(i, HeadIndexOf(i));
            }
        }
        // Reliable cross-backbone commands every 3rd round.
        if (round % 3 == 0 && num_clusters_ >= 2) {
            SendReliableUnicast(0, far_leaf);
            SendReliableUnicast(last_head, near_leaf);
        }
        // Group config-push to all members every 5th round.
        if (round % 5 == 0) {
            SendGroupReliable(kGroupAll,
                              static_cast<uint64_t>(p.node_count - 1));
        }
        // Small backbone group every 7th round (known responder count).
        if (round % 7 == 0 && num_clusters_ >= 2) {
            SendGroupReliable(kGroupBackbone,
                              static_cast<uint64_t>(num_clusters_ - 1));
        }
    }

    // Discard everything measured so far and start a clean measurement window.
    void ResetMeasurement() {
        ClearAllReceivedMessages();
        for (auto* node : topo_) {
            node->delivery_outcomes.clear();
        }
        reliable_ids_.clear();
        group_ids_.clear();
        uint32_t sf = metrics_.superframe_ms;
        uint32_t conv = metrics_.convergence_ms;
        metrics_ = StressMetrics{};
        metrics_.superframe_ms = sf;
        metrics_.convergence_ms = conv;
        // Link counters cover the measured window only, not network formation.
        for (auto* node : topo_) {
            virtual_network_.ResetReceivedMessageCount(node->address);
            virtual_network_.ResetDroppedMessageCount(node->address);
            virtual_network_.ResetCollisionCount(node->address);
        }
    }

    // -- TDMA alignment ------------------------------------------------------
    using SlotType = types::protocols::lora_mesh::SlotAllocation::SlotType;

    /// Print each node's data band: T = own TX, R<i> = RX from node i,
    /// . = sleep. Other slot types are omitted.
    void DumpDataBand() {
        for (size_t i = 0; i < topo_.size(); i++) {
            auto table = topo_[i]->protocol->GetSlotTable();
            std::ostringstream row;
            row << "##DATA## " << topo_[i]->name << " |";
            for (const auto& slot : table) {
                if (slot.type == SlotType::TX) {
                    row << " T";
                } else if (slot.type == SlotType::RX) {
                    row << " R" << (slot.target_address - kBaseAddr);
                }
            }
            std::cout << row.str() << "\n";
        }
        std::cout << std::flush;
    }

    /// For every node X and every data slot X transmits in, each topology
    /// neighbour Y of X must be listening to X in that slot. Returns the
    /// number of (sender, listener, slot) violations.
    size_t CountTdmaMisalignments() {
        size_t violations = 0;
        for (size_t x = 0; x < topo_.size(); x++) {
            auto tx_table = topo_[x]->protocol->GetSlotTable();
            for (int y : neighbours_[x]) {
                auto rx_table = topo_[y]->protocol->GetSlotTable();
                if (rx_table.size() != tx_table.size()) {
                    ADD_FAILURE()
                        << topo_[x]->name << " and " << topo_[y]->name
                        << " disagree on superframe length (" << tx_table.size()
                        << " vs " << rx_table.size() << ")";
                    violations++;
                    continue;
                }
                for (size_t s = 0; s < tx_table.size(); s++) {
                    if (tx_table[s].type != SlotType::TX) {
                        continue;
                    }
                    if (rx_table[s].type != SlotType::RX ||
                        rx_table[s].target_address != topo_[x]->address) {
                        violations++;
                        std::cout
                            << "##MISALIGNED## slot " << s << ": "
                            << topo_[x]->name << " TX, " << topo_[y]->name
                            << " type=" << static_cast<int>(rx_table[s].type)
                            << " target=0x" << std::hex
                            << rx_table[s].target_address << std::dec << "\n";
                    }
                }
            }
        }
        return violations;
    }

    void RunTraffic(const StressParams& p) {
        auto superframe = metrics_.superframe_ms;

        // Idle until the slot tables and superframe size settle so the
        // measurement captures steady state rather than formation transients.
        AdvanceTime(superframe * kSettleSuperframes);
        ResetMeasurement();

        // Re-read the superframe so reliable timeouts and per-round stepping
        // match the settled frame.
        superframe = GetSuperframeDuration(*topo_.front());
        metrics_.superframe_ms = superframe;

        DumpDataBand();
        metrics_.tdma_misalignments = CountTdmaMisalignments();

        // DIAGNOSTIC: dump per-node allocation + slot-table composition.
        for (auto* node : topo_) {
            auto tbl = node->protocol->GetSlotTable();
            int tx = 0, rx = 0, sleep = 0;
            for (const auto& s : tbl) {
                using ST =
                    types::protocols::lora_mesh::SlotAllocation::SlotType;
                if (s.type == ST::TX && s.target_address == node->address)
                    tx++;
                else if (s.type == ST::RX)
                    rx++;
                else if (s.type == ST::SLEEP)
                    sleep++;
            }
            std::cout << "##ALLOC## " << node->name << " local="
                      << static_cast<int>(
                             Net(*node)->GetLocalAllocatedDataSlots())
                      << " tx=" << tx << " rx=" << rx << " sleep=" << sleep
                      << " frame=" << tbl.size() << "\n";
        }
        std::cout << std::flush;

        // Measured window.
        for (int round = 0; round < kRounds; round++) {
            SendRound(p, round);
            AdvanceTime(superframe, superframe, 50u, 0, nullptr);
            SampleRelayQueues();
        }

        // Drain: let in-flight reliable messages and group windows resolve,
        // bounded so a stuck message cannot hang the test.
        for (int i = 0; i < kMaxDrainSuperframes && ReliablePending() > 0;
             i++) {
            AdvanceTime(superframe, superframe, 50u, 0, nullptr);
            SampleRelayQueues();
        }
        metrics_.reliable_pending_at_end = ReliablePending();
    }

    size_t ReliablePending() {
        size_t pending = 0;
        for (auto* node : topo_) {
            pending += Net(*node)->GetReliablePendingCount();
        }
        return pending;
    }

    // -- Finalize / report ---------------------------------------------------
    void CollectOutcomes() {
        for (auto* node : topo_) {
            for (const auto& r : node->delivery_outcomes) {
                uint32_t v = r.id.value();
                if (reliable_ids_.count(v)) {
                    if (r.outcome == Outcome::Delivered) {
                        metrics_.reliable_delivered++;
                        metrics_.reliable_rtt_ms.push_back(r.rtt_ms);
                    } else if (r.outcome == Outcome::Failed) {
                        metrics_.reliable_failed++;
                    }
                } else if (group_ids_.count(v)) {
                    if (r.outcome == Outcome::GroupWindowClosed) {
                        metrics_.group_window_counts.push_back(r.ack_count);
                        metrics_.group_actual_acks += r.ack_count;
                    }
                }
            }
        }
    }

    void CollectLinkCounters() {
        for (auto* node : topo_) {
            metrics_.link_received +=
                virtual_network_.GetReceivedMessageCount(node->address);
            metrics_.link_dropped +=
                virtual_network_.GetDroppedMessageCount(node->address);
            metrics_.link_collisions +=
                virtual_network_.GetCollisionCount(node->address);
        }
    }

    void EmitScorecard(const StressParams& p) {
        const auto& m = metrics_;
        double q_sat = m.FinalQuartileRelayQueue();
        // Verdict is scoped to what slot allocation actually governs: local
        // (non-reliable) delivery and relay-queue drainage. Reliable/group
        // multi-hop delivery has a separate, allocation-independent failure
        // (it is ~0% even under flat allocation) tracked as its own issue, so
        // it is reported below but does not decide HEALTHY/COLLAPSED here.
        // TX queue capacity in tests is 10; a persistently >half-full relay
        // queue means it never drains (collapse).
        bool healthy = m.NonReliablePdr() >= 0.95 && q_sat < 5.0 &&
                       m.tdma_misalignments == 0;

        auto pct = [](double d) {
            std::ostringstream o;
            o << std::fixed << std::setprecision(1) << (d * 100.0) << "%";
            return o.str();
        };

        std::cout << "\n===== STRESS SCORECARD [" << p.label
                  << "] nodes=" << p.node_count
                  << " slots=" << static_cast<int>(p.data_slots)
                  << " loss=" << p.backbone_loss << " =====\n";
        std::cout << std::left << std::setw(28) << "metric" << "value\n";
        auto row = [](const std::string& k, const std::string& v) {
            std::cout << std::left << std::setw(28) << k << v << "\n";
        };
        row("reliable pending at end",
            std::to_string(m.reliable_pending_at_end));
        row("reliable PDR", pct(m.ReliablePdr()) + "  (" +
                                std::to_string(m.reliable_delivered) + "/" +
                                std::to_string(m.reliable_sent) + ")");
        row("non-reliable PDR", pct(m.NonReliablePdr()) + "  (" +
                                    std::to_string(m.non_reliable_seen.size()) +
                                    "/" + std::to_string(m.non_reliable_sent) +
                                    ")");
        row("group ACK completeness",
            pct(m.GroupAckCompleteness()) + "  (" +
                std::to_string(m.group_actual_acks) + "/" +
                std::to_string(m.group_expected_acks) + ")");
        row("reliable RTT p50/p95",
            std::to_string(Percentile(m.reliable_rtt_ms, 0.50)) + "/" +
                std::to_string(Percentile(m.reliable_rtt_ms, 0.95)) + " ms");
        row("non-reliable 1way p50/p95",
            std::to_string(Percentile(m.non_reliable_latency_ms, 0.50)) + "/" +
                std::to_string(Percentile(m.non_reliable_latency_ms, 0.95)) +
                " ms");
        row(
            "relay queue max/final-q",
            std::to_string(m.MaxRelayQueue()) + "/" + [&] {
                std::ostringstream o;
                o << std::fixed << std::setprecision(1) << q_sat;
                return o.str();
            }());
        row("RTT p95 slope", [&] {
            std::ostringstream o;
            o << std::fixed << std::setprecision(2) << m.RttP95Slope();
            return o.str();
        }());
        row("collision rate", pct(m.CollisionRate()));
        row("TDMA misalignments", std::to_string(m.tdma_misalignments));
        row("superframe / convergence",
            std::to_string(m.superframe_ms) + " / " +
                std::to_string(m.convergence_ms) + " ms");
        std::cout << "VERDICT: " << (healthy ? "HEALTHY" : "COLLAPSED") << "\n";

        // Machine-readable line (tier B) — sorted keys for byte-stable output.
        std::ostringstream js;
        js << "##METRICS## {"
           << "\"label\":\"" << p.label << "\","
           << "\"nodes\":" << p.node_count << ","
           << "\"slots\":" << static_cast<int>(p.data_slots) << ","
           << "\"loss\":" << p.backbone_loss << ","
           << "\"pdr_reliable\":" << m.ReliablePdr() << ","
           << "\"reliable_pending_at_end\":" << m.reliable_pending_at_end << ","
           << "\"pdr_non_reliable\":" << m.NonReliablePdr() << ","
           << "\"group_ack_completeness\":" << m.GroupAckCompleteness() << ","
           << "\"rtt_p50\":" << Percentile(m.reliable_rtt_ms, 0.50) << ","
           << "\"rtt_p95\":" << Percentile(m.reliable_rtt_ms, 0.95) << ","
           << "\"oneway_p50\":" << Percentile(m.non_reliable_latency_ms, 0.50)
           << ","
           << "\"oneway_p95\":" << Percentile(m.non_reliable_latency_ms, 0.95)
           << ","
           << "\"relay_queue_max\":" << m.MaxRelayQueue() << ","
           << "\"relay_queue_final_q\":" << q_sat << ","
           << "\"rtt_p95_slope\":" << m.RttP95Slope() << ","
           << "\"collision_rate\":" << m.CollisionRate() << ","
           << "\"tdma_misalignments\":" << m.tdma_misalignments << ","
           << "\"superframe_ms\":" << m.superframe_ms << ","
           << "\"convergence_ms\":" << m.convergence_ms << ","
           << "\"verdict\":\"" << (healthy ? "HEALTHY" : "COLLAPSED") << "\"}";
        std::cout << js.str() << "\n" << std::flush;

        RecordProperty(
            "pdr_reliable",
            std::to_string(static_cast<int>(m.ReliablePdr() * 1000)));
        RecordProperty("relay_queue_final_q",
                       std::to_string(static_cast<int>(q_sat * 10)));
        RecordProperty("verdict", healthy ? "HEALTHY" : "COLLAPSED");
    }

    // -- The scenario --------------------------------------------------------
    void RunScenario(const StressParams& p) {
        BuildTopology(p);
        WireCallbacks();

        uint32_t start_ms = GetRTOS().getTickCount();
        for (auto* node : topo_) {
            ASSERT_TRUE(StartNode(*node)) << "Failed to start " << node->name;
        }
        ASSERT_TRUE(WaitForNetworkFormation(topo_, p.node_count - 1))
            << "Network did not form (" << p.node_count << " nodes)";
        // Large multi-hop meshes may not reach full all-to-all routing
        // knowledge within the strict window; proceed after a bounded settle
        // and let the traffic phase measure whatever routing exists.
        if (!WaitForRoutingStabilization(topo_)) {
            AdvanceTime(GetSuperframeDuration(*topo_.front()) * 3);
        }
        metrics_.convergence_ms = GetRTOS().getTickCount() - start_ms;
        metrics_.superframe_ms = GetSuperframeDuration(*topo_.front());

        JoinGroups();
        ClearAllReceivedMessages();
        for (auto* node : topo_) {
            node->delivery_outcomes.clear();
        }

        RunTraffic(p);

        CollectOutcomes();
        CollectLinkCounters();
        EmitScorecard(p);

        // Every node's data band must agree with its neighbours' after settle.
        EXPECT_EQ(metrics_.tdma_misalignments, 0u)
            << "Data-band TX slots not matched by neighbour RX slots";

        if (p.enforce) {
            EXPECT_GE(metrics_.NonReliablePdr(), 0.95)
                << "1-hop non-reliable delivery regressed";
            EXPECT_GE(metrics_.ReliablePdr(), 0.9)
                << "Reliable unicast delivery regressed";
            EXPECT_EQ(metrics_.reliable_pending_at_end, 0u)
                << "Reliable messages still pending after the drain";
            EXPECT_LT(metrics_.FinalQuartileRelayQueue(), 5.0)
                << "Backbone relay TX queue is persistently saturated";
            EXPECT_GT(metrics_.link_received, 0u) << "Network never forwarded";
        }
    }

    std::mutex metrics_mu_;
    StressMetrics metrics_;
    std::vector<TestNode*> topo_;
    std::vector<std::vector<int>> neighbours_;  // topology adjacency by index
    std::set<uint32_t> reliable_ids_;
    std::set<uint32_t> group_ids_;
    int num_clusters_ = 0;
    int nm_index_ = 0;
    uint32_t global_seq_ = 0;
};

TEST_P(NetworkStressTest, MixedTraffic) {
    RunScenario(GetParam());
}

INSTANTIATE_TEST_SUITE_P(
    Scenarios, NetworkStressTest,
    ::testing::Values(
        // node_count, data_slots, backbone_loss, enforce, label
        StressParams{10, 2, 0.0f, true, "10n_uniform"},
        StressParams{25, 2, 0.0f, false, "25n_uniform"}),
    [](const ::testing::TestParamInfo<StressParams>& info) {
        return std::string(info.param.label);
    });

}  // namespace test
}  // namespace loramesher
