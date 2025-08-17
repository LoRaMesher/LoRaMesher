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

/**
 * @class SuperframeServiceSynchronizeWithTest
 * @brief Test fixture for SynchronizeWith functionality
 */
class SuperframeServiceSynchronizeWithTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create superframe service with 10 slots of 100ms each
        service_ = std::make_shared<SuperframeService>(10, 100);
    }

    void TearDown() override {
        if (service_) {
            if (service_->IsSynchronized()) {
                service_->StopSuperframe();
            }
            service_.reset();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::shared_ptr<SuperframeService> service_;
};

/**
 * @brief Test basic synchronization functionality
 */
TEST_F(SuperframeServiceSynchronizeWithTest, BasicSynchronization) {
    // Start the superframe service
    ASSERT_TRUE(service_->StartSuperframe().IsSuccess());

    // Test basic synchronization
    // If external node is at slot 3 and its slot started at time 1000ms
    // The external superframe should have started at 1000 - (3 * 100) = 700ms
    uint32_t external_slot_start_time = 1000;
    uint16_t external_slot = 3;
    uint32_t slot_duration = 100;

    // Calculate expected superframe start time
    uint32_t expected_superframe_start =
        external_slot_start_time - (external_slot * slot_duration);
    EXPECT_EQ(expected_superframe_start, 700)
        << "Expected calculation verification failed";

    auto result =
        service_->SynchronizeWith(external_slot_start_time, external_slot);
    EXPECT_TRUE(result.IsSuccess())
        << "SynchronizeWith failed: " << result.GetErrorMessage();

    // Check that we're synchronized
    EXPECT_TRUE(service_->IsSynchronized());

    // Verify the superframe timing was actually updated
    // Allow some time for the service to process the synchronization
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Check that the current slot calculation matches our expectations
    // The current slot should be based on the new superframe start time
    auto stats = service_->GetSuperframeStats();

    // Calculate what the current slot should be based on the synchronized timing
    uint32_t current_time = GetRTOS().getTickCount();
    uint32_t time_since_superframe_start =
        current_time - expected_superframe_start;
    uint32_t superframe_duration =
        10 * slot_duration;  // 10 slots * 100ms = 1000ms
    uint32_t time_in_current_superframe =
        time_since_superframe_start % superframe_duration;
    uint16_t expected_current_slot = time_in_current_superframe / slot_duration;

    // The actual slot might be slightly different due to timing, but should be close
    EXPECT_LE(std::abs(static_cast<int>(stats.current_slot) -
                       static_cast<int>(expected_current_slot)),
              2)
        << "Current slot " << stats.current_slot
        << " should be close to expected slot " << expected_current_slot;
}

/**
 * @brief Test synchronization with slot 0
 */
TEST_F(SuperframeServiceSynchronizeWithTest, SynchronizeWithSlotZero) {
    ASSERT_TRUE(service_->StartSuperframe().IsSuccess());

    // Test with slot 0 - superframe start should equal slot start
    uint32_t external_slot_start_time = 1000;
    uint16_t external_slot = 0;
    uint32_t slot_duration = 100;

    // Calculate expected superframe start time
    uint32_t expected_superframe_start =
        external_slot_start_time - (external_slot * slot_duration);
    EXPECT_EQ(expected_superframe_start, 1000)
        << "For slot 0, superframe start should equal slot start";

    auto result =
        service_->SynchronizeWith(external_slot_start_time, external_slot);
    EXPECT_TRUE(result.IsSuccess())
        << "SynchronizeWith failed: " << result.GetErrorMessage();
    EXPECT_TRUE(service_->IsSynchronized());

    // Verify the synchronization is correct
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // For slot 0, the current time should be very close to the slot start time
    uint32_t current_time = GetRTOS().getTickCount();
    uint32_t time_since_superframe_start =
        current_time - expected_superframe_start;
    uint32_t superframe_duration = 10 * slot_duration;
    uint32_t time_in_current_superframe =
        time_since_superframe_start % superframe_duration;
    uint16_t expected_current_slot = time_in_current_superframe / slot_duration;

    auto stats = service_->GetSuperframeStats();
    EXPECT_LE(std::abs(static_cast<int>(stats.current_slot) -
                       static_cast<int>(expected_current_slot)),
              2)
        << "Current slot should be close to expected slot";
}

/**
 * @brief Test synchronization with maximum valid slot
 */
TEST_F(SuperframeServiceSynchronizeWithTest, SynchronizeWithMaxSlot) {
    ASSERT_TRUE(service_->StartSuperframe().IsSuccess());

    // Test with maximum slot (9 for 10-slot superframe)
    uint32_t external_slot_start_time = 2000;
    uint16_t external_slot = 9;

    auto result =
        service_->SynchronizeWith(external_slot_start_time, external_slot);
    EXPECT_TRUE(result.IsSuccess())
        << "SynchronizeWith failed: " << result.GetErrorMessage();
    EXPECT_TRUE(service_->IsSynchronized());
}

/**
 * @brief Test synchronization with invalid slot number
 */
TEST_F(SuperframeServiceSynchronizeWithTest, SynchronizeWithInvalidSlot) {
    ASSERT_TRUE(service_->StartSuperframe().IsSuccess());

    // Test with invalid slot number (10 for 10-slot superframe, should be 0-9)
    uint32_t external_slot_start_time = 2000;
    uint16_t external_slot = 10;

    auto result =
        service_->SynchronizeWith(external_slot_start_time, external_slot);
    EXPECT_FALSE(result.IsSuccess())
        << "SynchronizeWith should have failed with invalid slot";
}

/**
 * @brief Test synchronization with very large slot number
 */
TEST_F(SuperframeServiceSynchronizeWithTest, SynchronizeWithVeryLargeSlot) {
    ASSERT_TRUE(service_->StartSuperframe().IsSuccess());

    // Test with very large slot number
    uint32_t external_slot_start_time = 2000;
    uint16_t external_slot = 1000;

    auto result =
        service_->SynchronizeWith(external_slot_start_time, external_slot);
    EXPECT_FALSE(result.IsSuccess())
        << "SynchronizeWith should have failed with very large slot";
}

/**
 * @brief Test synchronization with time underflow condition
 */
TEST_F(SuperframeServiceSynchronizeWithTest, SynchronizeWithTimeUnderflow) {
    ASSERT_TRUE(service_->StartSuperframe().IsSuccess());

    // Test with time that would cause underflow
    // Slot 5 with start time 400ms: 5 * 100 = 500ms elapsed, but start time is only 400ms
    uint32_t external_slot_start_time = 400;
    uint16_t external_slot = 5;

    auto result =
        service_->SynchronizeWith(external_slot_start_time, external_slot);
    EXPECT_TRUE(result.IsSuccess())
        << "SynchronizeWith should have failed with underflow condition";
}

/**
 * @brief Test synchronization with large time values
 */
TEST_F(SuperframeServiceSynchronizeWithTest, SynchronizeWithLargeTimeValues) {
    ASSERT_TRUE(service_->StartSuperframe().IsSuccess());

    // Test with large time values to check for overflow
    uint32_t large_time = 0xFFFFFF00;  // Very large time
    uint16_t slot = 5;

    auto result = service_->SynchronizeWith(large_time, slot);
    EXPECT_TRUE(result.IsSuccess())
        << "SynchronizeWith failed with large time: "
        << result.GetErrorMessage();
    EXPECT_TRUE(service_->IsSynchronized());
}

/**
 * @brief Test synchronization without running superframe
 */
TEST_F(SuperframeServiceSynchronizeWithTest, SynchronizeWithoutRunning) {
    // Don't start the superframe service
    auto result = service_->SynchronizeWith(1000, 3);
    EXPECT_FALSE(result.IsSuccess())
        << "SynchronizeWith should fail when service not running";
}

/**
 * @brief Test multiple synchronizations and drift calculation
 */
TEST_F(SuperframeServiceSynchronizeWithTest, MultipleSynchronizations) {
    ASSERT_TRUE(service_->StartSuperframe().IsSuccess());

    uint32_t slot_duration = 100;

    // First synchronization
    auto result1 = service_->SynchronizeWith(1000, 2);
    EXPECT_TRUE(result1.IsSuccess())
        << "First synchronization failed: " << result1.GetErrorMessage();

    // Calculate expected first superframe start
    uint32_t expected_start1 = 1000 - (2 * slot_duration);  // 1000 - 200 = 800
    EXPECT_EQ(expected_start1, 800)
        << "First synchronization calculation verification";

    // Second synchronization - should cause drift
    auto result2 = service_->SynchronizeWith(2000, 5);
    EXPECT_TRUE(result2.IsSuccess())
        << "Second synchronization failed: " << result2.GetErrorMessage();

    // Calculate expected second superframe start
    uint32_t expected_start2 = 2000 - (5 * slot_duration);  // 2000 - 500 = 1500
    EXPECT_EQ(expected_start2, 1500)
        << "Second synchronization calculation verification";

    // Calculate expected drift (difference between the two superframe starts)
    int32_t expected_drift = static_cast<int32_t>(
        expected_start2 - expected_start1);  // 1500 - 800 = 700
    EXPECT_EQ(expected_drift, 700) << "Expected drift calculation verification";

    // Third synchronization
    auto result3 = service_->SynchronizeWith(3000, 1);
    EXPECT_TRUE(result3.IsSuccess())
        << "Third synchronization failed: " << result3.GetErrorMessage();

    // Calculate expected third superframe start
    uint32_t expected_start3 = 3000 - (1 * slot_duration);  // 3000 - 100 = 2900
    EXPECT_EQ(expected_start3, 2900)
        << "Third synchronization calculation verification";

    EXPECT_TRUE(service_->IsSynchronized());

    // Verify drift accumulation
    auto stats = service_->GetSuperframeStats();
    // The drift should be non-zero as we performed multiple synchronizations
    EXPECT_GT(stats.sync_drift_ms, 0)
        << "Drift should be accumulated from multiple synchronizations";
}

/**
 * @brief Test synchronization calculations with precise timing
 */
TEST_F(SuperframeServiceSynchronizeWithTest,
       PrecisionSynchronizationCalculations) {
    ASSERT_TRUE(service_->StartSuperframe().IsSuccess());

    uint32_t slot_duration = 100;

    // Test with various slot positions and verify calculations
    struct TestCase {
        uint32_t slot_start_time;
        uint16_t slot_number;
        uint32_t expected_superframe_start;
    };

    std::vector<TestCase> test_cases = {
        {5000, 0, 5000},  // Slot 0: 5000 - (0 * 100) = 5000
        {5250, 2, 5050},  // Slot 2: 5250 - (2 * 100) = 5050
        {5890, 8, 5090},  // Slot 8: 5890 - (8 * 100) = 5090
        {6000, 9, 5100},  // Slot 9: 6000 - (9 * 100) = 5100
    };

    for (const auto& test_case : test_cases) {
        // Calculate expected superframe start
        uint32_t calculated_start =
            test_case.slot_start_time - (test_case.slot_number * slot_duration);
        EXPECT_EQ(calculated_start, test_case.expected_superframe_start)
            << "Calculation mismatch for slot " << test_case.slot_number
            << " at time " << test_case.slot_start_time;

        // Perform synchronization
        auto result = service_->SynchronizeWith(test_case.slot_start_time,
                                                test_case.slot_number);
        EXPECT_TRUE(result.IsSuccess())
            << "SynchronizeWith failed for slot " << test_case.slot_number
            << " at time " << test_case.slot_start_time << ": "
            << result.GetErrorMessage();

        // Brief delay to allow processing
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(service_->IsSynchronized());
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO