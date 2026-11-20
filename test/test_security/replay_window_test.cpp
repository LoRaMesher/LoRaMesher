/**
 * @file replay_window_test.cpp
 * @brief Tests of the replay window and of the highest-only check
 */

#include <gtest/gtest.h>

#include <set>

#include "security/replay_window.hpp"

namespace loramesher {
namespace security {
namespace {

TEST(ReplayWindowTest, EmptyWindowAcceptsAnyCounter) {
    for (const uint32_t counter : {0u, 1u, 1000u, 0xFFFFFFFEu}) {
        ReplayWindow window;
        EXPECT_FALSE(window.HasSeenAny());
        EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(counter));
        EXPECT_TRUE(window.HasSeenAny());
        EXPECT_EQ(counter, window.GetState().highest);
    }
}

TEST(ReplayWindowTest, AcceptsCountersInOrder) {
    ReplayWindow window;
    for (uint32_t counter = 0; counter < 200; ++counter) {
        EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(counter));
    }
}

TEST(ReplayWindowTest, RejectsDuplicateOfTheHighestCounter) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(0));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(0));
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(50));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(50));
}

TEST(ReplayWindowTest, AcceptsOutOfOrderCounterOnceInsideTheWindow) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(100));
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(105));

    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(102));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(102));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(100));
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(104));
    EXPECT_EQ(105u, window.GetState().highest);
}

TEST(ReplayWindowTest, WindowEdgeIsThirtyOneBehindTheHighest) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(1000));

    EXPECT_EQ(ReplayVerdict::kTooOld, window.Check(1000 - 32));
    EXPECT_EQ(ReplayVerdict::kTooOld, window.Check(0));
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(1000 - 31));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(1000 - 31));
}

TEST(ReplayWindowTest, AdvancingDropsCountersThatLeaveTheWindow) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(10));
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(41));
    // 10 is 31 behind and still tracked
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.Check(10));
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(42));
    EXPECT_EQ(ReplayVerdict::kTooOld, window.Check(10));
    // 11 was never seen and is the oldest tracked counter
    EXPECT_EQ(ReplayVerdict::kAccept, window.Check(11));
}

TEST(ReplayWindowTest, JumpOfAWholeWindowClearsTheHistory) {
    for (const uint32_t jump : {32u, 33u, 5000u, 0x80000000u}) {
        SCOPED_TRACE(jump);
        ReplayWindow window;
        ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(7));
        ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(7 + jump));
        EXPECT_EQ(1u, window.GetState().bitmap);
        EXPECT_EQ(ReplayVerdict::kDuplicate, window.Check(7 + jump));
        EXPECT_EQ(ReplayVerdict::kTooOld, window.Check(7));
        EXPECT_EQ(ReplayVerdict::kAccept, window.Check(7 + jump - 1));
    }
}

TEST(ReplayWindowTest, JumpJustInsideTheWindowKeepsTheHistory) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(7));
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(7 + 31));
    EXPECT_EQ(0x80000001u, window.GetState().bitmap);
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.Check(7));
}

TEST(ReplayWindowTest, CheckDoesNotChangeTheWindow) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(20));
    const ReplayWindow::State before = window.GetState();
    EXPECT_EQ(ReplayVerdict::kAccept, window.Check(500));
    EXPECT_EQ(ReplayVerdict::kAccept, window.Check(15));
    EXPECT_TRUE(window.GetState() == before);
    // A forged frame that fails its tag never reaches Commit()
    EXPECT_EQ(ReplayVerdict::kAccept, window.Check(500));
}

TEST(ReplayWindowTest, CommitOfARejectedCounterChangesNothing) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(100));
    const ReplayWindow::State before = window.GetState();
    window.Commit(100);
    window.Commit(100 - 32);
    window.Commit(3);
    EXPECT_TRUE(window.GetState() == before);
}

