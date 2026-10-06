/**
 * @file reliable_messaging_test.cpp
 * @brief Unit tests for ReliableMessaging driven through a scripted host.
 */

#include <gtest/gtest.h>

#include <vector>

#include "protocols/lora_mesh/services/reliable_messaging.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

namespace {

constexpr AddressType kLocal = 0x0001;
constexpr AddressType kPeer = 0x0002;
constexpr AddressType kGroup = 0x8001;
constexpr uint32_t kSuperframeMs = 1000;

}  // namespace

class ReliableMessagingTest : public ::testing::Test {
   protected:
    void SetUp() override {
        messaging_ = std::make_unique<ReliableMessaging>(message_cache_,
                                                         MakeHost());
        messaging_->SetDeliveryCallback(
            [this](const reliability::DeliveryResult& result) {
                outcomes_.push_back(result);
            });
    }

    ReliableMessaging::Host MakeHost() {
        ReliableMessaging::Host host;
        host.node_address = kLocal;
        host.now_ms = [this]() {
            return now_ms_;
        };
        host.enqueue =
            [this](types::protocols::lora_mesh::SlotAllocation::SlotType,
                   std::unique_ptr<BaseMessage>) {
                if (!queue_accepts_) {
                    return Result(LoraMesherErrorCode::kQueueFull,
                                  "Queue full");
                }
                ++enqueued_;
                return Result::Success();
            };
        host.find_next_hop = [](AddressType dest) {
            return dest;
        };
        host.forward_data_message = [](const DataMessage&) {
            return Result::Success();
        };
        host.deliver_to_app = [](AddressType, uint8_t, AddressType, uint8_t,
                                 std::span<const uint8_t>) {
        };
        host.in_operational_state = []() {
            return true;
        };
        host.max_hops = []() -> uint8_t {
            return 4;
        };
        host.max_packet_size = []() -> uint16_t {
            return 255;
        };
        host.superframe_duration = []() -> uint32_t {
            return kSuperframeMs;
        };
        return host;
    }

    /// Advance the clock superframe by superframe, running the timers.
    void RunFor(uint32_t duration_ms) {
        for (uint32_t elapsed = 0; elapsed < duration_ms;
             elapsed += kSuperframeMs) {
            now_ms_ += kSuperframeMs;
            messaging_->ProcessReliableTimers();
        }
    }

    MessageCache message_cache_;
    std::unique_ptr<ReliableMessaging> messaging_;
    std::vector<reliability::DeliveryResult> outcomes_;
    uint32_t now_ms_ = 1000;
    bool queue_accepts_ = true;
    int enqueued_ = 0;
    const std::vector<uint8_t> payload_ = {1, 2, 3};
};

/**
 * @brief A group send whose attempts can never be queued fails and releases
 *        its acknowledgement window, so later group sends still close.
 */
TEST_F(ReliableMessagingTest, FailedGroupSendReleasesItsWindow) {
    constexpr uint32_t kWindowMs = 60000;
    queue_accepts_ = false;
    for (size_t i = 0; i < reliability::ReliableDelivery::kMaxPending; ++i) {
        ASSERT_NE(messaging_->SendGroupReliable(kGroup, payload_, 0, kWindowMs)
                      .source,
                  0u);
    }
    RunFor(20 * kSuperframeMs);
    ASSERT_EQ(messaging_->GetReliablePendingCount(), 0u);

    queue_accepts_ = true;
    outcomes_.clear();
    const auto id = messaging_->SendGroupReliable(kGroup, payload_, 0, 5000);
    ASSERT_NE(id.source, 0u);
    RunFor(10 * kSuperframeMs);

    EXPECT_EQ(messaging_->GetReliablePendingCount(), 0u);
    ASSERT_EQ(outcomes_.size(), 1u);
    EXPECT_EQ(outcomes_[0].outcome, reliability::Outcome::GroupWindowClosed);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
