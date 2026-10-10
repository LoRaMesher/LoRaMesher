/**
 * @file lora_mesh_restart_test.cpp
 * @brief Integration tests for stopping and restarting the LoRaMesh protocol
 */

#include <gtest/gtest.h>
#include <atomic>
#include <memory>
#include <vector>

#include "lora_mesh_test_fixture.hpp"
#include "types/power/power_types.hpp"

namespace loramesher {
namespace test {

namespace {
using ProtocolState = protocols::lora_mesh::INetworkService::ProtocolState;
using SlotType = types::protocols::lora_mesh::SlotAllocation::SlotType;
}  // namespace

/**
 * @brief Stop()/Start() cycles on nodes of a formed network
 *
 * Roles are pinned (one NETWORK_MANAGER, NODE_ONLY members) so a restarted
 * node always rejoins the same network.
 */
class LoRaMeshRestartTests : public LoRaMeshTestFixture {
   protected:
    void SetUp() override { LoRaMeshTestFixture::SetUp(); }

    void TearDown() override { LoRaMeshTestFixture::TearDown(); }

    /**
     * @brief Start a node and wait until it is the network manager
     */
    void StartUntilManager(TestNode& node) {
        ASSERT_TRUE(StartNode(node));
        uint32_t timeout = GetDiscoveryTimeout(node) * 2 + 500;
        ASSERT_TRUE(AdvanceTime(timeout, timeout, 15, 0,
                                [&]() {
                                    return node.protocol->GetState() ==
                                           ProtocolState::NETWORK_MANAGER;
                                }))
            << node.name << " did not become network manager";
    }

    /**
     * @brief Start a node and wait until it has joined the network
     */
    void StartUntilJoined(TestNode& node, TestNode& manager) {
        ASSERT_TRUE(StartNode(node));
        WaitUntilJoined(node, manager);
    }

    /**
     * @brief Advance time until a node is in NORMAL_OPERATION under @p manager
     */
    void WaitUntilJoined(TestNode& node, TestNode& manager) {
        uint32_t timeout = GetDiscoveryTimeout(node) * 4 +
                           GetSuperframeDuration(manager) * 6 + 2000;
        ASSERT_TRUE(AdvanceTime(
            timeout, timeout, 15, 0,
            [&]() {
                return node.protocol->GetState() ==
                           ProtocolState::NORMAL_OPERATION &&
                       node.protocol->GetNetworkManager() == manager.address;
            }))
            << node.name << " did not join the network of " << manager.name;
    }

    /**
     * @brief Send data and wait until the destination delivers it
     */
    void ExpectDelivery(TestNode& from, TestNode& to) {
        to.received_messages.clear();
        std::vector<uint8_t> payload = {0xC0, 0xFF, 0xEE};

        // A fresh member needs a superframe or two before it holds a route
        uint32_t ready_timeout = GetSuperframeDuration(from) * 4 + 2000;
        ASSERT_TRUE(
            AdvanceTime(ready_timeout, ready_timeout, 15, 0,
                        [&]() {
                            return static_cast<bool>(
                                from.protocol->IsReadyToSend(to.address));
                        }))
            << from.name << " never became ready to send to " << to.name;

        Result send_result = SendMessage(from, to, payload);
        ASSERT_TRUE(send_result) << send_result.GetErrorMessage();

        uint32_t timeout = GetSuperframeDuration(from) * 4 + 2000;
        EXPECT_TRUE(AdvanceTime(
            timeout, timeout, 15, 0,
            [&]() { return HasReceivedMessageFrom(to, from.address); }))
            << to.name << " did not receive data from " << from.name;
    }

    /**
     * @brief Stop a node and check the protocol is quiescent afterwards
     */
    void StopAndExpectIdle(TestNode& node) {
        Result result = StopNode(node);
        ASSERT_TRUE(result) << "Stop failed: " << result.GetErrorMessage();
        ExpectNoTaskMisuse();
        EXPECT_EQ(node.protocol->GetState(), ProtocolState::INITIALIZING);
        EXPECT_EQ(node.protocol->GetTxQueueSize(), 0u);
    }
};

/**
 * @brief A stopped member restarts, rejoins its manager and exchanges data
 */
TEST_F(LoRaMeshRestartTests, MemberStopStartRejoinsAndDelivers) {
    auto& manager = CreateManagerNode("Manager", 0x1001);
    auto& member = CreateJoiningNode("Member", 0x1002);
    SetLinkStatus(manager, member, true);

    StartUntilManager(manager);
    StartUntilJoined(member, manager);
    ExpectDelivery(member, manager);

    StopAndExpectIdle(member);
    AdvanceTime(GetSuperframeDuration(manager) * 2);

    StartUntilJoined(member, manager);
    ExpectNoTaskMisuse();
    ExpectDelivery(member, manager);
    ExpectDelivery(manager, member);
}

/**
 * @brief A member survives several consecutive stop/start cycles
 */
TEST_F(LoRaMeshRestartTests, RepeatedCyclesMember) {
    auto& manager = CreateManagerNode("Manager", 0x1001);
    auto& member = CreateJoiningNode("Member", 0x1002);
    SetLinkStatus(manager, member, true);

    StartUntilManager(manager);
    StartUntilJoined(member, manager);

    constexpr int kCycles = 5;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        SCOPED_TRACE(testing::Message() << "cycle " << cycle);
        StopAndExpectIdle(member);
        AdvanceTime(GetSlotDuration(manager) * 3);
        StartUntilJoined(member, manager);
        if (HasFatalFailure()) {
            return;
        }
    }

    ExpectNoTaskMisuse();
    ExpectDelivery(member, manager);
}

/**
 * @brief A restarted manager rebuilds its network and the member rejoins it
 */
TEST_F(LoRaMeshRestartTests, ManagerStopStart) {
    auto& manager = CreateManagerNode("Manager", 0x1001);
    auto& member = CreateJoiningNode("Member", 0x1002);
    SetLinkStatus(manager, member, true);

    StartUntilManager(manager);
    StartUntilJoined(member, manager);

    StopAndExpectIdle(manager);

    StartUntilManager(manager);
    ExpectNoTaskMisuse();

    // The member keeps its old membership until it misses the manager's
    // sync beacons, then rediscovers and joins the restarted manager
    uint32_t timeout =
        GetSuperframeDuration(manager) *
            (protocols::lora_mesh::kMaxNoReceivedSyncBeacons + 8) +
        GetDiscoveryTimeout(member) * 4;
    ASSERT_TRUE(AdvanceTime(timeout, timeout, 15, 0, [&]() {
        bool manager_knows_member = false;
        for (const auto& route : manager.protocol->GetNetworkNodes()) {
            manager_knows_member |=
                route.routing_entry.destination == member.address;
        }
        return manager_knows_member &&
               member.protocol->GetState() == ProtocolState::NORMAL_OPERATION;
    })) << "Member did not rejoin the restarted manager";
    ExpectDelivery(member, manager);
}

/**
 * @brief Repeated Stop() and Start() calls are no-ops that succeed
 */
TEST_F(LoRaMeshRestartTests, StopStartIdempotent) {
    auto& manager = CreateManagerNode("Manager", 0x1001);

    // Stop before the first Start
    ASSERT_TRUE(StopNode(manager));

    StartUntilManager(manager);
    ASSERT_TRUE(StartNode(manager));
    EXPECT_EQ(manager.protocol->GetState(), ProtocolState::NETWORK_MANAGER);

    StopAndExpectIdle(manager);
    Result second_stop = StopNode(manager);
    EXPECT_TRUE(second_stop) << second_stop.GetErrorMessage();

    StartUntilManager(manager);
    ExpectNoTaskMisuse();
}

/**
 * @brief Stop() issued while the node transmits in its own data slot
 * returns once the slot work is done, without killing the protocol task
 */
TEST_F(LoRaMeshRestartTests, StopDuringOwnDataSlotIsBounded) {
    auto& manager = CreateManagerNode("Manager", 0x1001);
    auto& member = CreateJoiningNode("Member", 0x1002);
    SetLinkStatus(manager, member, true);

    StartUntilManager(manager);
    StartUntilJoined(member, manager);
    uint32_t ready_timeout = GetSuperframeDuration(member) * 4 + 2000;
    ASSERT_TRUE(AdvanceTime(ready_timeout, ready_timeout, 15, 0, [&]() {
        return static_cast<bool>(
            member.protocol->IsReadyToSend(manager.address));
    }));

    auto is_own_tx_slot = [&](uint16_t slot) {
        for (const auto& allocation : member.protocol->GetSlotTable()) {
            if (allocation.slot_number == slot &&
                allocation.type == SlotType::TX) {
                return true;
            }
        }
        return false;
    };

    // Queue data, then step until the member's TX data slot has begun
    ASSERT_TRUE(SendMessage(member, manager, {0x01, 0x02, 0x03}));
    uint32_t timeout = GetSuperframeDuration(member) * 2;
    ASSERT_TRUE(AdvanceTime(timeout, timeout, 1, 0, [&]() {
        return is_own_tx_slot(member.protocol->GetCurrentSlot());
    })) << "Member never entered its TX data slot";

    auto* mock = dynamic_cast<os::RTOSMock*>(&GetRTOS());
    ASSERT_NE(mock, nullptr);
    uint64_t stop_started = mock->getVirtualTime();
    Result result = StopNode(member);
    uint64_t stop_elapsed = mock->getVirtualTime() - stop_started;

    ASSERT_TRUE(result) << result.GetErrorMessage();
    ExpectNoTaskMisuse();
    EXPECT_LE(stop_elapsed, GetSlotDuration(manager))
        << "Stop() should return once the current slot work is done";
}

/**
 * @brief After a restart the radio and superframe callbacks drive the
 * protocol again: slot transitions run and received packets are delivered
 */
TEST_F(LoRaMeshRestartTests, CallbacksRewiredAfterRestart) {
    auto sleep_count = std::make_shared<std::atomic<int>>(0);
    auto& manager = CreateManagerNode("Manager", 0x1001);
    auto& member = CreateNode(
        "Member", 0x1002, NodeRole::NODE_ONLY, PinConfig(), RadioConfig(),
        [sleep_count](LoRaMeshProtocolConfig& config) {
            config.setPrepareSleepCallback(
                [sleep_count](const power::SleepContext&) {
                    sleep_count->fetch_add(1, std::memory_order_relaxed);
                    return power::SleepResult{true};
                });
        });
    SetLinkStatus(manager, member, true);

    StartUntilManager(manager);
    StartUntilJoined(member, manager);

    StopAndExpectIdle(member);
    StartUntilJoined(member, manager);

    // Slot transitions reach the protocol task: sleep slots are entered
    int sleeps_before = sleep_count->load();
    uint32_t timeout = GetSuperframeDuration(member) * 3;
    EXPECT_TRUE(AdvanceTime(timeout, timeout, 15, 0, [&]() {
        return sleep_count->load() > sleeps_before;
    })) << "No sleep slot was processed after the restart";

    // Received packets reach the protocol task
    ExpectDelivery(manager, member);
    ExpectNoTaskMisuse();
}

/**
 * @brief Stop(), Pause() and Start() called from a data callback, which runs
 * on the protocol task, are rejected and the node keeps running
 */
TEST_F(LoRaMeshRestartTests, StopFromProtocolTaskIsRejected) {
    auto& manager = CreateManagerNode("Manager", 0x1001);
    auto& member = CreateJoiningNode("Member", 0x1002);
    SetLinkStatus(manager, member, true);

    StartUntilManager(manager);
    StartUntilJoined(member, manager);

    std::vector<Result> callback_results;
    auto* protocol = member.protocol.get();
    member.protocol->SetDataReceivedCallback(
        [&callback_results, protocol](AddressType,
                                      const std::vector<uint8_t>&) {
            callback_results.push_back(protocol->Stop());
            callback_results.push_back(protocol->Pause());
            callback_results.push_back(protocol->Start());
        });

    std::vector<uint8_t> payload = {0x5A};
    uint32_t ready_timeout = GetSuperframeDuration(manager) * 4 + 2000;
    ASSERT_TRUE(AdvanceTime(ready_timeout, ready_timeout, 15, 0, [&]() {
        return static_cast<bool>(
            manager.protocol->IsReadyToSend(member.address));
    }));
    ASSERT_TRUE(SendMessage(manager, member, payload));
    uint32_t timeout = GetSuperframeDuration(manager) * 4 + 2000;
    ASSERT_TRUE(AdvanceTime(timeout, timeout, 15, 0, [&]() {
        return !callback_results.empty();
    })) << "Member never delivered the data";

    ASSERT_EQ(callback_results.size(), 3u);
    for (const Result& result : callback_results) {
        EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState)
            << result.GetErrorMessage();
    }
    EXPECT_EQ(member.protocol->GetState(), ProtocolState::NORMAL_OPERATION);
    ExpectNoTaskMisuse();

    // The member still runs: it keeps exchanging data
    member.protocol->SetDataReceivedCallback(
        [node = &member](AddressType source, const std::vector<uint8_t>& data) {
            auto msg = BaseMessage::Create(node->address, source,
                                           MessageType::DATA, data);
            if (msg.has_value()) {
                node->received_messages.push_back(msg.value());
            }
        });
    ExpectDelivery(member, manager);
    ExpectDelivery(manager, member);
}

}  // namespace test
}  // namespace loramesher
