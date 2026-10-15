/**
 * @file join_contention_test.cpp
 * @brief Join request scheduling: discovery-slot choice, retry backoff and
 * retry state across rejoins
 *
 * The discovery band is a sequence of request/response slot pairs. A joiner
 * sends its JOIN_REQUEST in an even discovery slot so the network manager can
 * answer in the following odd slot of the same superframe. These tests drive
 * NetworkService directly: HandleSuperframeStart() and
 * HandleDiscoverySlotStart() stand in for the slot transitions, and removing
 * the queued JOIN_REQUEST stands in for its transmission.
 */

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "os/os_port.hpp"
#include "protocols/lora_mesh/services/message_queue_service.hpp"
#include "protocols/lora_mesh/services/network_service.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"
#include "types/messages/loramesher/join_response_message.hpp"
#include "types/messages/loramesher/sync_beacon_message.hpp"

#ifdef ARDUINO

TEST(JoinContentionTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

#include "os/rtos_mock.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

class JoinContentionTest : public ::testing::Test {
   protected:
    static constexpr AddressType kNodeAddress = 0x1001;
    static constexpr AddressType kNMAddress = 0x2000;
    static constexpr AddressType kSponsorAddress = 0x3003;
    static constexpr uint16_t kNetworkId = 0xABCD;
    static constexpr uint8_t kTotalSlots = 20;
    static constexpr uint16_t kSlotDurationMs = 1000;
    static constexpr int kUnansweredSuperframes = 60;
    static constexpr int kNoRequest = -1;

    void SetUp() override {
        if (auto* mock = dynamic_cast<os::RTOSMock*>(&GetRTOS())) {
            mock->SeedRandom(42);
        }
        message_queue_ = std::make_shared<MessageQueueService>(10);
        superframe_ = std::make_shared<SuperframeService>();
        service_ = std::make_unique<NetworkService>(
            kNodeAddress, message_queue_, superframe_, nullptr);

        INetworkService::NetworkConfig cfg;
        cfg.node_address = kNodeAddress;
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
    }

    /// Hear a sync beacon while in DISCOVERY, which starts joining
    void EnterJoining(uint8_t depth, AddressType beacon_source) {
        service_->StartDiscovery(5000);
        auto beacon = beacon_source == kNMAddress
                          ? SyncBeaconMessage::CreateOriginal(
                                0xFFFF, kNMAddress, kNetworkId, kTotalSlots,
                                kSlotDurationMs, kNMAddress, 0, depth)
                          : SyncBeaconMessage::CreateForwarded(
                                0xFFFF, beacon_source, kNetworkId, kTotalSlots,
                                kSlotDurationMs, kNMAddress, 1, 0, 0, depth);
        ASSERT_TRUE(beacon.has_value());
        ASSERT_TRUE(
            service_->ProcessReceivedMessage(beacon->ToBaseMessage(), 0));
        ASSERT_EQ(service_->GetState(),
                  INetworkService::ProtocolState::JOINING);
    }

    static uint8_t DiscoverySlots(uint8_t depth) { return (depth + 1) * 2; }

    /// Step through the discovery band; returns the slot a JOIN_REQUEST was
    /// sent in, or kNoRequest
    int RunDiscoveryBand(uint8_t discovery_slots) {
        int sent_at = kNoRequest;
        for (uint8_t i = 0; i < discovery_slots; i++) {
            service_->HandleDiscoverySlotStart(i);
            if (message_queue_->HasMessage(MessageType::JOIN_REQUEST)) {
                EXPECT_EQ(sent_at, kNoRequest)
                    << "More than one JOIN_REQUEST in one superframe";
                sent_at = i;
                message_queue_->RemoveMessage(MessageType::JOIN_REQUEST);
            }
        }
        return sent_at;
    }

    int RunSuperframe(uint8_t discovery_slots) {
        EXPECT_TRUE(service_->HandleSuperframeStart());
        return RunDiscoveryBand(discovery_slots);
    }

    /// Superframes (0 = the one joining started in) holding a JOIN_REQUEST
    std::vector<int> AttemptSuperframes(uint8_t discovery_slots) {
        std::vector<int> attempts;
        if (RunDiscoveryBand(discovery_slots) != kNoRequest) {
            attempts.push_back(0);
        }
        for (int s = 1; s <= kUnansweredSuperframes; s++) {
            if (RunSuperframe(discovery_slots) != kNoRequest) {
                attempts.push_back(s);
            }
        }
        return attempts;
    }

    void ReceiveJoinResponse(JoinResponseHeader::ResponseStatus status) {
        auto response = JoinResponseMessage::Create(
            kNodeAddress, kNMAddress, kNetworkId, 2, status, {}, 0, 0, 1);
        ASSERT_TRUE(response.has_value());
        ASSERT_TRUE(
            service_->ProcessReceivedMessage(response->ToBaseMessage(), 0));
    }

    /// Let the first attempts go unanswered until the retry count grows
    void FailFirstAttempts() {
        const uint8_t slots = DiscoverySlots(0);
        RunDiscoveryBand(slots);
        for (int s = 0; s < 3; s++) {
            RunSuperframe(slots);
        }
        ASSERT_GE(service_->GetJoinRetryCount(), 1);
    }

    std::shared_ptr<MessageQueueService> message_queue_;
    std::shared_ptr<SuperframeService> superframe_;
    std::unique_ptr<NetworkService> service_;
};

TEST_F(JoinContentionTest, RequestWaitsForItsDiscoverySlot) {
    EnterJoining(0, kNMAddress);

    EXPECT_FALSE(message_queue_->HasMessage(MessageType::JOIN_REQUEST))
        << "JOIN_REQUEST queued before its discovery slot started";

    service_->HandleDiscoverySlotStart(0);
    EXPECT_TRUE(message_queue_->HasMessage(MessageType::JOIN_REQUEST));
}

TEST_F(JoinContentionTest, DirectJoinUsesEvenDiscoverySlots) {
    constexpr uint8_t kDepth = 1;
    EnterJoining(kDepth, kNMAddress);

    const uint8_t slots = DiscoverySlots(kDepth);
    int first_pair = 0;
    int second_pair = 0;
    for (int s = 0; s <= kUnansweredSuperframes; s++) {
        int sent_at = (s == 0) ? RunDiscoveryBand(slots) : RunSuperframe(slots);
        if (sent_at == kNoRequest) {
            continue;
        }
        EXPECT_EQ(sent_at % 2, 0) << "Request sent in a response slot";
        EXPECT_LE(sent_at, 2 * kDepth);
        (sent_at == 0 ? first_pair : second_pair)++;
    }
    EXPECT_GT(first_pair, 0);
    EXPECT_GT(second_pair, 0) << "Requests never used the second slot pair";
}

TEST_F(JoinContentionTest, SponsoredJoinUsesFirstDiscoverySlot) {
    constexpr uint8_t kDepth = 2;
    EnterJoining(kDepth, kSponsorAddress);

    const uint8_t slots = DiscoverySlots(kDepth);
    int attempts = 0;
    for (int s = 0; s <= kUnansweredSuperframes; s++) {
        int sent_at = (s == 0) ? RunDiscoveryBand(slots) : RunSuperframe(slots);
        if (sent_at == kNoRequest) {
            continue;
        }
        EXPECT_EQ(sent_at, 0) << "Sponsored request left the first slot";
        attempts++;
    }
    EXPECT_GT(attempts, 0);
}

TEST_F(JoinContentionTest, FirstAttemptInTheJoiningSuperframe) {
    EnterJoining(0, kNMAddress);
    EXPECT_EQ(RunDiscoveryBand(DiscoverySlots(0)), 0);
}

TEST_F(JoinContentionTest, UnansweredAttemptsBackOffWithinWindow) {
    EnterJoining(0, kNMAddress);

    std::vector<int> attempts = AttemptSuperframes(DiscoverySlots(0));
    ASSERT_GE(attempts.size(), 2u);
    EXPECT_EQ(attempts.front(), 0);

    bool backed_off = false;
    for (size_t i = 1; i < attempts.size(); i++) {
        int gap = attempts[i] - attempts[i - 1];
        EXPECT_GE(gap, 1);
        EXPECT_LE(gap, 4) << "Backoff exceeded its 4-superframe window";
        backed_off |= gap > 1;
    }
    EXPECT_TRUE(backed_off) << "Unanswered attempts never backed off";
}

TEST_F(JoinContentionTest, SponsoredRetryWaitsAtLeastOneSuperframe) {
    constexpr uint8_t kDepth = 2;
    EnterJoining(kDepth, kSponsorAddress);

    std::vector<int> attempts = AttemptSuperframes(DiscoverySlots(kDepth));
    ASSERT_GE(attempts.size(), 2u);
    for (size_t i = 1; i < attempts.size(); i++) {
        int gap = attempts[i] - attempts[i - 1];
        EXPECT_GE(gap, 2) << "Retried while the sponsored response could "
                             "still be on its way";
        EXPECT_LE(gap, 5);
    }
}

TEST_F(JoinContentionTest, RetryCountSurvivesRejoin) {
    EnterJoining(0, kNMAddress);
    FailFirstAttempts();
    const uint8_t retries = service_->GetJoinRetryCount();

    service_->SetState(INetworkService::ProtocolState::FAULT_RECOVERY);
    service_->StartDiscovery(5000);
    ASSERT_TRUE(service_->StartJoining(kNMAddress, service_->GetJoinTimeout()));

    EXPECT_EQ(service_->GetJoinRetryCount(), retries)
        << "Rejoining reset the retry state, re-synchronising joiners";
}

TEST_F(JoinContentionTest, AcceptedResetsRetryState) {
    EnterJoining(0, kNMAddress);
    FailFirstAttempts();

    ReceiveJoinResponse(JoinResponseHeader::ResponseStatus::ACCEPTED);

    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::NORMAL_OPERATION);
    EXPECT_EQ(service_->GetJoinRetryCount(), 0);
}

TEST_F(JoinContentionTest, RetryLaterKeepsRetryCount) {
    EnterJoining(0, kNMAddress);
    FailFirstAttempts();
    const uint8_t retries = service_->GetJoinRetryCount();

    ReceiveJoinResponse(JoinResponseHeader::ResponseStatus::RETRY_LATER);

    EXPECT_EQ(service_->GetState(), INetworkService::ProtocolState::JOINING);
    EXPECT_EQ(service_->GetJoinRetryCount(), retries);
}

TEST_F(JoinContentionTest, JoinTimeoutSpansTheBackoffWindow) {
    EnterJoining(0, kNMAddress);
    EXPECT_EQ(service_->GetJoinTimeout(),
              superframe_->GetSuperframeDuration() * 13);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO
