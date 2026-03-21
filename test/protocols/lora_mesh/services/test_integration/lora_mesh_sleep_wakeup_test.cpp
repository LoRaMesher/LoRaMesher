/**
 * @file lora_mesh_sleep_wakeup_test.cpp
 * @brief Integration tests for sleep/wake-up power management callbacks
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "lora_mesh_test_fixture.hpp"
#include "types/power/power_types.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Tracks sleep/wake-up callback invocations for a single node
 */
struct SleepTracker {
    std::atomic<int> sleep_count{0};
    std::atomic<int> wake_count{0};
    bool veto_sleep{false};
    power::SleepContext last_sleep_context{};
    std::mutex context_mutex;
};

/**
 * @brief Integration test suite for sleep/wake-up power management
 */
class LoRaMeshSleepWakeUpTests : public LoRaMeshTestFixture {
   protected:
    std::map<AddressType, std::shared_ptr<SleepTracker>> trackers_;

    void SetUp() override { LoRaMeshTestFixture::SetUp(); }

    void TearDown() override { LoRaMeshTestFixture::TearDown(); }

    /**
     * @brief Create a node with sleep/wake-up callbacks that track invocations
     *
     * @param name Node name
     * @param address Node address
     * @param role Node role
     * @return TestNode& Reference to the created node
     */
    TestNode& CreateNodeWithSleepCallbacks(const std::string& name,
                                           AddressType address,
                                           NodeRole role = NodeRole::AUTO) {
        auto& node = CreateNode(name, address, role);

        auto tracker = std::make_shared<SleepTracker>();
        trackers_[address] = tracker;

        // Re-configure the node with sleep/wake-up callbacks
        LoRaMeshProtocolConfig config(address);
        config.setNodeRole(role);
        config.setTargetDutyCycle(1.0f);

        config.setPrepareSleepCallback(
            [tracker](const power::SleepContext& ctx) -> power::SleepResult {
                tracker->sleep_count.fetch_add(1, std::memory_order_relaxed);
                {
                    std::lock_guard<std::mutex> lock(tracker->context_mutex);
                    tracker->last_sleep_context = ctx;
                }
                return power::SleepResult{!tracker->veto_sleep};
            });

        config.setWakeUpCallback(
            [tracker](power::PowerState /* previous_state */) {
                tracker->wake_count.fetch_add(1, std::memory_order_relaxed);
            });

        auto result = node.protocol->Configure(config);
        EXPECT_TRUE(result) << "Failed to reconfigure node with callbacks: "
                            << result.GetErrorMessage();

        return node;
    }

    /**
     * @brief Get the sleep tracker for a node
     */
    SleepTracker& GetTracker(AddressType address) {
        return *trackers_.at(address);
    }

    /**
     * @brief Wait for tasks to execute
     */
    void WaitForTasksToExecute() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    /**
     * @brief Start a node and wait until it reaches NETWORK_MANAGER state
     */
    void StartNodeUntilNetworkManager(TestNode& node) {
        ASSERT_TRUE(StartNode(node));
        WaitForTasksToExecute();

        auto discovery_timeout = GetDiscoveryTimeout(node);
        bool became_manager = AdvanceTime(
            discovery_timeout + 500, discovery_timeout + 500, 15, 0, [&]() {
                return node.protocol->GetState() ==
                       protocols::lora_mesh::INetworkService::ProtocolState::
                           NETWORK_MANAGER;
            });
        ASSERT_TRUE(became_manager) << "Node 0x" << std::hex << node.address
                                    << " did not become network manager";
    }

    /**
     * @brief Start a node and wait until it reaches NORMAL_OPERATION state
     */
    void StartNodeUntilNormalOperation(TestNode& node) {
        ASSERT_TRUE(StartNode(node));
        WaitForTasksToExecute();

        auto discovery_timeout = GetDiscoveryTimeout(node);
        bool found_network = AdvanceTime(
            discovery_timeout + 500, discovery_timeout + 500, 15, 0, [&]() {
                auto state = node.protocol->GetState();
                return state == protocols::lora_mesh::INetworkService::
                                    ProtocolState::JOINING;
            });
        ASSERT_TRUE(found_network) << "Node 0x" << std::hex << node.address
                                   << " did not discover network";

        auto superframe_duration = GetSuperframeDuration(node);
        auto slot_duration = GetSlotDuration(node);
        uint32_t join_timeout = (superframe_duration > slot_duration)
                                    ? superframe_duration * 3
                                    : slot_duration * 10;

        bool joined = AdvanceTime(join_timeout, join_timeout, 15, 0, [&]() {
            return node.protocol->GetState() ==
                   protocols::lora_mesh::INetworkService::ProtocolState::
                       NORMAL_OPERATION;
        });
        ASSERT_TRUE(joined) << "Node 0x" << std::hex << node.address
                            << " did not reach NORMAL_OPERATION";
    }
};

/**
 * @brief Verify sleep and wake-up callbacks fire for a single network manager
 */
TEST_F(LoRaMeshSleepWakeUpTests, SingleNodeSleepWakeUpCallbacks) {
    auto& node = CreateNodeWithSleepCallbacks("Node1", 0x1001,
                                              NodeRole::NETWORK_MANAGER);

    StartNodeUntilNetworkManager(node);
    WaitForTasksToExecute();

    auto& tracker = GetTracker(node.address);

    // Use a generous timeout and condition-based advance so the superframe
    // timer and protocol task have enough virtual-time steps to process slots
    auto slot_duration = GetSlotDuration(node);
    uint32_t timeout =
        std::max(GetSuperframeDuration(node) * 10, slot_duration * 50);

    bool sleep_fired = AdvanceTime(timeout, timeout, 15, 0, [&]() {
        return tracker.sleep_count.load() > 0 && tracker.wake_count.load() > 0;
    });

    EXPECT_TRUE(sleep_fired) << "Sleep callback should have been invoked";
    EXPECT_GT(tracker.wake_count.load(), 0)
        << "Wake-up callback should have been invoked";
}

/**
 * @brief Verify sleep/wake callbacks fire independently on two nodes
 */
TEST_F(LoRaMeshSleepWakeUpTests, TwoNodeSleepWakeUpCallbacks) {
    auto& manager = CreateNodeWithSleepCallbacks("Manager", 0x1001,
                                                 NodeRole::NETWORK_MANAGER);
    auto& joiner =
        CreateNodeWithSleepCallbacks("Joiner", 0x1002, NodeRole::NODE_ONLY);

    SetLinkStatus(manager, joiner, true);

    StartNodeUntilNetworkManager(manager);
    StartNodeUntilNormalOperation(joiner);
    WaitForTasksToExecute();

    auto& manager_tracker = GetTracker(manager.address);
    auto& joiner_tracker = GetTracker(joiner.address);

    auto slot_duration = GetSlotDuration(manager);
    uint32_t timeout =
        std::max(GetSuperframeDuration(manager) * 10, slot_duration * 50);

    bool both_slept = AdvanceTime(timeout, timeout, 15, 0, [&]() {
        return manager_tracker.sleep_count.load() > 0 &&
               joiner_tracker.sleep_count.load() > 0;
    });

    EXPECT_TRUE(both_slept)
        << "Both nodes' sleep callbacks should have been invoked";
    EXPECT_GT(manager_tracker.wake_count.load(), 0)
        << "Manager wake-up callback should have been invoked";
    EXPECT_GT(joiner_tracker.wake_count.load(), 0)
        << "Joiner wake-up callback should have been invoked";
}

/**
 * @brief Verify that vetoing sleep prevents the wake-up callback from firing
 */
