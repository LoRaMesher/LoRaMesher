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
            std::lock_guard<std::mutex> lock(mutex_);
            slots_.push_back(slot);
        });
    }

    void TearDown() override {
        if (service_->IsRunning()) {
            service_->StopSuperframe();
        }
        service_.reset();
        mock_->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
    }

    std::vector<uint16_t> Slots() {
        std::lock_guard<std::mutex> lock(mutex_);
        return slots_;
    }

    os::RTOSMock* mock_ = nullptr;
    std::shared_ptr<SuperframeService> service_;
    std::mutex mutex_;
    std::vector<uint16_t> slots_;
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

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO
