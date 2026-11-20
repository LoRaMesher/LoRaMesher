/**
 * @file frame_counter_test.cpp
 * @brief Tests of the reserve-ahead transmit frame counter
 */

#include <gtest/gtest.h>

#include <memory>
#include <set>

#include "security/frame_counter.hpp"
#include "security/memory_counter_store.hpp"

namespace loramesher {
namespace security {
namespace {

constexpr uint32_t kSmallBlock = 8;

/**
 * @brief Store that loses power in the middle of a write
 *
 * The armed Store() reports a failure. Whether the new mark reached the
 * medium is chosen by the test, since a real power loss can go either way.
 */
class TornWriteStore : public MemoryCounterStore {
   public:
    void ArmTornWrite(bool mark_persists) {
        armed_ = true;
        mark_persists_ = mark_persists;
    }

    Result Store(uint32_t mark) override {
        if (armed_) {
            armed_ = false;
            if (mark_persists_) {
                MemoryCounterStore::Store(mark);
            }
            return Result(LoraMesherErrorCode::kHardwareError, "Power lost");
        }
        return MemoryCounterStore::Store(mark);
    }

   private:
    bool armed_ = false;
    bool mark_persists_ = false;
};

/**
 * @brief Records every value handed out across restarts
 *
 * Fails the test when a value repeats, goes backwards, or is not covered by
 * the persisted mark at the moment it is handed out.
 */
class CounterAudit {
   public:
    explicit CounterAudit(std::shared_ptr<MemoryCounterStore> store)
        : store_(std::move(store)) {}

    /// Draw count values and audit each of them
    void Draw(FrameCounter& counter, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t value = 0;
            ASSERT_TRUE(counter.Next(value).IsSuccess());
            Record(value);
        }
    }

    void Record(uint32_t value) {
        EXPECT_LT(value, store_->GetMark()) << "value above persisted mark";
        EXPECT_TRUE(seen_.insert(value).second) << "repeated value " << value;
        if (has_last_) {
            EXPECT_GT(value, last_);
        }
        last_ = value;
        has_last_ = true;
    }

    size_t GetCount() const { return seen_.size(); }

   private:
    std::shared_ptr<MemoryCounterStore> store_;
    std::set<uint32_t> seen_;
    uint32_t last_ = 0;
    bool has_last_ = false;
};

TEST(FrameCounterTest, NextFailsBeforeStart) {
    FrameCounter counter(std::make_shared<MemoryCounterStore>());
    uint32_t value = 77;
    const Result result = counter.Next(value);
    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(LoraMesherErrorCode::kNotInitialized, result.getErrorCode());
    EXPECT_EQ(77u, value);
    EXPECT_FALSE(counter.IsStarted());
}

TEST(FrameCounterTest, StartWithoutStoreFails) {
    FrameCounter counter(nullptr);
    EXPECT_FALSE(counter.Start().IsSuccess());
    uint32_t value = 0;
    EXPECT_FALSE(counter.Next(value).IsSuccess());
    EXPECT_FALSE(counter.Restore({0, 0}).IsSuccess());
}

TEST(FrameCounterTest, FreshStoreCountsFromZero) {
    auto store = std::make_shared<MemoryCounterStore>();
    FrameCounter counter(store);
    ASSERT_TRUE(counter.Start().IsSuccess());
    for (uint32_t expected = 0; expected < 5; ++expected) {
        uint32_t value = 99;
        ASSERT_TRUE(counter.Next(value).IsSuccess());
        EXPECT_EQ(expected, value);
    }
}

TEST(FrameCounterTest, StartWritesNothing) {
    auto store = std::make_shared<MemoryCounterStore>(4096);
    FrameCounter counter(store);
    ASSERT_TRUE(counter.Start().IsSuccess());
    EXPECT_EQ(0u, store->GetStoreCount());
    EXPECT_EQ(4096u, store->GetMark());
}

TEST(FrameCounterTest, MarkIsStoredBeforeTheFirstValueOfEachBlock) {
    auto store = std::make_shared<MemoryCounterStore>();
    FrameCounter counter(store);
    CounterAudit audit(store);
    ASSERT_TRUE(counter.Start().IsSuccess());

    audit.Draw(counter, 1);
    EXPECT_EQ(1u, store->GetStoreCount());
    EXPECT_EQ(FrameCounter::kDefaultBlockSize, store->GetMark());

    // The rest of the block costs no write
    audit.Draw(counter, FrameCounter::kDefaultBlockSize - 1);
    EXPECT_EQ(1u, store->GetStoreCount());

    audit.Draw(counter, 1);
    EXPECT_EQ(2u, store->GetStoreCount());
    EXPECT_EQ(2 * FrameCounter::kDefaultBlockSize, store->GetMark());
    EXPECT_EQ(FrameCounter::kDefaultBlockSize + 1, counter.GetState().next);
}

TEST(FrameCounterTest, BlockSizeIsConfigurable) {
    auto store = std::make_shared<MemoryCounterStore>();
    FrameCounter counter(store, 16);
    CounterAudit audit(store);
    ASSERT_TRUE(counter.Start().IsSuccess());
    audit.Draw(counter, 40);
    EXPECT_EQ(3u, store->GetStoreCount());
    EXPECT_EQ(48u, store->GetMark());
}

TEST(FrameCounterTest, BlockSizeZeroReservesOneValuePerWrite) {
    auto store = std::make_shared<MemoryCounterStore>();
    FrameCounter counter(store, 0);
    CounterAudit audit(store);
    ASSERT_TRUE(counter.Start().IsSuccess());
    audit.Draw(counter, 5);
    EXPECT_EQ(5u, store->GetStoreCount());
    EXPECT_EQ(5u, store->GetMark());
}

TEST(FrameCounterTest, StartJumpsToThePersistedMark) {
    auto store = std::make_shared<MemoryCounterStore>(5000);
    FrameCounter counter(store);
    ASSERT_TRUE(counter.Start().IsSuccess());
    uint32_t value = 0;
    ASSERT_TRUE(counter.Next(value).IsSuccess());
    EXPECT_EQ(5000u, value);
    EXPECT_EQ(5000u + FrameCounter::kDefaultBlockSize, store->GetMark());
}

TEST(FrameCounterTest, RestartAtEveryPointOfTheCycleNeverRepeats) {
    // The crash point sweeps three reserve cycles: before the first reserve,
    // right after a reserve, inside a block and on its last value
    for (uint32_t crash_after = 0; crash_after <= 3 * kSmallBlock;
         ++crash_after) {
        SCOPED_TRACE(crash_after);
        auto store = std::make_shared<MemoryCounterStore>();
        CounterAudit audit(store);
        {
            FrameCounter counter(store, kSmallBlock);
            ASSERT_TRUE(counter.Start().IsSuccess());
            audit.Draw(counter, crash_after);
        }
        const uint32_t mark_at_crash = store->GetMark();

        FrameCounter restarted(store, kSmallBlock);
        ASSERT_TRUE(restarted.Start().IsSuccess());
        EXPECT_EQ(mark_at_crash, restarted.GetState().next);
        audit.Draw(restarted, 3 * kSmallBlock);
        EXPECT_EQ(crash_after + 3 * kSmallBlock, audit.GetCount());
    }
}

TEST(FrameCounterTest, RepeatedRestartsNeverRepeat) {
    auto store = std::make_shared<MemoryCounterStore>();
    CounterAudit audit(store);
    for (uint32_t boot = 0; boot < 200; ++boot) {
        FrameCounter counter(store, kSmallBlock);
        ASSERT_TRUE(counter.Start().IsSuccess());
        audit.Draw(counter, (boot * 7) % 21);
    }
}

TEST(FrameCounterTest, PowerLossDuringTheStoreWriteNeverRepeats) {
    for (const bool mark_persists : {false, true}) {
        for (uint32_t before = 0; before <= 2 * kSmallBlock;
             before += kSmallBlock) {
            SCOPED_TRACE(mark_persists);
            SCOPED_TRACE(before);
            auto store = std::make_shared<TornWriteStore>();
            CounterAudit audit(store);
            {
                FrameCounter counter(store, kSmallBlock);
                ASSERT_TRUE(counter.Start().IsSuccess());
                audit.Draw(counter, before);

                // The next call must reserve, and the write is torn
                store->ArmTornWrite(mark_persists);
                uint32_t value = 0;
                EXPECT_FALSE(counter.Next(value).IsSuccess());
            }
            FrameCounter restarted(store, kSmallBlock);
            ASSERT_TRUE(restarted.Start().IsSuccess());
            audit.Draw(restarted, 3 * kSmallBlock);
        }
    }
}

TEST(FrameCounterTest, CounterSurvivesATornWriteWithoutRestart) {
    for (const bool mark_persists : {false, true}) {
        SCOPED_TRACE(mark_persists);
        auto store = std::make_shared<TornWriteStore>();
        CounterAudit audit(store);
        FrameCounter counter(store, kSmallBlock);
        ASSERT_TRUE(counter.Start().IsSuccess());
        audit.Draw(counter, kSmallBlock);

        store->ArmTornWrite(mark_persists);
        uint32_t value = 0;
        EXPECT_FALSE(counter.Next(value).IsSuccess());
        audit.Draw(counter, 2 * kSmallBlock);
    }
}

TEST(FrameCounterTest, StoreFailureStopsTheCounter) {
    auto store = std::make_shared<MemoryCounterStore>();
    CounterAudit audit(store);
    FrameCounter counter(store, kSmallBlock);
    ASSERT_TRUE(counter.Start().IsSuccess());
    audit.Draw(counter, kSmallBlock);
    const FrameCounter::State before = counter.GetState();

    store->SetStoreFailure(true);
    for (int attempt = 0; attempt < 5; ++attempt) {
        uint32_t value = 12345;
        const Result result = counter.Next(value);
        EXPECT_FALSE(result.IsSuccess());
        EXPECT_EQ(LoraMesherErrorCode::kHardwareError, result.getErrorCode());
        EXPECT_EQ(12345u, value);
    }
    EXPECT_TRUE(counter.GetState() == before);
    EXPECT_EQ(kSmallBlock, store->GetMark());

    // The counter continues without a gap once the store works again
    store->SetStoreFailure(false);
    uint32_t value = 0;
    ASSERT_TRUE(counter.Next(value).IsSuccess());
    EXPECT_EQ(kSmallBlock, value);
    audit.Record(value);
}

TEST(FrameCounterTest, StoreFailureOnTheFirstValueHandsOutNothing) {
    auto store = std::make_shared<MemoryCounterStore>();
    store->SetStoreFailure(true);
    FrameCounter counter(store);
    ASSERT_TRUE(counter.Start().IsSuccess());
    uint32_t value = 0;
    EXPECT_FALSE(counter.Next(value).IsSuccess());
    EXPECT_EQ(0u, store->GetMark());
}

TEST(FrameCounterTest, LoadFailureKeepsTheCounterStopped) {
    auto store = std::make_shared<MemoryCounterStore>(100);
    store->SetLoadFailure(true);
    FrameCounter counter(store);
    EXPECT_FALSE(counter.Start().IsSuccess());
    EXPECT_FALSE(counter.IsStarted());
    uint32_t value = 0;
    EXPECT_FALSE(counter.Next(value).IsSuccess());

    store->SetLoadFailure(false);
    ASSERT_TRUE(counter.Start().IsSuccess());
    ASSERT_TRUE(counter.Next(value).IsSuccess());
    EXPECT_EQ(100u, value);
}

TEST(FrameCounterTest, SnapshotRoundTripDoesNotBurnABlock) {
    auto store = std::make_shared<MemoryCounterStore>();
    CounterAudit audit(store);
    FrameCounter::State snapshot;
    {
        FrameCounter counter(store, kSmallBlock);
        ASSERT_TRUE(counter.Start().IsSuccess());
        audit.Draw(counter, kSmallBlock + 3);
        snapshot = counter.GetState();
    }
    EXPECT_EQ(kSmallBlock + 3, snapshot.next);
    EXPECT_EQ(2 * kSmallBlock, snapshot.reserved_end);
    const uint32_t writes_before_sleep = store->GetStoreCount();

    FrameCounter resumed(store, kSmallBlock);
    ASSERT_TRUE(resumed.Restore(snapshot).IsSuccess());
    EXPECT_TRUE(resumed.IsStarted());
    EXPECT_TRUE(resumed.GetState() == snapshot);
    EXPECT_EQ(writes_before_sleep, store->GetStoreCount());

    // The rest of the reserved block is used without a write
    audit.Draw(resumed, kSmallBlock - 3);
    EXPECT_EQ(writes_before_sleep, store->GetStoreCount());
    EXPECT_EQ(2 * kSmallBlock, resumed.GetState().next);

    audit.Draw(resumed, 1);
    EXPECT_EQ(writes_before_sleep + 1, store->GetStoreCount());
}

TEST(FrameCounterTest, SnapshotTakenBeforeTheFirstReserveRoundTrips) {
    auto store = std::make_shared<MemoryCounterStore>(64);
    FrameCounter counter(store, kSmallBlock);
    ASSERT_TRUE(counter.Start().IsSuccess());
    const FrameCounter::State snapshot = counter.GetState();

    FrameCounter resumed(store, kSmallBlock);
    ASSERT_TRUE(resumed.Restore(snapshot).IsSuccess());
    uint32_t value = 0;
    ASSERT_TRUE(resumed.Next(value).IsSuccess());
    EXPECT_EQ(64u, value);
    EXPECT_EQ(64u + kSmallBlock, store->GetMark());
}

TEST(FrameCounterTest, RestoreRejectsASnapshotOlderThanTheStore) {
    auto store = std::make_shared<MemoryCounterStore>();
    CounterAudit audit(store);
    FrameCounter counter(store, kSmallBlock);
    ASSERT_TRUE(counter.Start().IsSuccess());
    audit.Draw(counter, 3);
    const FrameCounter::State stale = counter.GetState();
    audit.Draw(counter, kSmallBlock);

    FrameCounter resumed(store, kSmallBlock);
    const Result result = resumed.Restore(stale);
    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(LoraMesherErrorCode::kInvalidState, result.getErrorCode());
    EXPECT_FALSE(resumed.IsStarted());

    // The fallback is a cold start, which skips the open block
    ASSERT_TRUE(resumed.Start().IsSuccess());
    audit.Draw(resumed, kSmallBlock);
}

TEST(FrameCounterTest, RestoreRejectsInconsistentSnapshots) {
    auto store = std::make_shared<MemoryCounterStore>(32);
    FrameCounter counter(store, kSmallBlock);
    // Mark ahead of the store
    EXPECT_FALSE(counter.Restore({40, 48}).IsSuccess());
    // Position beyond its own mark
    EXPECT_FALSE(counter.Restore({33, 32}).IsSuccess());
    EXPECT_FALSE(counter.IsStarted());
    // Position at the mark is the state right after a start
    EXPECT_TRUE(counter.Restore({32, 32}).IsSuccess());
}

TEST(FrameCounterTest, RestoreFailsWhenTheStoreCannotBeRead) {
    auto store = std::make_shared<MemoryCounterStore>(32);
    store->SetLoadFailure(true);
    FrameCounter counter(store, kSmallBlock);
    EXPECT_FALSE(counter.Restore({30, 32}).IsSuccess());
    EXPECT_FALSE(counter.IsStarted());
}

TEST(FrameCounterTest, FailedRestoreLeavesARunningCounterUntouched) {
    auto store = std::make_shared<MemoryCounterStore>();
    FrameCounter counter(store, kSmallBlock);
    ASSERT_TRUE(counter.Start().IsSuccess());
    uint32_t value = 0;
    ASSERT_TRUE(counter.Next(value).IsSuccess());
    const FrameCounter::State before = counter.GetState();

    EXPECT_FALSE(counter.Restore({0, 4}).IsSuccess());
    EXPECT_TRUE(counter.IsStarted());
    EXPECT_TRUE(counter.GetState() == before);
}

TEST(FrameCounterTest, ReportsExhaustionAtTheEndOfTheCounterSpace) {
    const uint32_t start = FrameCounter::kCounterLimit - 10;
    auto store = std::make_shared<MemoryCounterStore>(start);
    CounterAudit audit(store);
    FrameCounter counter(store);
    ASSERT_TRUE(counter.Start().IsSuccess());
    EXPECT_EQ(10u, counter.GetRemaining());
    EXPECT_FALSE(counter.IsExhausted());

    audit.Draw(counter, 10);
    EXPECT_EQ(FrameCounter::kCounterLimit, store->GetMark());
    EXPECT_EQ(1u, store->GetStoreCount());
    EXPECT_TRUE(counter.IsExhausted());
    EXPECT_EQ(0u, counter.GetRemaining());

    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t value = 4242;
        const Result result = counter.Next(value);
        EXPECT_FALSE(result.IsSuccess());
        EXPECT_EQ(LoraMesherErrorCode::kInvalidState, result.getErrorCode());
        EXPECT_EQ(4242u, value);
    }
    EXPECT_EQ(1u, store->GetStoreCount());
}

TEST(FrameCounterTest, LastValueHandedOutIsBelowTheLimit) {
    auto store =
        std::make_shared<MemoryCounterStore>(FrameCounter::kCounterLimit - 1);
    FrameCounter counter(store, kSmallBlock);
    ASSERT_TRUE(counter.Start().IsSuccess());
    uint32_t value = 0;
    ASSERT_TRUE(counter.Next(value).IsSuccess());
    EXPECT_EQ(FrameCounter::kCounterLimit - 1, value);
    EXPECT_FALSE(counter.Next(value).IsSuccess());
}

TEST(FrameCounterTest, ExhaustedCounterStaysExhaustedAfterRestart) {
    auto store =
        std::make_shared<MemoryCounterStore>(FrameCounter::kCounterLimit);
    FrameCounter counter(store);
    ASSERT_TRUE(counter.Start().IsSuccess());
    EXPECT_TRUE(counter.IsExhausted());
    uint32_t value = 0;
    EXPECT_FALSE(counter.Next(value).IsSuccess());
    EXPECT_EQ(0u, store->GetStoreCount());
}

}  // namespace
}  // namespace security
}  // namespace loramesher
