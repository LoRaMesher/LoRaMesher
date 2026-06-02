/**
 * @file rx_slot_listening_test.cpp
 * @brief Regression test for radio listening across a whole RX slot.
 *
 * A CONTROL_RX slot is a listening window that may carry more than one
 * transmission: timing jitter on a busy node can defer a neighbour's packet
 * into a later slot, so two packets can land in the same window. The protocol
 * must keep the radio in receive for the entire slot rather than sleeping after
 * the first packet — otherwise a packet arriving later in the same window is
 * silently dropped (radio off, no CRC, no reception attempt).
 *
 * This test delivers one packet into a receiver's CONTROL_RX slot, confirms it
 * was accepted, then delivers a second packet later in the same slot and
 * asserts it is not dropped. Before the fix the receiver slept after the first
 * packet and the second was dropped.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "routing_test_fixture.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "types/radio/radio_state.hpp"

namespace loramesher {
namespace test {

using SlotType = types::protocols::lora_mesh::SlotAllocation::SlotType;

class RxSlotListeningTests : public RoutingTestFixture {
   protected:
    static constexpr uint32_t kStepMs = 8u;

    SlotType CurrentSlotType(TestNode& node) {
        uint16_t slot = node.protocol->GetCurrentSlot();
        for (const auto& alloc : node.protocol->GetSlotTable()) {
            if (alloc.slot_number == slot) {
                return alloc.type;
            }
        }
        return SlotType::SLEEP;
    }

    void Tick() {
        time_controller_.AdvanceTime(kStepMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
};

/**
 * @brief A receiver must keep listening for a second packet that arrives later
 *        in the same CONTROL_RX slot.
 */
TEST_F(RxSlotListeningTests, SecondPacketInControlRxSlotIsNotDropped) {
    auto& mgr = CreateManagerNode("Mgr", 0x2001);
    auto& node = CreateJoiningNode("Node", 0x2002);
    // A second transmitter, used only to inject the "later" packet. Never
    // started; it serves purely as a delivery source on a one-way link so it
    // does not perturb the formed mgr<->node network.
    auto& src2 = CreateJoiningNode("Src2", 0x2003);

    SetLinkStatus(mgr, node, true);

    ASSERT_TRUE(StartNode(mgr));
    ASSERT_TRUE(StartNode(node));

    std::vector<TestNode*> nodes = {&mgr, &node};
    ASSERT_TRUE(WaitForNetworkFormation(nodes, 1));
    ASSERT_TRUE(WaitForRoutingStabilization(nodes));

    // Use the smallest control frame the manager has broadcast, so two
    // back-to-back deliveries comfortably fit inside one slot.
    std::vector<uint8_t> packet;
    for (const auto& m : virtual_network_.GetSentMessages(mgr.address)) {
        if (!m.empty() && (packet.empty() || m.size() < packet.size())) {
            packet = m;
        }
    }
    ASSERT_FALSE(packet.empty());

    virtual_network_.SetDirectionalLink(src2.address, node.address, true);
    virtual_network_.SetMessageDelay(mgr.address, node.address, 1);
    virtual_network_.SetMessageDelay(src2.address, node.address, 1);

    const uint32_t superframe = GetSuperframeDuration(node);
    const uint32_t max_search_ms = superframe * 20u + 5000u;

    // Advance, staying inside slot `slot0`, until `cond` holds or we run out of
    // slot. Returns true while still in slot0.
    auto run_in_slot = [&](uint16_t slot0, int max_ticks,
                           const std::function<bool()>& cond) -> bool {
        for (int i = 0; i < max_ticks; ++i) {
            if (cond()) {
                return true;
            }
            Tick();
            if (node.protocol->GetCurrentSlot() != slot0) {
                return false;
            }
        }
        return cond();
    };

    bool tested = false;
    bool second_not_dropped = false;
    uint16_t prev_slot = node.protocol->GetCurrentSlot();

    for (uint32_t elapsed = 0; elapsed < max_search_ms && !tested;
         elapsed += kStepMs) {
        Tick();

        // Act only at the start of a fresh CONTROL_RX slot, so the whole slot
        // is available for two deliveries (avoids wasted mid-slot retries).
        const uint16_t slot = node.protocol->GetCurrentSlot();
        const bool entered_new_slot = (slot != prev_slot);
        prev_slot = slot;
        if (!entered_new_slot ||
            CurrentSlotType(node) != SlotType::CONTROL_RX) {
            continue;
        }

        const uint16_t slot0 = slot;
        // Let the slot-transition handler arm the radio for reception.
        if (!run_in_slot(slot0, 5,
                         [&]() {
                             return virtual_network_.GetNodeRadioState(
                                        node.address) ==
                                    radio::RadioState::kReceive;
                         }) ||
            node.protocol->GetCurrentSlot() != slot0) {
            continue;
        }

        virtual_network_.ResetReceivedMessageCount(node.address);
        virtual_network_.ResetDroppedMessageCount(node.address);

        // First packet — must actually be delivered within this slot.
        virtual_network_.TransmitMessage(mgr.address, packet, -65.0f, 8.0f);
        bool in_slot = run_in_slot(slot0, 20, [&]() {
            return virtual_network_.GetReceivedMessageCount(node.address) >= 1;
        });
        if (!in_slot ||
            virtual_network_.GetReceivedMessageCount(node.address) < 1) {
            continue;  // not enough slot left here — try the next CONTROL_RX
        }

        // Let the protocol task apply its post-reception radio decision
        // (sleep, or — once fixed — stay in receive) before the next packet.
        for (int k = 0; k < 4 && node.protocol->GetCurrentSlot() == slot0;
             ++k) {
            Tick();
        }
        if (node.protocol->GetCurrentSlot() != slot0) {
            continue;
        }

        // Second packet — arrives later in the SAME slot.
        virtual_network_.TransmitMessage(src2.address, packet, -65.0f, 8.0f);
        in_slot = run_in_slot(slot0, 20, [&]() {
            return virtual_network_.GetReceivedMessageCount(node.address) >=
                       2 ||
                   virtual_network_.GetDroppedMessageCount(node.address) >= 1;
        });
        if (!in_slot &&
            virtual_network_.GetReceivedMessageCount(node.address) < 2 &&
            virtual_network_.GetDroppedMessageCount(node.address) == 0) {
            continue;  // second packet never landed inside the slot — retry
        }

        second_not_dropped =
            (virtual_network_.GetDroppedMessageCount(node.address) == 0 &&
             virtual_network_.GetReceivedMessageCount(node.address) >= 2);
        tested = true;
    }

    ASSERT_TRUE(tested)
        << "Could not deliver two packets within one CONTROL_RX "
           "slot to exercise the listening window";
    EXPECT_TRUE(second_not_dropped)
        << "Receiver dropped a packet that arrived later in the same "
           "CONTROL_RX "
           "slot — the radio slept after the first packet instead of listening "
           "for the whole slot";
}

}  // namespace test
}  // namespace loramesher
