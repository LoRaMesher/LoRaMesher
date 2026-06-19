/**
 * @file network_service_state_test.cpp
 * @brief Coverage tests for NetworkService state machine transitions,
 *        NM election paths, and superframe start handling.
 *
 * Complements network_service_coverage_test.cpp with tests that require
 * a freshly-configured (but not pre-configured) service fixture so that
 * node role, address, and state can be set freely per test.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "protocols/lora_mesh/services/message_queue_service.hpp"
#include "protocols/lora_mesh/services/network_service.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"
#include "types/messages/loramesher/join_request_message.hpp"
#include "types/messages/loramesher/nm_claim_message.hpp"
#include "types/messages/loramesher/sync_beacon_message.hpp"

#ifdef ARDUINO
TEST(NetworkServiceStateCoverageTest, SkipOnArduino) {
    GTEST_SKIP();
}
#else

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

// ─── Fixture ────────────────────────────────────────────────────────────────

class NetworkServiceStateCoverageTest : public ::testing::Test {
   protected:
    void SetUp() override {
        message_queue_ = std::make_shared<MessageQueueService>(20);
        superframe_ = std::make_shared<SuperframeService>();
        service_ = std::make_unique<NetworkService>(0x1001, message_queue_,
                                                    superframe_, nullptr);
    }

    void TearDown() override {
        if (service_)
            service_.reset();
        if (superframe_)
            superframe_.reset();
        if (message_queue_)
            message_queue_.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void Configure(AddressType addr = 0x1001, NodeRole role = NodeRole::AUTO) {
        INetworkService::NetworkConfig config;
        config.node_address = addr;
        config.max_hops = 10;
        config.max_packet_size = 255;
        config.default_data_slots = 4;
        config.max_network_nodes = 50;
        config.node_role = role;
        ASSERT_TRUE(service_->Configure(config));
    }

    std::unique_ptr<NetworkService> service_;
    std::shared_ptr<MessageQueueService> message_queue_;
    std::shared_ptr<SuperframeService> superframe_;
};

// ─── CreateNetwork: success path ────────────────────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, CreateNetworkSuccessEntersNMState) {
    Configure();
    ASSERT_TRUE(superframe_->StartSuperframe());
    Result result = service_->CreateNetwork();
    EXPECT_TRUE(result) << result.GetErrorMessage();
    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::NETWORK_MANAGER);
    EXPECT_TRUE(service_->IsNetworkCreator());
    EXPECT_TRUE(service_->IsSynchronized());
    superframe_->StopSuperframe();
}

// ─── ProcessSyncBeacon: NM state, foreign beacon ────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, ProcessSyncBeaconNMStateForeignBeacon) {
    Configure();
    ASSERT_TRUE(superframe_->StartSuperframe());
    ASSERT_TRUE(service_->CreateNetwork());
    ASSERT_EQ(service_->GetState(),
              INetworkService::ProtocolState::NETWORK_MANAGER);

    auto beacon_opt = SyncBeaconMessage::CreateOriginal(
        0xFFFF, 0x9001, 0xDEAD /*different network_id*/, 16, 1000, 0x9001, 0, 1,
        1);
    ASSERT_TRUE(beacon_opt.has_value());
    BaseMessage base_msg = beacon_opt->ToBaseMessage();

    Result result = service_->ProcessSyncBeacon(base_msg, 1000);
    EXPECT_TRUE(result);
    superframe_->StopSuperframe();
}

TEST_F(NetworkServiceStateCoverageTest, ProcessSyncBeaconNMStateOwnNetwork) {
    Configure();
    ASSERT_TRUE(superframe_->StartSuperframe());
    ASSERT_TRUE(service_->CreateNetwork());

    auto beacon_opt = SyncBeaconMessage::CreateOriginal(
        0xFFFF, 0x9001, 0 /*network_id=0, skips foreign check*/, 16, 1000,
        0x9001, 0, 1, 1);
    ASSERT_TRUE(beacon_opt.has_value());
    BaseMessage base_msg = beacon_opt->ToBaseMessage();

    Result result = service_->ProcessSyncBeacon(base_msg, 1000);
    EXPECT_TRUE(result);
    superframe_->StopSuperframe();
}

// ─── ComputeElectionPriority: NODE_ONLY path ────────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, ComputeElectionPriorityNodeOnly) {
    Configure(0x1001, NodeRole::NODE_ONLY);
    service_->StartElectionBackoff();
    EXPECT_FALSE(service_->IsElectionPending());  // NODE_ONLY never elects
}

// ─── StartElectionBackoff: NETWORK_MANAGER role ─────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, StartElectionBackoffNMRole) {
    Configure(0x1001, NodeRole::NETWORK_MANAGER);
    service_->StartElectionBackoff();
    EXPECT_TRUE(service_->IsElectionPending());
}

// ─── ProcessNMClaim: NETWORK_MANAGER state, yield ───────────────────────────

TEST_F(NetworkServiceStateCoverageTest, ProcessNMClaimNMStateYields) {
    // Node 0x1004: addr_component = small → computed priority > 0
    auto svc2 = std::make_unique<NetworkService>(0x1004, message_queue_,
                                                 superframe_, nullptr);
    INetworkService::NetworkConfig config;
    config.node_address = 0x1004;
    config.max_hops = 5;
    config.max_packet_size = 255;
    config.default_data_slots = 2;
    config.max_network_nodes = 20;
    config.node_role = NodeRole::NETWORK_MANAGER;
    ASSERT_TRUE(svc2->Configure(config));

    ASSERT_TRUE(superframe_->StartSuperframe());
    ASSERT_TRUE(svc2->CreateNetwork());
    ASSERT_EQ(svc2->GetState(),
              INetworkService::ProtocolState::NETWORK_MANAGER);

    // Send claim with priority 0 (highest) — forces svc2 to yield
    auto claim_opt = NMClaimMessage::Create(0x0001, 0, 2, 0xBEEF);
    ASSERT_TRUE(claim_opt.has_value());
    BaseMessage base_msg = claim_opt->ToBaseMessage();

    Result result = svc2->ProcessNMClaim(base_msg);
    EXPECT_TRUE(result);
    EXPECT_EQ(svc2->GetState(), INetworkService::ProtocolState::DISCOVERY);

    superframe_->StopSuperframe();
    svc2.reset();
}

TEST_F(NetworkServiceStateCoverageTest, ProcessNMClaimNMStateWins) {
    Configure(0x1001, NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(superframe_->StartSuperframe());
    ASSERT_TRUE(service_->CreateNetwork());
    ASSERT_EQ(service_->GetState(),
              INetworkService::ProtocolState::NETWORK_MANAGER);

    // Claim with worst priority (0xFF) — we win and stay NM
    auto claim_opt = NMClaimMessage::Create(0x2001, 0xFF, 2, 0xBEEF);
    ASSERT_TRUE(claim_opt.has_value());
    BaseMessage base_msg = claim_opt->ToBaseMessage();

    Result result = service_->ProcessNMClaim(base_msg);
    EXPECT_TRUE(result);
    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::NETWORK_MANAGER);
    superframe_->StopSuperframe();
}

// ─── ProcessNMClaim: FAULT_RECOVERY state ───────────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, ProcessNMClaimFaultRecoveryYields) {
    Configure(0x1001, NodeRole::AUTO);
    service_->SetState(INetworkService::ProtocolState::FAULT_RECOVERY);
    service_->StartElectionBackoff();
    ASSERT_TRUE(service_->IsElectionPending());

    // Higher-priority claimant → we yield
    auto claim_opt = NMClaimMessage::Create(0x0001, 0, 3, 0xABCD);
    ASSERT_TRUE(claim_opt.has_value());
    BaseMessage base_msg = claim_opt->ToBaseMessage();

    Result result = service_->ProcessNMClaim(base_msg);
    EXPECT_TRUE(result);
    EXPECT_FALSE(service_->IsElectionPending());
}

TEST_F(NetworkServiceStateCoverageTest, ProcessNMClaimFaultRecoveryWins) {
    auto svc = std::make_unique<NetworkService>(0x0100, message_queue_,
                                                superframe_, nullptr);
    INetworkService::NetworkConfig config;
    config.node_address = 0x0100;
    config.max_hops = 5;
    config.max_packet_size = 255;
    config.default_data_slots = 2;
    config.max_network_nodes = 20;
    config.node_role = NodeRole::AUTO;
    ASSERT_TRUE(svc->Configure(config));
    svc->SetState(INetworkService::ProtocolState::FAULT_RECOVERY);
    svc->StartElectionBackoff();
    ASSERT_TRUE(svc->IsElectionPending());

    // Worse priority claimant → we win and keep election
    auto claim_opt = NMClaimMessage::Create(0x9999, 0xFF, 3, 0xABCD);
    ASSERT_TRUE(claim_opt.has_value());
    BaseMessage base_msg = claim_opt->ToBaseMessage();

    Result result = svc->ProcessNMClaim(base_msg);
    EXPECT_TRUE(result);
    EXPECT_TRUE(svc->IsElectionPending());
    svc.reset();
}

// ─── ProcessNMClaim: NM_ELECTION state yields ────────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, ProcessNMClaimNMElectionYields) {
    Configure(0x1001, NodeRole::AUTO);
    service_->SetState(INetworkService::ProtocolState::NM_ELECTION);
    service_->StartElectionBackoff();

    auto claim_opt = NMClaimMessage::Create(0x0001, 0, 5, 0x5678);
    ASSERT_TRUE(claim_opt.has_value());
    BaseMessage base_msg = claim_opt->ToBaseMessage();

    Result result = service_->ProcessNMClaim(base_msg);
    EXPECT_TRUE(result);
    EXPECT_FALSE(service_->IsElectionPending());
}

// ─── HandleSuperframeStart: NM state ────────────────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, HandleSuperframeStartNMState) {
    Configure();
    ASSERT_TRUE(superframe_->StartSuperframe());
    ASSERT_TRUE(service_->CreateNetwork());

    Result result = service_->HandleSuperframeStart();
    EXPECT_TRUE(result) << result.GetErrorMessage();
    superframe_->StopSuperframe();
}

// ─── HandleSuperframeStart: JOINING state ───────────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, HandleSuperframeStartJoiningState) {
    Configure();
    service_->SetNetworkManager(0x2001);
    service_->SetState(INetworkService::ProtocolState::JOINING);

    Result result = service_->HandleSuperframeStart();
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

// ─── HandleSuperframeStart: FAULT_RECOVERY ───────────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, HandleSuperframeStartFaultRecovery) {
    Configure();
    service_->SetState(INetworkService::ProtocolState::FAULT_RECOVERY);

    Result result = service_->HandleSuperframeStart();
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

// ─── HandleSuperframeStart: NORMAL_OPERATION → FAULT_RECOVERY ───────────────

TEST_F(NetworkServiceStateCoverageTest,
       HandleSuperframeStartNormalOpMissedBeacons) {
    Configure();
    service_->SetState(INetworkService::ProtocolState::NORMAL_OPERATION);

    // After kMaxNoReceivedSyncBeacons (5) calls → FAULT_RECOVERY
    for (int i = 0; i < 6; i++) {
        service_->HandleSuperframeStart();
    }
    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::FAULT_RECOVERY);
}

// ─── ResetNetworkState: resets to INITIALIZING ───────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, ResetNetworkStateResetsAll) {
    Configure();
    service_->SetState(INetworkService::ProtocolState::NORMAL_OPERATION);
    service_->ResetNetworkState();
    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::INITIALIZING);
    EXPECT_FALSE(service_->IsNetworkFound());
    EXPECT_FALSE(service_->IsNetworkCreator());
    EXPECT_FALSE(service_->IsSynchronized());
}

// ─── StartDiscovery: NETWORK_MANAGER role → CreateNetwork ───────────────────

TEST_F(NetworkServiceStateCoverageTest, StartDiscoveryNMRoleCreatesNetwork) {
    Configure(0x1001, NodeRole::NETWORK_MANAGER);
    ASSERT_TRUE(superframe_->StartSuperframe());
    Result result = service_->StartDiscovery(5000);
    EXPECT_TRUE(result) << result.GetErrorMessage();
    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::NETWORK_MANAGER);
    superframe_->StopSuperframe();
}

// ─── ProcessReceivedMessage: unknown message type returns error ──────────────

TEST_F(NetworkServiceStateCoverageTest,
       ProcessReceivedMessageUnknownTypeError) {
    uint8_t payload = 0;
    BaseMessage msg(0xFFFF, 0x2001, static_cast<MessageType>(0xFE),
                    std::span<const uint8_t>(&payload, 1));
    Result result = service_->ProcessReceivedMessage(msg, 0);
    EXPECT_FALSE(result);
}

// ─── SetJoiningSlots: exercises the full slot modification loop ──────────────

TEST_F(NetworkServiceStateCoverageTest, SetJoiningSlotsSucceeds) {
    Configure();
    service_->SetNetworkManager(0x2001);
    service_->SetState(INetworkService::ProtocolState::JOINING);

    Result result = service_->SetJoiningSlots();
    EXPECT_TRUE(result) << result.GetErrorMessage();
}

// ─── SetDiscoverySlots: covers slot allocation ───────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, SetDiscoverySlotsSucceeds) {
    Result result = service_->SetDiscoverySlots();
    EXPECT_TRUE(result);
    auto table = service_->GetSlotTable();
    EXPECT_GT(table.size(), 0u);
}

// ─── SendNMClaim: message is queued ─────────────────────────────────────────

TEST_F(NetworkServiceStateCoverageTest, SendNMClaimQueuesMessage) {
    Result result = service_->SendNMClaim();
    EXPECT_TRUE(result) << result.GetErrorMessage();
    EXPECT_TRUE(message_queue_->HasMessage(MessageType::NM_CLAIM));
}

// ─── SendBroadcast: state gate ──────────────────────────────────────────────

TEST_F(NetworkServiceStateCoverageTest,
       SendBroadcastRejectedOutsideNormalOperation) {
    Configure();

    const std::vector<INetworkService::ProtocolState> blocked_states = {
        INetworkService::ProtocolState::INITIALIZING,
        INetworkService::ProtocolState::DISCOVERY,
        INetworkService::ProtocolState::JOINING,
        INetworkService::ProtocolState::FAULT_RECOVERY,
        INetworkService::ProtocolState::NM_ELECTION,
    };

    std::vector<uint8_t> payload = {0xAB, 0xCD};
    for (auto state : blocked_states) {
        service_->SetState(state);
        Result result = service_->SendBroadcast(payload);
        EXPECT_FALSE(result)
            << "SendBroadcast should fail in state " << static_cast<int>(state);
        EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidState)
            << "Expected kInvalidState in state " << static_cast<int>(state);
    }
}

TEST_F(NetworkServiceStateCoverageTest,
       SendBroadcastSucceedsInNormalOperation) {
    Configure();
    service_->SetState(INetworkService::ProtocolState::NORMAL_OPERATION);

    std::vector<uint8_t> payload = {0xAB, 0xCD};
    Result result = service_->SendBroadcast(payload);
    EXPECT_TRUE(result) << result.GetErrorMessage();
    EXPECT_TRUE(message_queue_->HasMessage(MessageType::DATA_BROADCAST));
}

TEST_F(NetworkServiceStateCoverageTest, SendBroadcastSucceedsInNetworkManager) {
    Configure();
    service_->SetState(INetworkService::ProtocolState::NETWORK_MANAGER);

    std::vector<uint8_t> payload = {0xAB, 0xCD};
    Result result = service_->SendBroadcast(payload);
    EXPECT_TRUE(result) << result.GetErrorMessage();
    EXPECT_TRUE(message_queue_->HasMessage(MessageType::DATA_BROADCAST));
}

TEST_F(NetworkServiceStateCoverageTest,
       ApplyRoleChangeInvalidRoleReturnsError) {
    Configure();
    Result r = service_->ApplyRoleChange(static_cast<NodeRole>(99));
    EXPECT_FALSE(r);
    EXPECT_EQ(r.getErrorCode(), LoraMesherErrorCode::kInvalidParameter);
}

TEST_F(NetworkServiceStateCoverageTest, ApplyRoleChangeSameRoleNoOp) {
    Configure(0x1001, NodeRole::AUTO);
    Result r = service_->ApplyRoleChange(NodeRole::AUTO);
    EXPECT_TRUE(r);
}

TEST_F(NetworkServiceStateCoverageTest,
       ApplyRoleChangePromoteToNMFromJoiningDefersChange) {
    Configure(0x1001, NodeRole::AUTO);
    service_->SetState(INetworkService::ProtocolState::JOINING);
    Result r = service_->ApplyRoleChange(NodeRole::NETWORK_MANAGER);
    EXPECT_TRUE(r) << r.GetErrorMessage();
    EXPECT_EQ(service_->GetNodeRole(), NodeRole::NETWORK_MANAGER);
}

TEST_F(NetworkServiceStateCoverageTest, ApplyRoleChangeAutoToNodeOnly) {
    Configure(0x1001, NodeRole::AUTO);
    service_->SetState(INetworkService::ProtocolState::DISCOVERY);
    Result r = service_->ApplyRoleChange(NodeRole::NODE_ONLY);
    EXPECT_TRUE(r) << r.GetErrorMessage();
    EXPECT_EQ(service_->GetNodeRole(), NodeRole::NODE_ONLY);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO
