/**
 * @file election_backoff_test.cpp
 * @brief Network Manager election backoff in FAULT_RECOVERY
 *
 * Each node's election backoff is staggered by role, address and jitter so
 * that the highest-priority node claims first and the others hear its
 * NM_CLAIM. These tests check that the backoff expires at its own deadline
 * (not at the next superframe start) and that a node waiting for its backoff
 * listens on every slot.
 */

#include <gtest/gtest.h>

#include <memory>

#include "os/os_port.hpp"
#include "protocols/lora_mesh/services/message_queue_service.hpp"
#include "protocols/lora_mesh/services/network_service.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"

#ifdef ARDUINO

TEST(ElectionBackoffTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

#include "os/rtos_mock.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

class ElectionBackoffTest : public ::testing::Test {
   protected:
    static constexpr AddressType kNodeAddress = 0x1080;
    /// Longer than any backoff: listen window + role bonus + address bonus
    /// + jitter
    static constexpr uint32_t kBeyondAnyBackoffMs = 20000;

    void SetUp() override {
        mock_ = dynamic_cast<os::RTOSMock*>(&GetRTOS());
        ASSERT_NE(mock_, nullptr);
        mock_->SeedRandom(42);
        mock_->setTimeMode(os::RTOSMock::TimeMode::kVirtualTime);

        message_queue_ = std::make_shared<MessageQueueService>(10);
        superframe_ = std::make_shared<SuperframeService>();
        service_ = std::make_unique<NetworkService>(
            kNodeAddress, message_queue_, superframe_, nullptr);

        INetworkService::NetworkConfig cfg;
        cfg.node_address = kNodeAddress;
        cfg.node_role = NodeRole::AUTO;
        cfg.max_hops = 5;
        cfg.max_packet_size = 255;
        cfg.default_data_slots = 2;
        cfg.max_network_nodes = 20;
        ASSERT_TRUE(service_->Configure(cfg));
    }

    void TearDown() override {
        service_.reset();
        superframe_.reset();
        message_queue_.reset();
        mock_->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
    }

    /// Lose the network manager: FAULT_RECOVERY with an election backoff
    void EnterFaultRecovery() {
        service_->SetState(INetworkService::ProtocolState::FAULT_RECOVERY);
        service_->StartElectionBackoff();
        ASSERT_TRUE(service_->IsElectionPending());
    }

    os::RTOSMock* mock_ = nullptr;
    std::shared_ptr<MessageQueueService> message_queue_;
    std::shared_ptr<SuperframeService> superframe_;
    std::unique_ptr<NetworkService> service_;
};

TEST_F(ElectionBackoffTest, RemainingBackoffCountsDown) {
    EnterFaultRecovery();

    const uint32_t remaining = service_->GetElectionBackoffRemaining();
    ASSERT_GT(remaining, 1000u);

    mock_->advanceTime(1000);
    EXPECT_EQ(service_->GetElectionBackoffRemaining(), remaining - 1000);

    mock_->advanceTime(remaining);
    EXPECT_EQ(service_->GetElectionBackoffRemaining(), 0u);
}

TEST_F(ElectionBackoffTest, ExpiredBackoffStartsElectionWithoutSuperframe) {
    EnterFaultRecovery();
    mock_->advanceTime(kBeyondAnyBackoffMs);

    service_->CheckElectionBackoff();

    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::NM_ELECTION);
    EXPECT_TRUE(message_queue_->HasMessage(MessageType::NM_CLAIM));
}

TEST_F(ElectionBackoffTest, PendingBackoffKeepsWaiting) {
    EnterFaultRecovery();

    service_->CheckElectionBackoff();

    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::FAULT_RECOVERY);
    EXPECT_FALSE(message_queue_->HasMessage(MessageType::NM_CLAIM));
}

TEST_F(ElectionBackoffTest, BackoffSpanningTickWrapStillWaits) {
    // Move the 32-bit tick counter to 3 s before it wraps; every backoff is
    // longer than that, so its deadline lies past the wrap.
    const uint32_t now = mock_->getTickCount();
    mock_->advanceTime(UINT32_MAX - now - 3000);
    EnterFaultRecovery();

    EXPECT_GT(service_->GetElectionBackoffRemaining(), 3000u);
    service_->CheckElectionBackoff();
    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::FAULT_RECOVERY);

    mock_->advanceTime(kBeyondAnyBackoffMs);
    EXPECT_EQ(service_->GetElectionBackoffRemaining(), 0u);
    service_->CheckElectionBackoff();
    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::NM_ELECTION);
}

TEST_F(ElectionBackoffTest, WaitingNodeListensOnEverySlot) {
    EnterFaultRecovery();

    auto table = service_->GetSlotTable();
    ASSERT_FALSE(table.empty());
    for (const auto& slot : table) {
        EXPECT_TRUE(slot.IsDiscoverySlot())
            << "Slot " << slot.slot_number
            << " cannot hear another node's NM_CLAIM";
    }
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO
