/**
 * @file superframe_resume_test.cpp
 * @brief Tests of resuming a superframe after the node slept through part of it
 */

#include <gtest/gtest.h>

#include <mutex>
#include <utility>
#include <vector>

#include "os/os_port.hpp"
#include "protocols/lora_mesh/services/superframe_service.hpp"

#ifdef ARDUINO

TEST(SuperframeResumeTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

#include "os/rtos_mock.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

class SuperframeResumeTest : public ::testing::Test {
   protected:
    static constexpr uint16_t kSlots = 10;
    static constexpr uint32_t kSlotMs = 100;
    static constexpr uint32_t kSuperframeMs = kSlots * kSlotMs;

    void SetUp() override {
        mock_ = dynamic_cast<os::RTOSMock*>(&GetRTOS());
        ASSERT_NE(mock_, nullptr);
        mock_->setTimeMode(os::RTOSMock::TimeMode::kVirtualTime);
        service_ = std::make_shared<SuperframeService>(16, 1000);
        service_->SetSuperframeCallback([this](uint16_t slot, bool new_frame) {
            std::lock_guard<std::mutex> lock(mutex_);
            transitions_.emplace_back(slot, new_frame);
        });
        now_ = GetRTOS().getTickCount();
    }

    void TearDown() override {
        if (service_->IsRunning()) {
            service_->StopSuperframe();
        }
        service_.reset();
        mock_->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
    }

    using Transition = std::pair<uint16_t, bool>;

    std::vector<Transition> Transitions() {
        std::lock_guard<std::mutex> lock(mutex_);
        return transitions_;
    }

    os::RTOSMock* mock_ = nullptr;
    std::shared_ptr<SuperframeService> service_;
    uint32_t now_ = 0;
    std::mutex mutex_;
    std::vector<Transition> transitions_;
};

TEST_F(SuperframeResumeTest, ProjectsPhaseFromLastKnownStart) {
    // The node last saw a superframe start 2.55 superframes ago
    ASSERT_TRUE(service_->ResumeAt(now_ - 2550, kSlots, kSlotMs, 7));

    EXPECT_TRUE(service_->IsRunning());
    EXPECT_TRUE(service_->IsSynchronized());
    EXPECT_EQ(service_->GetSlotDuration(), kSlotMs);
    EXPECT_EQ(service_->GetSuperframeDuration(), kSuperframeMs);
    EXPECT_EQ(service_->GetSlotStartTime(0), now_ - 550);
    EXPECT_EQ(service_->GetCurrentSlot(), 5u);
    EXPECT_EQ(service_->GetSuperframeStats().superframes_completed, 9u);
}

TEST_F(SuperframeResumeTest, ContinuesWithNextSlotAndSuperframe) {
    ASSERT_TRUE(service_->ResumeAt(now_ - 2550, kSlots, kSlotMs, 0));

    // Into slot 0 of the next superframe, which starts 450 ms from now
    mock_->advanceTime(500);

    const auto transitions = Transitions();
    ASSERT_FALSE(transitions.empty());
    EXPECT_EQ(transitions.front(), Transition(6, false));
    EXPECT_EQ(transitions.back(), Transition(0, true));
    EXPECT_EQ(service_->GetSlotStartTime(0), now_ + 450);
    // A member keeps the network's phase: a new superframe starts exactly one
    // superframe after the previous one
    mock_->advanceTime(kSuperframeMs);
    EXPECT_EQ(service_->GetSlotStartTime(0), now_ + 450 + kSuperframeMs);
}

TEST_F(SuperframeResumeTest, ResumeAtSuperframeBoundary) {
    ASSERT_TRUE(
        service_->ResumeAt(now_ - 3 * kSuperframeMs, kSlots, kSlotMs, 0));

    EXPECT_EQ(service_->GetCurrentSlot(), 0u);
    EXPECT_EQ(service_->GetSlotStartTime(0), now_);
    mock_->advanceTime(150);
    const auto transitions = Transitions();
    ASSERT_FALSE(transitions.empty());
    EXPECT_EQ(transitions.back().first, 1u);
}

TEST_F(SuperframeResumeTest, RefusedWhileRunning) {
    ASSERT_TRUE(service_->StartSuperframe());
    EXPECT_FALSE(service_->ResumeAt(now_, kSlots, kSlotMs, 0));
}

TEST_F(SuperframeResumeTest, RejectsInvalidSchedule) {
    EXPECT_FALSE(service_->ResumeAt(now_, 0, kSlotMs, 0));
    EXPECT_FALSE(service_->ResumeAt(now_, kSlots, 0, 0));
    // A start in the future cannot be projected
    EXPECT_FALSE(service_->ResumeAt(now_ + 10, kSlots, kSlotMs, 0));
    EXPECT_FALSE(service_->IsRunning());
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher

#endif  // ARDUINO
