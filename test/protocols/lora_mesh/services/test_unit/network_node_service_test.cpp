/**
 * @file network_service_test.cpp
 * @brief Simple lifecycle tests for NetworkService
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>

#include "protocols/lora_mesh/services/message_queue_service.hpp"
#include "protocols/lora_mesh/services/network_service.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

/**
 * @class NetworkServiceLifecycleTest
 * @brief Simple lifecycle test fixture for NetworkService
 */
class NetworkServiceLifecycleTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create dependencies
        message_queue_service_ = std::make_shared<MessageQueueService>(10);
        superframe_service_ = std::make_shared<SuperframeService>();

        // Create network service
        service_ = std::make_unique<NetworkService>(
            0x1001, message_queue_service_, superframe_service_, nullptr);
    }

    void TearDown() override {
        // Clean up resources
        if (service_) {
            service_.reset();
        }
        superframe_service_.reset();
        message_queue_service_.reset();

        // Give time for cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::unique_ptr<NetworkService> service_;
    std::shared_ptr<MessageQueueService> message_queue_service_;
    std::shared_ptr<SuperframeService> superframe_service_;
};

/**
 * @brief Test basic network service creation and destruction
 */
TEST_F(NetworkServiceLifecycleTest, CreateAndDestroy) {
    // Service should be created successfully
    EXPECT_NE(service_, nullptr);

    // Should be in initialization state initially
    EXPECT_EQ(service_->GetState(),
              INetworkService::ProtocolState::INITIALIZING);

    // Reset explicitly (destructor will be called)
    service_.reset();
}

/**
 * @brief Test network service configuration
 */
TEST_F(NetworkServiceLifecycleTest, Configure) {
    // Create configuration
    INetworkService::NetworkConfig config;
    config.node_address = 0x1001;
    config.hello_interval_ms = 1000;
    config.route_timeout_ms = 3000;
    config.node_timeout_ms = 5000;
    config.max_hops = 10;
    config.max_packet_size = 255;
    config.default_data_slots = 4;
    config.max_network_nodes = 50;

    // Configure service
    Result result = service_->Configure(config);
    EXPECT_TRUE(result) << "Configuration failed: " << result.GetErrorMessage();
}

/**
 * @brief Test discovery start and stop
 */
TEST_F(NetworkServiceLifecycleTest, DiscoveryLifecycle) {
    // Configure first
    INetworkService::NetworkConfig config;
    config.node_address = 0x1001;
    config.hello_interval_ms = 1000;
    config.route_timeout_ms = 3000;
    config.node_timeout_ms = 5000;
    config.max_hops = 10;
    config.max_packet_size = 255;
    config.default_data_slots = 4;
    config.max_network_nodes = 50;

    Result result = service_->Configure(config);
    ASSERT_TRUE(result);

    // Start discovery
    result = service_->StartDiscovery(5000);  // 5 second timeout
    EXPECT_TRUE(result) << "Start discovery failed: "
                        << result.GetErrorMessage();

    // Should be in discovery state
    EXPECT_EQ(service_->GetState(), INetworkService::ProtocolState::DISCOVERY);

    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

/**
 * @brief Test getting network nodes
 */
TEST_F(NetworkServiceLifecycleTest, GetNetworkNodes) {
    // Initially should have empty network nodes
    const auto& nodes = service_->GetNetworkNodes();
    EXPECT_TRUE(nodes.empty());
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher