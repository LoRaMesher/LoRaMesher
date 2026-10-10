/**
 * @file superframe_wake_test.cpp
 * @brief Tests of the superframe schedule after the MCU wakes from sleep
 */

#include <gtest/gtest.h>

#include <mutex>
#include <vector>

#include "os/os_port.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"

#ifdef ARDUINO

TEST(SuperframeWakeTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

#include "os/rtos_mock.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

class SuperframeWakeTest : public ::testing::Test {
   protected:
    static constexpr uint16_t kSlots = 10;
    static constexpr uint32_t kSlotMs = 100;

    void SetUp() override {
        mock_ = dynamic_cast<os::RTOSMock*>(&GetRTOS());
        ASSERT_NE(mock_, nullptr);
        mock_->setTimeMode(os::RTOSMock::TimeMode::kVirtualTime);
        service_ = std::make_shared<SuperframeService>(kSlots, kSlotMs);
        service_->SetSuperframeCallback([this](uint16_t slot, bool) {
            uint32_t busy_ms = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                slots_.push_back(slot);
                times_.push_back(GetRTOS().getTickCount());
                std::swap(busy_ms, next_callback_busy_ms_);
            }
            // Stands for the work done right after a transition: posting to
            // the protocol task, which preempts this task, and its logging
            if (busy_ms > 0) {
                GetRTOS().delay(busy_ms);
            }
        });
    }

    void TearDown() override {
        if (service_->IsRunning()) {
            service_->StopSuperframe();
        }
        service_.reset();
        mock_->ResetNodeClocks();
        mock_->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
    }

    std::vector<uint16_t> Slots() {
        std::lock_guard<std::mutex> lock(mutex_);
        return slots_;
    }

    std::vector<uint32_t> Times() {
        std::lock_guard<std::mutex> lock(mutex_);
        return times_;
    }

    /// The next slot callback keeps the update task busy for @p ms
    void BusyInNextCallback(uint32_t ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        next_callback_busy_ms_ = ms;
    }

    os::RTOSMock* mock_ = nullptr;
    std::shared_ptr<SuperframeService> service_;
    std::mutex mutex_;
    std::vector<uint16_t> slots_;
    std::vector<uint32_t> times_;
    uint32_t next_callback_busy_ms_ = 0;
};

TEST_F(SuperframeWakeTest, SlotStartedBeforeTheWakeNoticeIsNotSkipped) {
    ASSERT_TRUE(
        service_->ResumeAt(GetRTOS().getTickCount(), kSlots, kSlotMs, 0));
    mock_->advanceTime(50);

    // While the MCU sleeps the update task's timer does not run, so it
    // learns about the wake-up only after the next slot has begun
    os::TaskHandle_t task = service_->TestGetUpdateTask();
    ASSERT_NE(task, nullptr);
    GetRTOS().SuspendTask(task);
    mock_->advanceTime(70);  // Slot 1 begins at 100
    service_->NotifyWokeUp();
    GetRTOS().ResumeTask(task);
    mock_->advanceTime(10);

    const auto slots = Slots();
    ASSERT_FALSE(slots.empty());
    EXPECT_EQ(slots.front(), 1u);

    // The schedule then continues normally
    mock_->advanceTime(100);
    EXPECT_EQ(Slots().back(), 2u);
}

TEST_F(SuperframeWakeTest, WakeNoticeBeforeTheBoundaryEmitsNothingEarly) {
    ASSERT_TRUE(
        service_->ResumeAt(GetRTOS().getTickCount(), kSlots, kSlotMs, 0));
    mock_->advanceTime(50);
    service_->NotifyWokeUp();
    mock_->advanceTime(10);
    EXPECT_TRUE(Slots().empty());

    mock_->advanceTime(50);
    ASSERT_EQ(Slots().size(), 1u);
    EXPECT_EQ(Slots().front(), 1u);
}

TEST_F(SuperframeWakeTest, BoundaryPassedWhileHandlingTheWakeIsNotSkipped) {
    const uint32_t start = GetRTOS().getTickCount();
    ASSERT_TRUE(service_->ResumeAt(start, kSlots, kSlotMs, 0));
    mock_->advanceTime(50);

    // The MCU sleeps through slot 1 and wakes 5 ms before slot 2 begins.
    // Handling slot 1 then takes until after that boundary.
    os::TaskHandle_t task = service_->TestGetUpdateTask();
    ASSERT_NE(task, nullptr);
    GetRTOS().SuspendTask(task);
    mock_->advanceTime(145);  // 195
    BusyInNextCallback(10);
    service_->NotifyWokeUp();
    GetRTOS().ResumeTask(task);
    mock_->advanceTime(55);  // 250

    // Slot 2 is handled as soon as the task is free, not skipped
    const auto slots = Slots();
    const auto times = Times();
    ASSERT_EQ(slots, (std::vector<uint16_t>{1, 2}));
    EXPECT_LE(times[1] - start, 2 * kSlotMs + 15);

    // And the next slot starts on time: the task is not left a slot behind
    mock_->advanceTime(60);  // 310
    ASSERT_EQ(Slots().size(), 3u);
    EXPECT_EQ(Slots()[2], 3u);
    EXPECT_EQ(Times()[2] - start, 3 * kSlotMs);
}

TEST_F(SuperframeWakeTest, WaitEndingJustBeforeABoundaryIsNotStretched) {
    const uint32_t start = GetRTOS().getTickCount();
    ASSERT_TRUE(service_->ResumeAt(start, kSlots, kSlotMs, 0));
    mock_->advanceTime(195);
    ASSERT_EQ(Slots(), (std::vector<uint16_t>{1}));

    // A notification wakes the task 5 ms before slot 2
    service_->NotifyWokeUp();
    mock_->advanceTime(10);  // 205

    ASSERT_EQ(Slots(), (std::vector<uint16_t>{1, 2}));
    EXPECT_EQ(Times()[1] - start, 2 * kSlotMs);
}

TEST_F(SuperframeWakeTest, ClockSteppedBackIsNotANewSuperframe) {
    const uint32_t start = GetRTOS().getTickCount();
    ASSERT_TRUE(service_->ResumeAt(start, kSlots, kSlotMs, 0));
    mock_->advanceTime(120);
    ASSERT_EQ(Slots(), (std::vector<uint16_t>{1}));
    const uint32_t superframes =
        service_->GetSuperframeStats().superframes_completed;

    // A clock correction after a sleep moves the clock back into slot 0
    GetRTOS().ContinueTickCountFrom(start + 90);
    service_->NotifyWokeUp();
    mock_->advanceTime(5);  // 95
    EXPECT_EQ(Slots(), (std::vector<uint16_t>{1}));
    EXPECT_EQ(service_->GetSuperframeStats().superframes_completed,
              superframes);

    // Slot 1 is not handled again, and slot 2 starts on the corrected clock
    mock_->advanceTime(100);  // 195
    EXPECT_EQ(Slots(), (std::vector<uint16_t>{1}));
    mock_->advanceTime(10);  // 205
    ASSERT_EQ(Slots(), (std::vector<uint16_t>{1, 2}));
    EXPECT_EQ(Times()[1] - start, 2 * kSlotMs);
    EXPECT_EQ(service_->GetSuperframeStats().superframes_completed,
              superframes);
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO
