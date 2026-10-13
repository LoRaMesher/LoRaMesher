/**
 * @file routing_slice_airtime_test.cpp
 * @brief Tests that sliced routing broadcasts fit their control slot and that
 *        a slice whose transmission was skipped is not lost
 */

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "os/os_port.hpp"
#include "protocols/lora_mesh/services/message_queue_service.hpp"
#include "protocols/lora_mesh/services/network_service.hpp"
#include "protocols/lora_mesh/services/slot_tx_budget.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"
#include "types/messages/loramesher/routing_table_message.hpp"

#ifdef ARDUINO

TEST(RoutingSliceAirtimeTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

#include "os/rtos_mock.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

class RoutingSliceAirtimeTest : public ::testing::Test {
   protected:
    static constexpr AddressType kNodeAddress = 0x1001;
    static constexpr uint16_t kSlots = 20;
    static constexpr uint32_t kSlotMs = 1000;
    static constexpr uint32_t kGuardMs = 50;
    /// Time on air of the network service without a radio (ms per byte)
    static constexpr uint32_t kToaPerByteMs = 10;

    void SetUp() override {
        queue_ = std::make_shared<MessageQueueService>(10);
        superframe_ = std::make_shared<SuperframeService>(kSlots, kSlotMs);
        service_ = std::make_unique<NetworkService>(kNodeAddress, queue_,
                                                    superframe_, nullptr);
        INetworkService::NetworkConfig cfg;
        cfg.node_address = kNodeAddress;
        cfg.max_hops = 5;
        cfg.max_packet_size = 255;
        cfg.guard_time_ms = kGuardMs;
        cfg.default_data_slots = 1;
        cfg.max_network_nodes = 30;
        ASSERT_TRUE(service_->Configure(cfg));
        for (AddressType i = 0; i < 20; ++i) {
            const auto neighbour = static_cast<AddressType>(0x2000 + i);
            service_->UpdateRouteEntry(neighbour, neighbour, 1, 200, 1, 0);
        }
    }

    static size_t MessageSize(size_t entries) {
        return BaseHeader::Size() +
               RoutingTableHeader::RoutingTableFieldsSize() +
               entries * RoutingTableEntry::Size();
    }

    std::vector<AddressType> Destinations(const BaseMessage& message) {
        auto routing = RoutingTableMessage::CreateFromBaseMessage(message);
        EXPECT_TRUE(routing.has_value());
        std::vector<AddressType> destinations;
        if (routing) {
            for (const auto& entry : routing->GetEntries()) {
                destinations.push_back(entry.destination);
            }
        }
        return destinations;
    }

    std::shared_ptr<MessageQueueService> queue_;
    std::shared_ptr<SuperframeService> superframe_;
    std::unique_ptr<NetworkService> service_;
};

TEST_F(RoutingSliceAirtimeTest, SliceFitsTheControlSlotAfterAStartDelay) {
    // The packet size alone would allow 24 entries; the slot's airtime,
    // after the start latency the slot must tolerate, allows fewer
    const size_t capacity = service_->ComputeBroadcastSliceCapacity();
    ASSERT_GT(capacity, 0u);
    EXPECT_TRUE(FitsInSlot(kTxStartLatencyBudgetMs, kGuardMs,
                           MessageSize(capacity) * kToaPerByteMs, kSlotMs));
    EXPECT_FALSE(FitsInSlot(kTxStartLatencyBudgetMs, kGuardMs,
                            MessageSize(capacity + 1) * kToaPerByteMs,
                            kSlotMs));

    auto message = service_->CreateRoutingTableMessage();
    ASSERT_NE(message, nullptr);
    EXPECT_TRUE(FitsInSlot(kTxStartLatencyBudgetMs, kGuardMs,
                           message->GetTotalSize() * kToaPerByteMs, kSlotMs));
}

TEST_F(RoutingSliceAirtimeTest, UnsentSliceIsBroadcastNextTime) {
    auto skipped = service_->CreateRoutingTableMessage();
    ASSERT_NE(skipped, nullptr);
    const auto skipped_routes = Destinations(*skipped);
    ASSERT_FALSE(skipped_routes.empty());

    // The transmission was skipped: the next message carries the same routes
    auto retried = service_->CreateRoutingTableMessage();
    ASSERT_NE(retried, nullptr);
    EXPECT_EQ(Destinations(*retried), skipped_routes);

    // Sent: the rotation moves on
    ASSERT_TRUE(retried->InvokePreSendCallback());
    auto next = service_->CreateRoutingTableMessage();
    ASSERT_NE(next, nullptr);
    EXPECT_NE(Destinations(*next), skipped_routes);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO
