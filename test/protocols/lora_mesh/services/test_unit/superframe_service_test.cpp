// /**
//  * @file superframe_service_test.cpp
//  * @brief Tests for SuperframeService using RTOSMock virtual time
//  */

// #include <gtest/gtest.h>
// #include <atomic>
// #include <memory>
// #include <vector>

// #include "os/rtos_mock.hpp"
// #include "protocols/lora_mesh/services/superframe_service.hpp"
// #include "types/protocols/lora_mesh/slot_allocation.hpp"
// #include "types/protocols/lora_mesh/superframe.hpp"

// #ifdef ARDUINO

// TEST(SuperframeServiceTest, ImplementArduinoTests) {
//     GTEST_SKIP();
// }

// #else

// namespace {
// using namespace loramesher::types::protocols::lora_mesh;
// }

// namespace loramesher {
// namespace protocols {
// namespace lora_mesh {
// namespace test {

// /**
//  * @class SuperframeServiceTest
//  * @brief Test fixture for SuperframeService
//  */
// class SuperframeServiceTest : public ::testing::Test {
//    protected:
//     void SetUp() override {
//         // Get RTOS instance and cast to RTOSMock
//         rtos_ = &GetRTOS();
//         rtosMock_ = dynamic_cast<os::RTOSMock*>(rtos_);
//         ASSERT_NE(rtosMock_, nullptr) << "RTOS is not an RTOSMock instance";

//         // Set virtual time mode for testing
//         rtosMock_->setTimeMode(os::RTOSMock::TimeMode::kVirtualTime);

//         // Store initial time
//         initialTime_ = rtos_->getTickCount();

//         // Create test superframe (10 slots, 6 data, 2 discovery, 2 control, 1000ms each)
//         test_superframe_ = Superframe(10, 6, 2, 2, 1000, initialTime_);

//         // Create service instance
//         service_ = std::make_unique<SuperframeService>(test_superframe_);

//         // Disable task-based updates for deterministic testing
//         service_->SetTaskEnabled(false);

//         // Set up callback tracking
//         callback_calls_.clear();
//         service_->SetSuperframeCallback(
//             [this](uint16_t slot, bool new_superframe) {
//                 callback_calls_.push_back({slot, new_superframe});
//             });
//     }

//     void TearDown() override {
//         // Clean up resources
//         service_.reset();

//         // Return to real-time mode after tests
//         rtosMock_->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
//     }

//     /**
//      * @brief Helper method to advance virtual time and update service state
//      *
//      * @param ms_to_advance Time to advance in milliseconds
//      */
//     void AdvanceTimeAndUpdate(uint32_t ms_to_advance) {
//         rtosMock_->advanceTime(ms_to_advance);
//         service_->UpdateSuperframeState();
//     }

//     /**
//      * @brief Helper to create test slot allocation table
//      *
//      * @return Test slot allocation table
//      */
//     std::vector<SlotAllocation> CreateTestSlotTable() {
//         std::vector<SlotAllocation> slot_table;

//         // Slots 0-5: Data slots (TX/RX alternating)
//         for (uint16_t i = 0; i < 6; ++i) {
//             SlotAllocation::SlotType type = (i % 2 == 0)
//                                                 ? SlotAllocation::SlotType::TX
//                                                 : SlotAllocation::SlotType::RX;
//             slot_table.emplace_back(i, type);
//         }

//         // Slots 6-7: Discovery slots
//         slot_table.emplace_back(6, SlotAllocation::SlotType::DISCOVERY_TX);
//         slot_table.emplace_back(7, SlotAllocation::SlotType::DISCOVERY_RX);

//         // Slots 8-9: Control slots
//         slot_table.emplace_back(8, SlotAllocation::SlotType::CONTROL_TX);
//         slot_table.emplace_back(9, SlotAllocation::SlotType::CONTROL_RX);

//         return slot_table;
//     }

//     /**
//      * @brief Structure to track callback invocations
//      */
//     struct CallbackEvent {
//         uint16_t slot;
//         bool new_superframe;
//     };

//     // Test fixture members
//     os::RTOS* rtos_ = nullptr;
//     os::RTOSMock* rtosMock_ = nullptr;
//     uint32_t initialTime_ = 0;

//     std::unique_ptr<SuperframeService> service_;
//     Superframe test_superframe_;

//     std::vector<CallbackEvent> callback_calls_;
// };

// /**
//  * @brief Test basic superframe start and stop
//  */
// TEST_F(SuperframeServiceTest, StartStopSuperframe) {
//     // Initially not running
//     EXPECT_FALSE(service_->IsSynchronized());

//     // Start superframe
//     Result result = service_->StartSuperframe();
//     EXPECT_TRUE(result.IsSuccess());
//     EXPECT_TRUE(service_->IsSynchronized());

//     // Check initial state
//     EXPECT_EQ(service_->GetCurrentSlot(), 0);
//     EXPECT_EQ(service_->GetTimeInSlot(), 0);

//     // Try to start again (should fail)
//     result = service_->StartSuperframe();
//     EXPECT_FALSE(result.IsSuccess());

//     // Stop superframe
//     result = service_->StopSuperframe();
//     EXPECT_TRUE(result.IsSuccess());
//     EXPECT_FALSE(service_->IsSynchronized());

//     // Try to stop again (should fail)
//     result = service_->StopSuperframe();
//     EXPECT_FALSE(result.IsSuccess());
// }

// /**
//  * @brief Test slot progression within a superframe
//  */
// TEST_F(SuperframeServiceTest, SlotProgression) {
//     service_->StartSuperframe();

//     // Test progression through slots
//     for (uint16_t expected_slot = 0; expected_slot < 10; ++expected_slot) {
//         EXPECT_EQ(service_->GetCurrentSlot(), expected_slot);

//         // Time remaining should decrease as we advance within the slot
//         AdvanceTimeAndUpdate(500);  // Half way through slot
//         EXPECT_EQ(service_->GetTimeRemainingInSlot(), 500);
//         EXPECT_EQ(service_->GetTimeInSlot(), 500);

//         // Move to next slot
//         AdvanceTimeAndUpdate(500);  // Complete the slot

//         // Reset callback tracking after each slot
//         callback_calls_.clear();
//     }

//     // After 10 slots, should wrap to slot 0 (new superframe)
//     EXPECT_EQ(service_->GetCurrentSlot(), 0);
// }

// /**
//  * @brief Test superframe transitions with callbacks
//  */
// TEST_F(SuperframeServiceTest, SuperframeTransitions) {
//     service_->StartSuperframe();

//     // Clear any initial callbacks
//     callback_calls_.clear();

//     // Move through entire superframe (10 * 1000ms = 10000ms)
//     // We'll do this in steps to ensure callbacks are triggered
//     for (int slot = 0; slot < 10; ++slot) {
//         AdvanceTimeAndUpdate(1000);
//     }

//     // Should be back at slot 0
//     EXPECT_EQ(service_->GetCurrentSlot(), 0);

//     // Check that callbacks were called for each slot transition
//     // and also for new superframe
//     bool found_new_superframe = false;
//     bool found_slot_transitions = false;

//     for (const auto& call : callback_calls_) {
//         if (call.new_superframe) {
//             found_new_superframe = true;
//             EXPECT_EQ(call.slot, 0);  // New superframe starts at slot 0
//         } else {
//             found_slot_transitions = true;
//         }
//     }

//     EXPECT_TRUE(found_new_superframe)
//         << "New superframe callback not triggered";
//     EXPECT_TRUE(found_slot_transitions)
//         << "Slot transition callbacks not triggered";

//     // Verify we have appropriate number of callbacks
//     // We should have at least 10 slots transitions (but some might be combined with new_superframe)
//     EXPECT_GE(callback_calls_.size(), 10);
// }

// /**
//  * @brief Test slot type determination
//  */
// TEST_F(SuperframeServiceTest, SlotTypeDetection) {
//     auto slot_table = CreateTestSlotTable();
//     service_->StartSuperframe();

//     // Test different slot types
//     struct TestCase {
//         uint32_t time_advance;
//         uint16_t expected_slot;
//         SlotAllocation::SlotType expected_type;
//     };

//     std::vector<TestCase> test_cases = {
//         {0, 0, SlotAllocation::SlotType::TX},     // Slot 0: TX
//         {1000, 1, SlotAllocation::SlotType::RX},  // Slot 1: RX
//         {1000, 2, SlotAllocation::SlotType::TX},  // Slot 2: TX
//         {4000, 6,
//          SlotAllocation::SlotType::DISCOVERY_TX},  // Slot 6: Discovery TX
//         {1000, 7,
//          SlotAllocation::SlotType::DISCOVERY_RX},  // Slot 7: Discovery RX
//         {1000, 8, SlotAllocation::SlotType::CONTROL_TX},  // Slot 8: Control TX
//         {1000, 9, SlotAllocation::SlotType::CONTROL_RX},  // Slot 9: Control RX
//     };

//     for (const auto& test_case : test_cases) {
//         AdvanceTimeAndUpdate(test_case.time_advance);

//         EXPECT_EQ(service_->GetCurrentSlot(), test_case.expected_slot);
//         EXPECT_EQ(service_->GetCurrentSlotType(slot_table),
//                   test_case.expected_type);
//         EXPECT_TRUE(
//             service_->IsInSlotType(test_case.expected_type, slot_table));
//     }
// }

// /**
//  * @brief Test superframe configuration updates
//  */
// TEST_F(SuperframeServiceTest, SuperframeConfigUpdate) {
//     // Test updating configuration before starting
//     Result result = service_->UpdateSuperframeConfig(20, 12, 4, 4, 500);
//     EXPECT_TRUE(result.IsSuccess());

//     const auto& config = service_->GetSuperframeConfig();
//     EXPECT_EQ(config.total_slots, 20);
//     EXPECT_EQ(config.data_slots, 12);
//     EXPECT_EQ(config.discovery_slots, 4);
//     EXPECT_EQ(config.control_slots, 4);
//     EXPECT_EQ(config.slot_duration_ms, 500);

//     // Test updating while running
//     service_->StartSuperframe();
//     result = service_->UpdateSuperframeConfig(15, 9, 3, 3, 750);
//     EXPECT_TRUE(result.IsSuccess());

//     // Test invalid configuration
//     result = service_->UpdateSuperframeConfig(10, 15, 5, 5,
//                                               1000);  // Sum exceeds total
//     EXPECT_FALSE(result.IsSuccess());
// }

// /**
//  * @brief Test synchronization with external timing
//  */
// TEST_F(SuperframeServiceTest, ExternalSynchronization) {
//     service_->StartSuperframe();

//     // Advance to slot 5
//     AdvanceTimeAndUpdate(5000);
//     EXPECT_EQ(service_->GetCurrentSlot(), 5);

//     // Synchronize with external superframe that's at slot 3
//     // This should adjust our timing
//     uint32_t current_time = rtos_->getTickCount();
//     Result result = service_->SynchronizeWith(current_time, 3);
//     EXPECT_TRUE(result.IsSuccess());

//     // We should now be at slot 3
//     EXPECT_EQ(service_->GetCurrentSlot(), 3);
//     EXPECT_TRUE(service_->IsSynchronized());
// }

// /**
//  * @brief Test manual superframe advancement
//  */
// TEST_F(SuperframeServiceTest, ManualSuperframeAdvancement) {
//     service_->StartSuperframe();

//     // Advance to some slot within the superframe
//     AdvanceTimeAndUpdate(3500);  // Middle of slot 3
//     EXPECT_EQ(service_->GetCurrentSlot(), 3);
//     callback_calls_.clear();

//     // Manually advance to next superframe
//     Result result = service_->AdvanceToNextSuperframe();
//     EXPECT_TRUE(result.IsSuccess());

//     // Should be at slot 0 of new superframe
//     EXPECT_EQ(service_->GetCurrentSlot(), 0);

//     // Check callback was called
//     EXPECT_FALSE(callback_calls_.empty());
//     bool found_new_superframe = false;
//     for (const auto& call : callback_calls_) {
//         if (call.new_superframe) {
//             found_new_superframe = true;
//             break;
//         }
//     }
//     EXPECT_TRUE(found_new_superframe);
// }

// /**
//  * @brief Test superframe statistics
//  */
// TEST_F(SuperframeServiceTest, SuperframeStatistics) {
//     service_->StartSuperframe();

//     // Complete one full superframe
//     AdvanceTimeAndUpdate(10000);

//     // Complete another partial superframe
//     AdvanceTimeAndUpdate(5000);

//     auto stats = service_->GetSuperframeStats();
//     EXPECT_GE(stats.superframes_completed, 1);
//     EXPECT_EQ(stats.current_slot, 5);
//     EXPECT_EQ(stats.time_in_current_slot_ms,
//               0);  // Should be 0 right after slot transition
//     EXPECT_EQ(stats.total_runtime_ms, 15000);
// }

// /**
//  * @brief Test task management
//  */
// TEST_F(SuperframeServiceTest, TaskManagement) {
//     // Initially task should be disabled (we disabled in setup)
//     EXPECT_FALSE(service_->IsTaskEnabled());

//     // Enable the task
//     service_->SetTaskEnabled(true);
//     EXPECT_TRUE(service_->IsTaskEnabled());

//     // Start the superframe
//     service_->StartSuperframe();

//     // Advance time (task should update slots automatically)
//     rtosMock_->advanceTime(3000);

//     // Since we're using virtual time, we need to explicitly run the task update
//     // function since it won't run on its own in the test environment
//     // In real operation, this would happen automatically in the RTOS task

//     // Simulate task running
//     for (int i = 0; i < 3; i++) {
//         service_->UpdateSuperframeState();
//     }

//     // Verify slot advanced
//     EXPECT_EQ(service_->GetCurrentSlot(), 3);

//     // Disable task
//     service_->SetTaskEnabled(false);
//     EXPECT_FALSE(service_->IsTaskEnabled());
// }

// /**
//  * @brief Test update interval configuration
//  */
// TEST_F(SuperframeServiceTest, UpdateIntervalConfig) {
//     // Check default interval
//     EXPECT_EQ(service_->GetUpdateInterval(), 20);  // Default is 20ms

//     // Set to minimum allowed value
//     service_->SetUpdateInterval(5);
//     EXPECT_EQ(service_->GetUpdateInterval(),
//               10);  // Should be adjusted to minimum 10ms

//     // Set to maximum allowed value
//     service_->SetUpdateInterval(2000);
//     EXPECT_EQ(service_->GetUpdateInterval(),
//               1000);  // Should be adjusted to maximum 1000ms

//     // Set to valid intermediate value
//     service_->SetUpdateInterval(100);
//     EXPECT_EQ(service_->GetUpdateInterval(), 100);
// }

// /**
//  * @brief Test automatic superframe detection
//  */
// TEST_F(SuperframeServiceTest, AutomaticSuperframeDetection) {
//     service_->StartSuperframe();
//     callback_calls_.clear();

//     // Advance directly to the end of the superframe
//     AdvanceTimeAndUpdate(9999);  // Just before end of last slot

//     // No new superframe yet
//     bool found_new_superframe = false;
//     for (const auto& call : callback_calls_) {
//         if (call.new_superframe) {
//             found_new_superframe = true;
//             break;
//         }
//     }
//     EXPECT_FALSE(found_new_superframe);

//     // Advance past the end of the superframe
//     callback_calls_.clear();
//     AdvanceTimeAndUpdate(2);  // Just past the end

//     // Should detect new superframe
//     found_new_superframe = false;
//     for (const auto& call : callback_calls_) {
//         if (call.new_superframe) {
//             found_new_superframe = true;
//             break;
//         }
//     }
//     EXPECT_TRUE(found_new_superframe);
// }

// /**
//  * @brief Test timing accuracy with statistics
//  */
// TEST_F(SuperframeServiceTest, TimingAccuracy) {
//     service_->StartSuperframe();

//     // Complete several superframes
//     for (int i = 0; i < 3; i++) {
//         AdvanceTimeAndUpdate(10000);  // Complete superframe
//     }

//     auto stats = service_->GetSuperframeStats();

//     // Check basic statistics
//     EXPECT_EQ(stats.superframes_completed, 3);
//     EXPECT_EQ(stats.current_slot,
//               0);  // Should be at slot 0 after 3 complete superframes

//     // Timing accuracy should be good with virtual time
//     EXPECT_LE(stats.avg_slot_accuracy_ms, 10);  // Should be very accurate
//     EXPECT_EQ(stats.sync_drift_ms, 0);  // No drift with perfect virtual time
// }

// /**
//  * @brief Test auto-advance enable/disable
//  */
// TEST_F(SuperframeServiceTest, AutoAdvanceControl) {
//     service_->StartSuperframe();

//     // Auto-advance should be enabled by default
//     EXPECT_TRUE(service_->IsAutoAdvanceEnabled());

//     // Disable auto-advance
//     service_->SetAutoAdvance(false);
//     EXPECT_FALSE(service_->IsAutoAdvanceEnabled());

//     // Advance past superframe boundary
//     callback_calls_.clear();
//     AdvanceTimeAndUpdate(10001);  // Just past end of superframe

//     // Should not have auto-advanced to new superframe
//     bool found_new_superframe = false;
//     for (const auto& call : callback_calls_) {
//         if (call.new_superframe) {
//             found_new_superframe = true;
//             break;
//         }
//     }
//     EXPECT_FALSE(found_new_superframe);

//     // Current slot should reflect time but superframe not advanced
//     // If the superframe was not advanced, the current slot should be the last slot
//     EXPECT_EQ(service_->GetCurrentSlot(), 9);

//     // Re-enable auto-advance
//     service_->SetAutoAdvance(true);
//     EXPECT_TRUE(service_->IsAutoAdvanceEnabled());

//     // Update should now detect and handle the new superframe
//     callback_calls_.clear();
//     service_->UpdateSuperframeState();

//     // Should now have detected new superframe
//     found_new_superframe = false;
//     for (const auto& call : callback_calls_) {
//         if (call.new_superframe) {
//             found_new_superframe = true;
//             break;
//         }
//     }
//     EXPECT_TRUE(found_new_superframe);

//     // Slot should now be in the normal range
//     EXPECT_EQ(service_->GetCurrentSlot(), 0);
// }

// }  // namespace test
// }  // namespace lora_mesh
// }  // namespace protocols
// }  // namespace loramesher

// #endif  // ARDUINO
