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

/**
 * @brief Test rapid creation and destruction to reproduce race condition
 * 
 * This test rapidly creates and destroys SuperframeService instances
 * to try to reproduce the race condition that causes SIGABRT.
 */
TEST(SuperframeServiceRaceConditionTest, RapidCreateDestroy) {
    const int iterations = 50;

    for (int i = 0; i < iterations; ++i) {
        LOG_DEBUG("=== Race condition test iteration %d ===", i);

        // Create service
        auto service = std::make_shared<SuperframeService>();

        // Start superframe
        Result result = service->StartSuperframe();
        EXPECT_TRUE(result) << "Start failed on iteration " << i << ": "
                            << result.GetErrorMessage();

        // Let it run for a very short time
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Stop superframe
        result = service->StopSuperframe();
        EXPECT_TRUE(result) << "Stop failed on iteration " << i << ": "
                            << result.GetErrorMessage();

        // Immediately destroy service (this should trigger the race condition)
        service.reset();

        // Brief pause to let any cleanup complete
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

/**
 * @brief Test concurrent access to SuperframeService
 * 
 * This test creates multiple threads that simultaneously start/stop
 * and destroy SuperframeService instances.
 */
TEST(SuperframeServiceRaceConditionTest, ConcurrentAccess) {
    const int num_threads = 5;
    const int iterations_per_thread = 10;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, iterations_per_thread]() {
            for (int i = 0; i < iterations_per_thread; ++i) {
                LOG_DEBUG("=== Thread %d, iteration %d ===", t, i);

                auto service = std::make_shared<SuperframeService>();

                Result result = service->StartSuperframe();
                EXPECT_TRUE(result)
                    << "Thread " << t << " start failed on iteration " << i;

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(5 + (t * 2)));

                result = service->StopSuperframe();
                EXPECT_TRUE(result)
                    << "Thread " << t << " stop failed on iteration " << i;

                // Destroy immediately
                service.reset();

                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO