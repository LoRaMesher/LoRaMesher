/**
 * @file replay_state_test.cpp
 * @brief Tests of the receive side replay checks
 */

#include <gtest/gtest.h>

#include <set>

#include "security/replay_state.hpp"

namespace loramesher {
namespace security {
namespace {

// --- Data window --------------------------------------------------------------

TEST(ReplayWindowTest, ColdWindowAcceptsTheFirstCounterAndSetsEveryBit) {
    for (const uint32_t counter : {0u, 1u, 1000u, 0x3FFFFFFEu}) {
        ReplayWindow window;
        EXPECT_FALSE(window.HasSeenAny());
        EXPECT_EQ(kDataCounterNone, window.GetHigh());
        EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(counter));
        EXPECT_TRUE(window.HasSeenAny());
        EXPECT_EQ(counter, window.GetState().high);
        EXPECT_EQ(0xFFFFFFFFu, window.GetState().bitmap);
    }
}

TEST(ReplayWindowTest, NothingOlderThanTheFirstCounterIsAccepted) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(100));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.Check(100));
    for (uint32_t counter = 69; counter < 100; ++counter) {
        EXPECT_EQ(ReplayVerdict::kDuplicate, window.Check(counter)) << counter;
    }
    EXPECT_EQ(ReplayVerdict::kTooOld, window.Check(68));
    EXPECT_EQ(ReplayVerdict::kTooOld, window.Check(0));
    EXPECT_EQ(ReplayVerdict::kAccept, window.Check(101));
}

TEST(ReplayWindowTest, AcceptsCountersInOrder) {
    ReplayWindow window;
    for (uint32_t counter = 0; counter < 200; ++counter) {
        EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(counter));
        EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(counter));
    }
}

TEST(ReplayWindowTest, AcceptsOutOfOrderCounterOnceInsideTheWindow) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(100));
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(105));
    EXPECT_EQ(0xFFFFFFE1u, window.GetState().bitmap);

    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(102));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(102));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(100));
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(104));
    EXPECT_EQ(105u, window.GetHigh());
    EXPECT_EQ(0xFFFFFFEBu, window.GetState().bitmap);
}

TEST(ReplayWindowTest, ShiftOfThirtyOneKeepsTheOldestBit) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(7));
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(7 + 31));
    EXPECT_EQ(0x80000001u, window.GetState().bitmap);
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.Check(7));
    EXPECT_EQ(ReplayVerdict::kAccept, window.Check(8));
    EXPECT_EQ(ReplayVerdict::kTooOld, window.Check(6));
}

TEST(ReplayWindowTest, ShiftOfThirtyTwoOrMoreLeavesOnlyTheNewCounter) {
    for (const uint32_t jump : {32u, 33u, 64u, 5000u, 0x20000000u}) {
        SCOPED_TRACE(jump);
        ReplayWindow window;
        ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(7));
        ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(7 + jump));
        EXPECT_EQ(0x00000001u, window.GetState().bitmap);
        EXPECT_EQ(7 + jump, window.GetHigh());
        EXPECT_EQ(ReplayVerdict::kDuplicate, window.Check(7 + jump));
        EXPECT_EQ(ReplayVerdict::kTooOld, window.Check(7));
        EXPECT_EQ(ReplayVerdict::kAccept, window.Check(7 + jump - 1));
        EXPECT_EQ(ReplayVerdict::kAccept, window.Check(7 + jump - 31));
    }
}

TEST(ReplayWindowTest, LateCounterIsAcceptedOnceAfterAJump) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(10));
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(100));
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(69));
    EXPECT_EQ(ReplayVerdict::kDuplicate, window.CheckAndCommit(69));
    EXPECT_EQ(ReplayVerdict::kTooOld, window.CheckAndCommit(68));
    EXPECT_EQ(0x80000001u, window.GetState().bitmap);
}

TEST(ReplayWindowTest, RejectsTheAllOnesValueAndAnythingBeyondTheSpace) {
    ReplayWindow window;
    for (const uint32_t counter :
         {0x3FFFFFFFu, 0x40000000u, 0x80000029u, 0xFFFFFFFFu}) {
        EXPECT_EQ(ReplayVerdict::kInvalidCounter,
                  window.CheckAndCommit(counter));
        EXPECT_FALSE(window.HasSeenAny());
    }
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(0x3FFFFFFEu));
    const ReplayWindow::State before = window.GetState();
    EXPECT_EQ(ReplayVerdict::kInvalidCounter,
              window.CheckAndCommit(0x3FFFFFFFu));
    window.Commit(0x3FFFFFFFu);
    EXPECT_TRUE(window.GetState() == before);
}

TEST(ReplayWindowTest, CheckDoesNotChangeTheWindow) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(20));
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(60));
    const ReplayWindow::State before = window.GetState();
    EXPECT_EQ(ReplayVerdict::kAccept, window.Check(500));
    EXPECT_EQ(ReplayVerdict::kAccept, window.Check(55));
    EXPECT_TRUE(window.GetState() == before);
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

TEST(ReplayWindowTest, ResumeKeepsOnlyTheHighestCounter) {
    ReplayWindow window;
    for (const uint32_t counter : {60u, 62u, 65u, 70u, 71u, 150u, 140u}) {
        ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(counter));
    }
    ReplayWindow resumed;
    resumed.RestoreHigh(window.GetHigh());
    EXPECT_EQ(150u, resumed.GetHigh());
    EXPECT_EQ(0xFFFFFFFFu, resumed.GetState().bitmap);
    // Only values above the highest one are accepted
    for (uint32_t counter = 100; counter <= 150; ++counter) {
        EXPECT_NE(ReplayVerdict::kAccept, resumed.Check(counter)) << counter;
    }
    EXPECT_EQ(ReplayVerdict::kAccept, resumed.CheckAndCommit(151));

    // A window that had seen nothing resumes as cold
    ReplayWindow cold;
    cold.RestoreHigh(kDataCounterNone);
    EXPECT_FALSE(cold.HasSeenAny());
    EXPECT_EQ(ReplayVerdict::kAccept, cold.Check(0));
}

TEST(ReplayWindowTest, ResetForgetsEverything) {
    ReplayWindow window;
    ASSERT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(900));
    window.Reset();
    EXPECT_FALSE(window.HasSeenAny());
    EXPECT_EQ(ReplayVerdict::kAccept, window.CheckAndCommit(3));
}

TEST(ReplayWindowTest, NeverAcceptsACounterTwice) {
    // Compare with a plain model over a jittered, lossy, repeating stream
    ReplayWindow window;
    std::set<uint32_t> accepted;
    uint32_t first = 0;
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

        ReplayVerdict expected = ReplayVerdict::kAccept;
        if (!accepted.empty()) {
            if (counter <= highest &&
                highest - counter >= ReplayWindow::kWindowSize) {
                expected = ReplayVerdict::kTooOld;
            } else if (accepted.count(counter) != 0 ||
                       (counter < first && counter <= highest)) {
                // Values below the first accepted one count as seen
                expected = ReplayVerdict::kDuplicate;
            }
        }
        ASSERT_EQ(expected, window.CheckAndCommit(counter)) << step;
        if (expected == ReplayVerdict::kAccept) {
            if (accepted.empty()) {
                first = counter;
            }
            accepted.insert(counter);
            if (counter > highest) {
                highest = counter;
            }
        }
    }
    EXPECT_GT(accepted.size(), 1000u);
}

// --- Strict counters ----------------------------------------------------------

TEST(StrictReplayCounterTest, AcceptsOnlyRisingCounters) {
    StrictReplayCounter control(kControlCounterNone);
    EXPECT_FALSE(control.HasSeenAny());
    EXPECT_EQ(kControlCounterNone, control.GetLast());
    EXPECT_EQ(ReplayVerdict::kAccept, control.CheckAndCommit(0));
    EXPECT_TRUE(control.HasSeenAny());
    EXPECT_EQ(ReplayVerdict::kDuplicate, control.CheckAndCommit(0));
    EXPECT_EQ(ReplayVerdict::kAccept, control.CheckAndCommit(1));
    EXPECT_EQ(ReplayVerdict::kAccept, control.CheckAndCommit(10));
    EXPECT_EQ(ReplayVerdict::kDuplicate, control.CheckAndCommit(10));
    EXPECT_EQ(ReplayVerdict::kTooOld, control.CheckAndCommit(9));
    EXPECT_EQ(ReplayVerdict::kTooOld, control.CheckAndCommit(0));
    EXPECT_EQ(ReplayVerdict::kAccept, control.CheckAndCommit(0xFFFFFFFEu));
    EXPECT_EQ(ReplayVerdict::kDuplicate, control.CheckAndCommit(0xFFFFFFFEu));
    EXPECT_EQ(0xFFFFFFFEu, control.GetLast());
}

TEST(StrictReplayCounterTest, FirstCounterMayBeAnyValue) {
    StrictReplayCounter beacon(kBeaconCounterNone);
    EXPECT_EQ(ReplayVerdict::kAccept, beacon.CheckAndCommit(70000));
    EXPECT_EQ(ReplayVerdict::kTooOld, beacon.Check(69999));
}

TEST(StrictReplayCounterTest, AllOnesValueNeverClearsTheState) {
    StrictReplayCounter beacon(kBeaconCounterNone);
    EXPECT_EQ(ReplayVerdict::kInvalidCounter, beacon.CheckAndCommit(0xFFFFFFu));
    EXPECT_EQ(ReplayVerdict::kInvalidCounter,
              beacon.CheckAndCommit(0x1000000u));
    EXPECT_FALSE(beacon.HasSeenAny());

    ASSERT_EQ(ReplayVerdict::kAccept, beacon.CheckAndCommit(0xFFFFFEu));
    EXPECT_EQ(ReplayVerdict::kInvalidCounter, beacon.CheckAndCommit(0xFFFFFFu));
    beacon.Commit(0xFFFFFFu);
    EXPECT_EQ(0xFFFFFEu, beacon.GetLast());
    EXPECT_EQ(ReplayVerdict::kTooOld, beacon.Check(5));

    StrictReplayCounter control(kControlCounterNone);
    ASSERT_EQ(ReplayVerdict::kAccept, control.CheckAndCommit(40));
    EXPECT_EQ(ReplayVerdict::kInvalidCounter,
              control.CheckAndCommit(0xFFFFFFFFu));
    EXPECT_EQ(40u, control.GetLast());
}

TEST(StrictReplayCounterTest, CheckAndRejectedCommitChangeNothing) {
    StrictReplayCounter control(kControlCounterNone);
    ASSERT_EQ(ReplayVerdict::kAccept, control.CheckAndCommit(40));
    EXPECT_EQ(ReplayVerdict::kAccept, control.Check(41));
    control.Commit(40);
    control.Commit(12);
    EXPECT_EQ(40u, control.GetLast());
}

TEST(StrictReplayCounterTest, RestoreAndReset) {
    StrictReplayCounter beacon(kBeaconCounterNone);
    beacon.Restore(77);
    EXPECT_EQ(ReplayVerdict::kDuplicate, beacon.Check(77));
    EXPECT_EQ(ReplayVerdict::kAccept, beacon.Check(78));

    beacon.Restore(kBeaconCounterNone);
    EXPECT_FALSE(beacon.HasSeenAny());
    beacon.Restore(77);
    beacon.Reset();
    EXPECT_FALSE(beacon.HasSeenAny());
    EXPECT_EQ(ReplayVerdict::kAccept, beacon.Check(1));
}

// --- Per-peer state and the epoch rule ------------------------------------------

TEST(HopReplayStateTest, StartsWithNothingSeenInEverySpace) {
    const HopReplayState peer(3);
    EXPECT_EQ(3u, peer.epoch);
    EXPECT_EQ(0xFFFFFFu, peer.beacon.GetLast());
    EXPECT_EQ(0xFFFFFFFFu, peer.control.GetLast());
    EXPECT_EQ(0x3FFFFFFFu, peer.group.GetHigh());
    EXPECT_EQ(0x3FFFFFFFu, peer.relayed.GetHigh());
}

TEST(HopReplayStateTest, WindowsAreIndependent) {
    HopReplayState peer(1);
    ASSERT_EQ(EpochVerdict::kCurrent, peer.AcceptEpoch(1));
    EXPECT_EQ(ReplayVerdict::kAccept, peer.group.CheckAndCommit(50));
    // The same origin counter in the other window is a different frame class
    EXPECT_EQ(ReplayVerdict::kAccept, peer.relayed.CheckAndCommit(50));
    EXPECT_EQ(ReplayVerdict::kAccept, peer.beacon.CheckAndCommit(50));
    EXPECT_EQ(ReplayVerdict::kAccept, peer.control.CheckAndCommit(50));
    EXPECT_EQ(ReplayVerdict::kDuplicate, peer.group.Check(50));
    EXPECT_EQ(ReplayVerdict::kDuplicate, peer.relayed.Check(50));
}

TEST(HopReplayStateTest, OlderEpochIsDroppedAndNewerEpochClearsEverything) {
    HopReplayState peer(5);
    ASSERT_EQ(ReplayVerdict::kAccept, peer.beacon.CheckAndCommit(900));
    ASSERT_EQ(ReplayVerdict::kAccept, peer.control.CheckAndCommit(901));
    ASSERT_EQ(ReplayVerdict::kAccept, peer.group.CheckAndCommit(902));
    ASSERT_EQ(ReplayVerdict::kAccept, peer.relayed.CheckAndCommit(903));

    EXPECT_EQ(EpochVerdict::kOlder, peer.AcceptEpoch(4));
    EXPECT_EQ(5u, peer.epoch);
    EXPECT_EQ(900u, peer.beacon.GetLast());

    EXPECT_EQ(EpochVerdict::kNewer, peer.AcceptEpoch(6));
    EXPECT_EQ(6u, peer.epoch);
    EXPECT_FALSE(peer.beacon.HasSeenAny());
    EXPECT_FALSE(peer.control.HasSeenAny());
    EXPECT_FALSE(peer.group.HasSeenAny());
    EXPECT_FALSE(peer.relayed.HasSeenAny());
    // Counters restart at an epoch switch, so low values are accepted again
    EXPECT_EQ(ReplayVerdict::kAccept, peer.control.CheckAndCommit(0));
    EXPECT_EQ(EpochVerdict::kCurrent, peer.AcceptEpoch(6));
    EXPECT_EQ(0u, peer.control.GetLast());
}

TEST(EndToEndReplayStateTest, FollowsTheSameEpochRule) {
    EndToEndReplayState peer(2);
    ASSERT_EQ(EpochVerdict::kCurrent, peer.AcceptEpoch(2));
    ASSERT_EQ(ReplayVerdict::kAccept, peer.window.CheckAndCommit(41));
    EXPECT_EQ(EpochVerdict::kOlder, peer.AcceptEpoch(1));
    EXPECT_EQ(ReplayVerdict::kDuplicate, peer.window.Check(41));
    EXPECT_EQ(EpochVerdict::kNewer, peer.AcceptEpoch(3));
    EXPECT_EQ(3u, peer.epoch);
    EXPECT_EQ(ReplayVerdict::kAccept, peer.window.Check(0));
}

}  // namespace
}  // namespace security
}  // namespace loramesher