TEST(ReplayWindowTest, StateRoundTripKeepsEveryVerdict) {
    ReplayWindow window;
    for (const uint32_t counter : {60u, 62u, 65u, 70u, 71u, 90u}) {
        ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(counter));
    }
    ReplayWindow restored;
    restored.Restore(window.GetState());
    EXPECT_TRUE(restored.GetState() == window.GetState());
    for (uint32_t counter = 0; counter < 130; ++counter) {
        EXPECT_EQ(window.Check(counter), restored.Check(counter)) << counter;
    }
}

TEST(ReplayWindowTest, ResetForgetsEverything) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(900));
    window.Reset();
    EXPECT_FALSE(window.HasSeenAny());
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(3));
}

TEST(ReplayWindowTest, HighestCounterValueIsHandled) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(0xFFFFFFFEu));
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(0xFFFFFFFFu));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.Check(0xFFFFFFFFu));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.Check(0xFFFFFFFEu));
    EXPECT_EQ(ReplayVerdict::kAccept, window.Check(0xFFFFFFFDu));
    EXPECT_EQ(ReplayVerdict::kTooOld, window.Check(0));
}

TEST(ReplayWindowTest, NeverAcceptsACounterTwice) {
    // Compare with a plain model over a jittered, lossy, repeating stream
    ReplayWindow window;
    std::set<uint32_t> accepted;
    uint32_t highest = 0;
    uint32_t random = 0x12345678u;
    uint32_t base = 0;
    for (int step = 0; step < 20000; ++step) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        base += (random >> 8) % 3;
        const uint32_t jitter = (random >> 16) % 48;
        const uint32_t counter = base + 50 - jitter;

        const bool tracked = accepted.empty() || counter > highest ||
                             highest - counter < ReplayWindow::kWindowSize;
        const ReplayVerdict expected =
            !tracked ? ReplayVerdict::kTooOld
                     : (accepted.count(counter) != 0 ? ReplayVerdict::kDuplicate
                                                     : ReplayVerdict::kAccept);
        ASSERT_EQ(expected, window.CheckAndCommit(counter)) << step;
        if (expected == ReplayVerdict::kAccept) {
            accepted.insert(counter);
            if (counter > highest) {
                highest = counter;
            }
        }
    }
    EXPECT_GT(accepted.size(), 1000u);
}

TEST(HighestCounterWindowTest, AcceptsOnlyRisingCounters) {
    HighestCounterWindow window;
    EXPECT_FALSE(window.HasSeenAny());
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(0));
    EXPECT_TRUE(window.HasSeenAny());
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(0));
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(1));
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(10));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(10));
    EXPECT_EQ(ReplayVerdict::kTooOld, window.CheckAndCommit(9));
    EXPECT_EQ(ReplayVerdict::kTooOld, window.CheckAndCommit(0));
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(0xFFFFFFFFu));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(0xFFFFFFFFu));
}

TEST(HighestCounterWindowTest, FirstCounterMayBeAnyValue) {
    HighestCounterWindow window;
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(70000));
    EXPECT_EQ(ReplayVerdict::kTooOld, window.Check(69999));
}

TEST(HighestCounterWindowTest, CheckAndRejectedCommitChangeNothing) {
    HighestCounterWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(40));
    const HighestCounterWindow::State before = window.GetState();
    EXPECT_EQ(ReplayVerdict::kAccept, window.Check(41));
    window.Commit(40);
    window.Commit(12);
    EXPECT_TRUE(window.GetState() == before);
}

TEST(HighestCounterWindowTest, StateRoundTripAndReset) {
    HighestCounterWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(77));
    HighestCounterWindow restored;
    restored.Restore(window.GetState());
    EXPECT_TRUE(restored.GetState() == window.GetState());
    EXPECT_EQ(ReplayVerdict::kDuplicate, restored.Check(77));
    EXPECT_EQ(ReplayVerdict::kAccept, restored.Check(78));

    restored.Reset();
    EXPECT_FALSE(restored.HasSeenAny());
    EXPECT_EQ(ReplayVerdict::kAccept, restored.Check(1));
}

}  // namespace
}  // namespace security
}  // namespace loramesher
