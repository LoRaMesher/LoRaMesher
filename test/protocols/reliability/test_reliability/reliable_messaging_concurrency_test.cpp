/**
 * @file reliable_messaging_concurrency_test.cpp
 * @brief ReliableMessaging driven concurrently from an application thread and
 *        the protocol task.
 *
 * The application thread sends reliable unicast and group messages while the
 * protocol thread advances the retransmission timers. Run under the
 * ThreadSanitizer environment (test_native_tsan) to detect data races.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
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

}  // namespace

class ReliableMessagingConcurrencyTest : public ::testing::Test {
   protected:
    ReliableMessaging::Host MakeHost() {
        ReliableMessaging::Host host;
        host.node_address = kLocal;
        host.now_ms = [this]() {
            return clock_ms_.fetch_add(50, std::memory_order_relaxed);
        };
        host.enqueue =
            [this](types::protocols::lora_mesh::SlotAllocation::SlotType,
                   std::unique_ptr<BaseMessage>) {
                enqueued_.fetch_add(1, std::memory_order_relaxed);
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
        return host;
    }

    std::atomic<uint32_t> clock_ms_{1000};
    std::atomic<uint32_t> enqueued_{0};
    MessageCache message_cache_;
};

TEST_F(ReliableMessagingConcurrencyTest,
       SendersAndTimersRunOnDifferentThreads) {
    ReliableMessaging messaging(message_cache_, MakeHost());
    std::atomic<uint32_t> outcomes{0};
    messaging.SetDeliveryCallback(
        [&outcomes](const reliability::DeliveryResult&) {
            outcomes.fetch_add(1, std::memory_order_relaxed);
        });

    constexpr int kIterations = 2000;
    std::atomic<bool> sending{true};
    const std::vector<uint8_t> payload = {1, 2, 3, 4};

    std::thread app([&]() {
        for (int i = 0; i < kIterations; ++i) {
            if (i % 4 == 0) {
                messaging.SendGroupReliable(kGroup, payload, 0,
                                            /*window_ms=*/200);
            } else {
                messaging.SendReliable(kPeer, payload, /*max_retries=*/1,
                                       /*timeout_override_ms=*/100);
            }
        }
        sending.store(false);
    });

    std::thread protocol([&]() {
        while (sending.load()) {
            messaging.ProcessReliableTimers();
            (void)messaging.GetReliablePendingCount();
        }
    });

    app.join();
    protocol.join();

    // Drain every outstanding entry.
    for (int i = 0; i < 1000 && messaging.GetReliablePendingCount() > 0; ++i) {
        messaging.ProcessReliableTimers();
    }
    EXPECT_EQ(messaging.GetReliablePendingCount(), 0u);
    EXPECT_GT(outcomes.load(), 0u);
}

TEST_F(ReliableMessagingConcurrencyTest, DeliveryCallbackMaySendAgain) {
    ReliableMessaging messaging(message_cache_, MakeHost());
    const std::vector<uint8_t> payload = {1};
    int resends = 0;
    messaging.SetDeliveryCallback(
        [&](const reliability::DeliveryResult& result) {
            if (result.outcome == reliability::Outcome::Failed &&
                resends == 0) {
                ++resends;
                messaging.SendReliable(kPeer, payload, 0, 100);
            }
        });

    ASSERT_NE(messaging.SendReliable(kPeer, payload, 0, 100).source, 0u);
    for (int i = 0; i < 100 && resends == 0; ++i) {
        messaging.ProcessReliableTimers();
    }

    EXPECT_EQ(resends, 1);
    EXPECT_EQ(messaging.GetReliablePendingCount(), 1u);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
