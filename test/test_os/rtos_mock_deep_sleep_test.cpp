/**
 * @file rtos_mock_deep_sleep_test.cpp
 * @brief Tests of the mock RTOS deep sleep and per-node clocks
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "os/rtos_mock.hpp"

#ifdef ARDUINO

TEST(RTOSMockDeepSleepTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

namespace loramesher {
namespace test {

class RTOSMockDeepSleepTest : public ::testing::Test {
   protected:
    void SetUp() override {
        mock_ = dynamic_cast<os::RTOSMock*>(&GetRTOS());
        ASSERT_NE(mock_, nullptr);
        mock_->setTimeMode(os::RTOSMock::TimeMode::kVirtualTime);
    }

    void TearDown() override {
        if (task_ != nullptr) {
            GetRTOS().DeleteTask(task_);
        }
        GetRTOS().SetCurrentTaskNodeAddress("0xFFFF");
        mock_->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
    }

    os::RTOSMock* mock_ = nullptr;
    os::TaskHandle_t task_ = nullptr;
};

TEST_F(RTOSMockDeepSleepTest, DeepSleepRecordsRequestAndParksTask) {
    struct State {
        std::atomic<bool> started{false};
        std::atomic<bool> returned{false};
    } state;

    ASSERT_TRUE(GetRTOS().CreateTask(
        [](void* param) {
            auto* s = static_cast<State*>(param);
            GetRTOS().SetCurrentTaskNodeAddress("0x1001");
            s->started = true;
            GetRTOS().DeepSleep(5000);
            s->returned = true;
        },
        "Sleeper", 2048, &state, 1, &task_));
    for (int i = 0; i < 200 && !state.started; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    mock_->waitForTasksToReblock(1000);
    const uint64_t asked_at = mock_->getVirtualTime();

    auto requests = mock_->TakeDeepSleepRequests();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].node_address, "0x1001");
    EXPECT_EQ(requests[0].wake_at_ms, asked_at + 5000);
    EXPECT_TRUE(mock_->TakeDeepSleepRequests().empty());

    // The task stays parked however long time runs
    mock_->advanceTime(200000);
    EXPECT_FALSE(state.returned);

    // Powering the node off ends the park
    GetRTOS().DeleteTask(task_);
    task_ = nullptr;
    EXPECT_FALSE(state.returned);
}

TEST_F(RTOSMockDeepSleepTest, TickCountContinuesPerNode) {
    const uint32_t shared = GetRTOS().getTickCount();

    GetRTOS().SetCurrentTaskNodeAddress("0x1001");
    GetRTOS().ContinueTickCountFrom(500);
    EXPECT_EQ(GetRTOS().getTickCount(), 500u);
    mock_->advanceTime(40);
    EXPECT_EQ(GetRTOS().getTickCount(), 540u);

    // Other nodes keep the shared clock
    GetRTOS().SetCurrentTaskNodeAddress("0x1002");
    EXPECT_EQ(GetRTOS().getTickCount(), shared + 40);

    // Powering off forgets the offset
    mock_->ResetNodeTickCount("0x1001");
    GetRTOS().SetCurrentTaskNodeAddress("0x1001");
    EXPECT_EQ(GetRTOS().getTickCount(), shared + 40);
}

TEST_F(RTOSMockDeepSleepTest, PersistentClockFollowsVirtualTimeWithOffset) {
    GetRTOS().SetCurrentTaskNodeAddress("0x1001");
    const uint64_t start = GetRTOS().GetPersistentTimeUs();
    mock_->advanceTime(250);
    EXPECT_EQ(GetRTOS().GetPersistentTimeUs() - start, 250000u);

    mock_->SetPersistentClockOffset("0x1001", -3000);
    EXPECT_EQ(GetRTOS().GetPersistentTimeUs() - start, 247000u);

    GetRTOS().SetCurrentTaskNodeAddress("0x1002");
    EXPECT_EQ(GetRTOS().GetPersistentTimeUs() - start, 250000u);
}

TEST_F(RTOSMockDeepSleepTest, NewVirtualTimeSessionForgetsNodeClocks) {
    GetRTOS().SetCurrentTaskNodeAddress("0x1001");
    GetRTOS().ContinueTickCountFrom(7);
    mock_->SetPersistentClockOffset("0x1001", 1000);

    mock_->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
    mock_->setTimeMode(os::RTOSMock::TimeMode::kVirtualTime);

    EXPECT_EQ(GetRTOS().getTickCount(), mock_->getVirtualTime());
    EXPECT_EQ(GetRTOS().GetPersistentTimeUs(),
              mock_->getVirtualTime() * 1000ULL);
}

}  // namespace test
}  // namespace loramesher

#endif  // ARDUINO