TEST_F(LoRaMeshSleepWakeUpTests, SleepVetoPreventsWakeCallback) {
    auto& node = CreateNodeWithSleepCallbacks("Node1", 0x1001, NodeRole::AUTO);

    // Enable veto before starting
    auto& tracker = GetTracker(node.address);
    tracker.veto_sleep = true;

    StartNodeUntilNetworkManager(node);
    WaitForTasksToExecute();

    auto slot_duration = GetSlotDuration(node);
    uint32_t timeout =
        std::max(GetSuperframeDuration(node) * 10, slot_duration * 50);

    bool sleep_fired = AdvanceTime(timeout, timeout, 15, 0, [&]() {
        return tracker.sleep_count.load() > 0;
    });

    EXPECT_TRUE(sleep_fired)
        << "Sleep callback should have been invoked even when vetoing";
    EXPECT_EQ(tracker.wake_count.load(), 0)
        << "Wake-up callback should NOT fire when sleep is vetoed";
}

/**
 * @brief Verify the SleepContext passed to the callback contains valid data
 */
TEST_F(LoRaMeshSleepWakeUpTests, SleepContextHasValidData) {
    auto& node = CreateNodeWithSleepCallbacks("Node1", 0x1001, NodeRole::AUTO);

    StartNodeUntilNetworkManager(node);
    WaitForTasksToExecute();

    auto& tracker = GetTracker(node.address);

    auto slot_duration = GetSlotDuration(node);
    uint32_t timeout = GetSuperframeDuration(node) * 4;

    bool sleep_fired = AdvanceTime(timeout, timeout, 15, 0, [&]() {
        return tracker.sleep_count.load() > 0;
    });

    ASSERT_TRUE(sleep_fired) << "Sleep callback must have been invoked";

    // Check the last context received
    std::lock_guard<std::mutex> lock(tracker.context_mutex);
    auto& ctx = tracker.last_sleep_context;

    EXPECT_EQ(ctx.requested_state, power::PowerState::LIGHT_SLEEP)
        << "Requested state should be LIGHT_SLEEP";
    EXPECT_GT(ctx.sleep_duration_ms, 0u)
        << "Sleep duration should be greater than zero";

    // Sleep duration should be slot_duration minus wake_up_guard
    EXPECT_LE(ctx.sleep_duration_ms, slot_duration)
        << "Sleep duration should not exceed slot duration";
}

/**
 * @brief Verify the network continues to function after sleep/wake cycles
 */
TEST_F(LoRaMeshSleepWakeUpTests, NetworkFunctionsAfterSleepWakeCycles) {
    auto& manager = CreateNodeWithSleepCallbacks("Manager", 0x1001,
                                                 NodeRole::NETWORK_MANAGER);
    auto& joiner =
        CreateNodeWithSleepCallbacks("Joiner", 0x1002, NodeRole::NODE_ONLY);

    SetLinkStatus(manager, joiner, true);

    StartNodeUntilNetworkManager(manager);
    StartNodeUntilNormalOperation(joiner);
    WaitForTasksToExecute();

    auto& manager_tracker = GetTracker(manager.address);

    auto slot_duration = GetSlotDuration(manager);
    uint32_t timeout = GetSuperframeDuration(manager) * 4;

    // Wait for at least one sleep cycle
    bool sleep_fired = AdvanceTime(timeout, timeout, 15, 0, [&]() {
        return manager_tracker.sleep_count.load() > 0 &&
               manager_tracker.wake_count.load() > 0;
    });
    ASSERT_TRUE(sleep_fired) << "Sleep cycles should have occurred";

    // Send a data message from joiner to manager
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    auto send_result = SendMessage(joiner, manager, payload);
    EXPECT_TRUE(send_result)
        << "SendData should succeed: " << send_result.GetErrorMessage();

    // Wait for message delivery with condition
    bool received = AdvanceTime(timeout, timeout, 15, 0, [&]() {
        return HasReceivedMessageFrom(manager, joiner.address);
    });
    EXPECT_TRUE(received)
        << "Manager should have received data from joiner after sleep cycles";
}

}  // namespace test
}  // namespace loramesher
