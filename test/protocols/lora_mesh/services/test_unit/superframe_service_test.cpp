/**
 * @file superframe_service_test.cpp
 * @brief Simple lifecycle tests for SuperframeService
 */

#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <thread>

#include "os/os_port.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"
#include "types/protocols/lora_mesh/slot_allocation.hpp"
#include "types/protocols/lora_mesh/superframe.hpp"

#ifdef ARDUINO

TEST(SuperframeServiceTest, ImplementArduinoTests) {
    GTEST_SKIP();
}

#else

namespace {
using namespace loramesher::types::protocols::lora_mesh;
}

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

/**
 * @class SuperframeServiceLifecycleTest
 * @brief Simple lifecycle test fixture for SuperframeService
 */
class SuperframeServiceLifecycleTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create simple default superframe configuration
        service_ = std::make_shared<SuperframeService>();
    }

    void TearDown() override {
        // Clean up resources
        if (service_) {
            if (service_->IsSynchronized()) {
                service_->StopSuperframe();
            }
            service_.reset();
        }

        // Give time for cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::shared_ptr<SuperframeService> service_;
};

/**
 * @brief Test basic superframe service creation and destruction
 */
TEST_F(SuperframeServiceLifecycleTest, CreateAndDestroy) {
    // Service should be created successfully
    EXPECT_NE(service_, nullptr);

    // Initially not synchronized
    EXPECT_FALSE(service_->IsSynchronized());

    // Reset explicitly (destructor will be called)
    service_.reset();
}

/**
 * @brief Test superframe start and stop
 */
TEST_F(SuperframeServiceLifecycleTest, StartAndStop) {
    // Initially not running
    EXPECT_FALSE(service_->IsSynchronized());

    // Start superframe
    Result result = service_->StartSuperframe();
    EXPECT_TRUE(result);

    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop superframe
    result = service_->StopSuperframe();
    EXPECT_TRUE(result);
    EXPECT_FALSE(service_->IsSynchronized());
}

/**
 * @brief Test multiple start/stop cycles
 */
TEST_F(SuperframeServiceLifecycleTest, MultipleStartStop) {
    for (int i = 0; i < 3; ++i) {
        // Start
        Result result = service_->StartSuperframe();
        EXPECT_TRUE(result)
            << "Start failed on iteration " << i << result.GetErrorMessage();

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Stop
        result = service_->StopSuperframe();
        EXPECT_TRUE(result)
            << "Stop failed on iteration " << i << result.GetErrorMessage();

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO