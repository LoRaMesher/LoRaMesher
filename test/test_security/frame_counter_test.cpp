/**
 * @file frame_counter_test.cpp
 * @brief Tests of the reserve-ahead transmit frame counters
 */

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <vector>

#include "security/frame_counter.hpp"
#include "security/memory_counter_store.hpp"

namespace loramesher {
namespace security {
namespace {

constexpr CounterSpace kTrafficSpaces[] = {
    CounterSpace::kBeacon, CounterSpace::kControl, CounterSpace::kData};
constexpr CounterSpace kAllSpaces[] = {
    CounterSpace::kBeacon, CounterSpace::kControl, CounterSpace::kData,
    CounterSpace::kRecovery};

size_t Index(CounterSpace space) {
    return static_cast<size_t>(space);
}

/// When the scheduled refill save gets to run
enum class RefillPolicy {
    kImmediate,   ///< Right after the seal that scheduled it
    kLazy,        ///< Only once a seal was refused at the saved limit
    kEveryThird,  ///< After every third seal
};

constexpr RefillPolicy kRefillPolicies[] = {
    RefillPolicy::kImmediate, RefillPolicy::kLazy, RefillPolicy::kEveryThird};

std::shared_ptr<MemoryCounterStore> MakeProvisionedStore(
    const CounterRecord& record = CounterRecord{}) {
    return std::make_shared<MemoryCounterStore>(record);
}

/**
 * @brief Store that loses power in the middle of a write
 *
 * The armed Save() reports a failure. Whether the new record reached the
 * medium is chosen by the test, since a real power loss can go either way.
 */
class TornWriteStore : public MemoryCounterStore {
   public:
    TornWriteStore() : MemoryCounterStore(CounterRecord{}) {}

    void ArmTornWrite(bool record_persists) {
        armed_ = true;
        record_persists_ = record_persists;
    }

    Result Save(const CounterRecord& record) override {
        if (armed_) {
            armed_ = false;
            if (record_persists_) {
                MemoryCounterStore::Save(record);
            }
            return Result(LoraMesherErrorCode::kHardwareError, "Power lost");
        }
        return MemoryCounterStore::Save(record);
    }

   private:
    bool armed_ = false;
    bool record_persists_ = false;
};

/**
 * @brief Seals frames the way the protocol would and audits every value
 *
 * Fails the test when a value repeats within one epoch, goes backwards, or
 * is not below the limit that is durable at the moment it is handed out.
 */
class Sealer {
   public:
    explicit Sealer(std::shared_ptr<MemoryCounterStore> store)
        : store_(std::move(store)) {}

    /// Forget the values of the previous epoch
    void NewEpoch() {
        for (auto& values : seen_) {
            values.clear();
        }
    }

    /**
     * @brief Seal count frames in a space
     *
     * The first need calls EnsureReserved(). A refused seal runs the save
     * and tries again, as a queued frame would.
     */
    void Seal(FrameCounters& counters, CounterSpace space, uint32_t count,
              RefillPolicy policy = RefillPolicy::kImmediate) {
        ASSERT_EQ(CounterStatus::kOk, counters.EnsureReserved(space));
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t value = 0;
            CounterStatus status = counters.Next(space, value);
            if (status == CounterStatus::kStoreError) {
                if (space == CounterSpace::kRecovery) {
                    ASSERT_EQ(CounterStatus::kOk,
                              counters.EnsureReserved(space));
                } else {
                    ASSERT_TRUE(counters.HasPendingSave());
                    ASSERT_EQ(CounterStatus::kOk, counters.RunPendingSave());
                }
                status = counters.Next(space, value);
            }
            ASSERT_EQ(CounterStatus::kOk, status);
            Record(space, value);
            ++sealed_;
            if (policy == RefillPolicy::kImmediate ||
                (policy == RefillPolicy::kEveryThird && sealed_ % 3 == 0)) {
                ASSERT_EQ(CounterStatus::kOk, counters.RunPendingSave());
            }
        }
    }

    void Record(CounterSpace space, uint32_t value) {
        const size_t index = Index(space);
        ASSERT_TRUE(store_->GetRecord().has_value());
        EXPECT_LT(value, store_->GetRecord()->limit[index])
            << "value at or above the durable limit";
        EXPECT_TRUE(seen_[index].insert(value).second)
            << "repeated value " << value;
        EXPECT_EQ(value, *seen_[index].rbegin()) << "value went backwards";
    }

    size_t GetCount(CounterSpace space) const {
        return seen_[Index(space)].size();
    }

   private:
    std::shared_ptr<MemoryCounterStore> store_;
    std::set<uint32_t> seen_[kCounterSpaceCount];
    uint32_t sealed_ = 0;
};

// --- Start and missing storage ---------------------------------------------

TEST(FrameCountersTest, NothingWorksBeforeStart) {
    FrameCounters counters(MakeProvisionedStore());
    uint32_t value = 77;
    EXPECT_EQ(CounterStatus::kNotStarted,
              counters.Next(CounterSpace::kData, value));
    EXPECT_EQ(77u, value);
    EXPECT_EQ(CounterStatus::kNotStarted,
              counters.EnsureReserved(CounterSpace::kData));
    EXPECT_EQ(CounterStatus::kNotStarted, counters.RunPendingSave());
    EXPECT_EQ(CounterStatus::kNotStarted, counters.SwitchEpoch(1));
    EXPECT_FALSE(counters.IsStarted());
}

TEST(FrameCountersTest, StartFailsWithoutStore) {
    FrameCounters counters(nullptr);
    EXPECT_EQ(CounterStatus::kStoreError, counters.Start());
    EXPECT_FALSE(counters.IsStarted());
}

TEST(FrameCountersTest, StartRefusesAnEmptyStore) {
    auto store = std::make_shared<MemoryCounterStore>();
    FrameCounters counters(store);
    EXPECT_EQ(CounterStatus::kStoreError, counters.Start());
    EXPECT_FALSE(counters.IsStarted());
    // The library never creates the record on its own
    EXPECT_EQ(0u, store->GetSaveCount());
    EXPECT_FALSE(store->GetRecord().has_value());
}

TEST(FrameCountersTest, StartRefusesAnUnreadableOrInvalidRecord) {
    auto store = MakeProvisionedStore();
    store->SetLoadFailure(true);
    FrameCounters counters(store);
    EXPECT_EQ(CounterStatus::kStoreError, counters.Start());

    CounterRecord invalid;
    invalid.limit[0] = 0x01000000u;
    FrameCounters other(MakeProvisionedStore(invalid));
    EXPECT_EQ(CounterStatus::kStoreError, other.Start());
}

TEST(FrameCountersTest, BootStartsEachCounterAtItsLimitAndWritesNothing) {
    CounterRecord record;
    record.tx_epoch = 7;
    record.limit[0] = 100;
    record.limit[1] = 2000;
    record.limit[2] = 30000;
    record.limit[3] = 4;
    auto store = MakeProvisionedStore(record);
    FrameCounters counters(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());

    EXPECT_EQ(7u, counters.GetEpoch());
    for (const CounterSpace space : kAllSpaces) {
        EXPECT_EQ(record.limit[Index(space)], counters.GetNext(space));
        EXPECT_EQ(record.limit[Index(space)], counters.GetLimit(space));
    }
    EXPECT_EQ(0u, store->GetSaveCount());
    EXPECT_FALSE(counters.HasPendingSave());
}

TEST(FrameCountersTest, BootThatSendsNothingCostsNothing) {
    CounterRecord record;
    record.limit[1] = 500;
    auto store = MakeProvisionedStore(record);
    for (int boot = 0; boot < 1000; ++boot) {
        FrameCounters counters(store);
        ASSERT_EQ(CounterStatus::kOk, counters.Start());
        ASSERT_EQ(CounterStatus::kOk, counters.RunPendingSave());
    }
    EXPECT_EQ(0u, store->GetSaveCount());
    EXPECT_TRUE(*store->GetRecord() == record);
}

// --- Reservation ramp and boot cost -----------------------------------------

TEST(FrameCountersTest, BlocksStartAtOneAndDoubleUpToTheSpaceMaximum) {
    for (const CounterSpace space : kTrafficSpaces) {
        SCOPED_TRACE(Index(space));
        auto store = MakeProvisionedStore();
        FrameCounters counters(store);
        Sealer sealer(store);
        ASSERT_EQ(CounterStatus::kOk, counters.Start());

        // With a lazy refill every block is used up before the next one,
        // so the limit grows by exactly the block size
        std::vector<uint32_t> blocks;
        uint32_t last_limit = 0;
        for (int i = 0; i < 2000; ++i) {
            sealer.Seal(counters, space, 1, RefillPolicy::kLazy);
            const uint32_t limit = store->GetRecord()->limit[Index(space)];
            if (limit != last_limit) {
                blocks.push_back(limit - last_limit);
                last_limit = limit;
            }
        }
        const uint32_t maximum = FrameCounters::kMaxBlock[Index(space)];
        ASSERT_GT(blocks.size(), 10u);
        uint32_t expected = 1;
        for (const uint32_t block : blocks) {
            EXPECT_EQ(expected, block);
            expected = expected * 2 > maximum ? maximum : expected * 2;
        }
        EXPECT_EQ(maximum, blocks.back());
    }
    EXPECT_EQ(64u, FrameCounters::kMaxBlock[0]);
    EXPECT_EQ(256u, FrameCounters::kMaxBlock[1]);
    EXPECT_EQ(256u, FrameCounters::kMaxBlock[2]);
    EXPECT_EQ(1u, FrameCounters::kMaxBlock[3]);
}

TEST(FrameCountersTest, BootCostMatchesTheSpecExamples) {
    // n frames sealed, then the values consumed: 1 -> 3, 3 -> 7, 48 -> 112
    const uint32_t examples[][2] = {{1, 3}, {3, 7}, {48, 112}};
    for (const auto& example : examples) {
        SCOPED_TRACE(example[0]);
        auto store = MakeProvisionedStore();
        FrameCounters counters(store);
        Sealer sealer(store);
        ASSERT_EQ(CounterStatus::kOk, counters.Start());
        sealer.Seal(counters, CounterSpace::kBeacon, example[0]);
        EXPECT_EQ(example[1], store->GetRecord()->limit[0]);
    }
}

TEST(FrameCountersTest, BootConsumesAtMostThreeTimesItsFramesPlusOne) {
    for (const CounterSpace space : kTrafficSpaces) {
        for (const RefillPolicy policy : kRefillPolicies) {
            SCOPED_TRACE(Index(space));
            SCOPED_TRACE(static_cast<int>(policy));
            auto store = MakeProvisionedStore();
            FrameCounters counters(store);
            Sealer sealer(store);
            ASSERT_EQ(CounterStatus::kOk, counters.Start());
            for (uint32_t n = 1; n <= 1500; ++n) {
                sealer.Seal(counters, space, 1, policy);
                // A reset now would continue at the saved limit
                const uint32_t consumed =
                    store->GetRecord()->limit[Index(space)];
                ASSERT_LE(consumed, 3 * n + 1) << n;
                ASSERT_GE(consumed, n) << n;
            }
        }
    }
}

TEST(FrameCountersTest, BootCostBoundHoldsOverManyBoots) {
    for (const RefillPolicy policy : kRefillPolicies) {
        SCOPED_TRACE(static_cast<int>(policy));
        auto store = MakeProvisionedStore();
        Sealer sealer(store);
        uint64_t bound = 0;
        uint32_t random = 0x2545F491u;
        for (int boot = 0; boot < 400; ++boot) {
            FrameCounters counters(store);
            ASSERT_EQ(CounterStatus::kOk, counters.Start());
            for (const CounterSpace space : kTrafficSpaces) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                const uint32_t frames = (random >> 8) % 40;
                if (frames == 0) {
                    continue;  // trigger never reached in this boot
                }
                sealer.Seal(counters, space, frames, policy);
                if (space == CounterSpace::kData) {
                    bound += 3 * frames + 1;
                }
            }
        }
        EXPECT_LE(store->GetRecord()->limit[2], bound);
        EXPECT_GE(store->GetRecord()->limit[2],
                  sealer.GetCount(CounterSpace::kData));
    }
}

TEST(FrameCountersTest, OneFrameThenResetCostsThreeValuesAndTwoWrites) {
    for (const CounterSpace space : kTrafficSpaces) {
        SCOPED_TRACE(Index(space));
        auto store = MakeProvisionedStore();
        Sealer sealer(store);
        for (uint32_t boot = 1; boot <= 50; ++boot) {
            FrameCounters counters(store);
            ASSERT_EQ(CounterStatus::kOk, counters.Start());
            sealer.Seal(counters, space, 1);
            EXPECT_EQ(3 * boot, store->GetRecord()->limit[Index(space)]);
            EXPECT_EQ(2 * boot, store->GetSaveCount());
        }
    }
}

TEST(FrameCountersTest, RecoverySpaceCostsOneValueAndOneWritePerRequest) {
    auto store = MakeProvisionedStore();
    Sealer sealer(store);
    for (uint32_t boot = 1; boot <= 20; ++boot) {
        FrameCounters counters(store);
        ASSERT_EQ(CounterStatus::kOk, counters.Start());
        sealer.Seal(counters, CounterSpace::kRecovery, 1);
        EXPECT_FALSE(counters.HasPendingSave());
        EXPECT_EQ(boot, store->GetRecord()->limit[3]);
        EXPECT_EQ(boot, store->GetSaveCount());
    }

    // Several requests in one boot stay at one value per write
    FrameCounters counters(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    sealer.Seal(counters, CounterSpace::kRecovery, 5);
    EXPECT_EQ(25u, store->GetRecord()->limit[3]);
    EXPECT_EQ(25u, store->GetSaveCount());
}

TEST(FrameCountersTest, SteadyStateWritesOncePerThreeQuartersOfABlock) {
    auto store = MakeProvisionedStore();
    FrameCounters counters(store);
    Sealer sealer(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());

    sealer.Seal(counters, CounterSpace::kBeacon, 1000);
    uint32_t writes = store->GetSaveCount();
    sealer.Seal(counters, CounterSpace::kBeacon, 48 * 10);
    EXPECT_EQ(writes + 10, store->GetSaveCount());

    sealer.Seal(counters, CounterSpace::kControl, 3000);
    writes = store->GetSaveCount();
    sealer.Seal(counters, CounterSpace::kControl, 192 * 10);
    EXPECT_EQ(writes + 10, store->GetSaveCount());
}

// --- Hard stop and store failures ------------------------------------------

TEST(FrameCountersTest, NextNeverTouchesTheStore) {
    auto store = MakeProvisionedStore();
    FrameCounters counters(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    const uint32_t loads = store->GetLoadCount();

    // No reservation yet: the seal is refused and a save is scheduled
    uint32_t value = 99;
    EXPECT_EQ(CounterStatus::kStoreError,
              counters.Next(CounterSpace::kControl, value));
    EXPECT_EQ(99u, value);
    EXPECT_TRUE(counters.HasPendingSave());
    EXPECT_EQ(0u, store->GetSaveCount());

    ASSERT_EQ(CounterStatus::kOk, counters.RunPendingSave());
    ASSERT_EQ(CounterStatus::kOk, counters.Next(CounterSpace::kControl, value));
    EXPECT_EQ(0u, value);
    EXPECT_EQ(1u, store->GetSaveCount());
    EXPECT_EQ(loads, store->GetLoadCount());
}

TEST(FrameCountersTest, SealIsRefusedAtTheSavedLimitUntilTheRefillRuns) {
    auto store = MakeProvisionedStore();
    FrameCounters counters(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    ASSERT_EQ(CounterStatus::kOk, counters.EnsureReserved(CounterSpace::kData));

    uint32_t value = 0;
    ASSERT_EQ(CounterStatus::kOk, counters.Next(CounterSpace::kData, value));
    EXPECT_TRUE(counters.HasPendingSave());
    for (int attempt = 0; attempt < 3; ++attempt) {
        value = 1234;
        EXPECT_EQ(CounterStatus::kStoreError,
                  counters.Next(CounterSpace::kData, value));
        EXPECT_EQ(1234u, value);
    }
    EXPECT_EQ(1u, counters.GetNext(CounterSpace::kData));

    ASSERT_EQ(CounterStatus::kOk, counters.RunPendingSave());
    ASSERT_EQ(CounterStatus::kOk, counters.Next(CounterSpace::kData, value));
    EXPECT_EQ(1u, value);
}

TEST(FrameCountersTest, FailedSaveLeavesTheOldLimitInForce) {
    auto store = MakeProvisionedStore();
    FrameCounters counters(store);
    Sealer sealer(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    sealer.Seal(counters, CounterSpace::kControl, 100);
    const CounterRecord before = *store->GetRecord();
    const uint32_t limit = counters.GetLimit(CounterSpace::kControl);

    store->SetSaveFailure(true);
    uint32_t value = 0;
    uint32_t handed_out = 0;
    while (counters.Next(CounterSpace::kControl, value) == CounterStatus::kOk) {
        sealer.Record(CounterSpace::kControl, value);
        ++handed_out;
        if (counters.HasPendingSave()) {
            EXPECT_EQ(CounterStatus::kStoreError, counters.RunPendingSave());
        }
        ASSERT_LT(handed_out, 1000u);
    }
    EXPECT_EQ(limit, counters.GetNext(CounterSpace::kControl));
    EXPECT_EQ(limit, counters.GetLimit(CounterSpace::kControl));
    EXPECT_TRUE(*store->GetRecord() == before);
    EXPECT_TRUE(counters.HasPendingSave());
    EXPECT_EQ(CounterStatus::kStoreError,
              counters.Next(CounterSpace::kControl, value));

    // The counter continues without a gap once the store works again
    store->SetSaveFailure(false);
    ASSERT_EQ(CounterStatus::kOk, counters.RunPendingSave());
    ASSERT_EQ(CounterStatus::kOk, counters.Next(CounterSpace::kControl, value));
    EXPECT_EQ(limit, value);
    sealer.Record(CounterSpace::kControl, value);
}

TEST(FrameCountersTest, FailedFirstReservationHandsOutNothing) {
    auto store = MakeProvisionedStore();
    store->SetSaveFailure(true);
    FrameCounters counters(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    EXPECT_EQ(CounterStatus::kStoreError,
              counters.EnsureReserved(CounterSpace::kBeacon));
    uint32_t value = 0;
    EXPECT_EQ(CounterStatus::kStoreError,
              counters.Next(CounterSpace::kBeacon, value));
    EXPECT_EQ(0u, store->GetRecord()->limit[0]);
}

// --- Crashes -----------------------------------------------------------------

TEST(FrameCountersTest, RestartAtEveryPointOfTheCycleNeverRepeats) {
    // The crash point sweeps several reserve cycles: before the first
    // reservation, right after one, inside a block and on its last value,
    // with the refill save done or still outstanding
    for (const CounterSpace space : kAllSpaces) {
        for (const RefillPolicy policy : kRefillPolicies) {
            for (uint32_t crash_after = 0; crash_after <= 150; ++crash_after) {
                SCOPED_TRACE(Index(space));
                SCOPED_TRACE(static_cast<int>(policy));
                SCOPED_TRACE(crash_after);
                auto store = MakeProvisionedStore();
                Sealer sealer(store);
                {
                    FrameCounters counters(store);
                    ASSERT_EQ(CounterStatus::kOk, counters.Start());
                    if (crash_after > 0) {
                        sealer.Seal(counters, space, crash_after, policy);
                    }
                }
                const uint32_t limit_at_crash =
                    store->GetRecord()->limit[Index(space)];

                FrameCounters restarted(store);
                ASSERT_EQ(CounterStatus::kOk, restarted.Start());
                EXPECT_EQ(limit_at_crash, restarted.GetNext(space));
                sealer.Seal(restarted, space, 80, policy);
                ASSERT_EQ(crash_after + 80, sealer.GetCount(space));
            }
        }
    }
}

TEST(FrameCountersTest, RepeatedRestartsNeverRepeat) {
    auto store = MakeProvisionedStore();
    Sealer sealer(store);
    for (uint32_t boot = 0; boot < 300; ++boot) {
        FrameCounters counters(store);
        ASSERT_EQ(CounterStatus::kOk, counters.Start());
        for (const CounterSpace space : kAllSpaces) {
            const uint32_t frames = (boot * 7 + Index(space) * 5) % 23;
            if (frames > 0) {
                sealer.Seal(counters, space, frames, kRefillPolicies[boot % 3]);
            }
        }
    }
}

TEST(FrameCountersTest, PowerLossDuringASaveNeverRepeats) {
    for (const bool record_persists : {false, true}) {
        for (uint32_t before = 0; before <= 40; ++before) {
            SCOPED_TRACE(record_persists);
            SCOPED_TRACE(before);
            auto store = std::make_shared<TornWriteStore>();
            Sealer sealer(store);
            {
                FrameCounters counters(store);
                ASSERT_EQ(CounterStatus::kOk, counters.Start());
                if (before > 0) {
                    sealer.Seal(counters, CounterSpace::kData, before,
                                RefillPolicy::kLazy);
                }
                // Use what is left, then the next save is torn
                uint32_t value = 0;
                while (counters.Next(CounterSpace::kData, value) ==
                       CounterStatus::kOk) {
                    sealer.Record(CounterSpace::kData, value);
                }
                store->ArmTornWrite(record_persists);
                EXPECT_EQ(
                    CounterStatus::kStoreError,
                    before == 0 && sealer.GetCount(CounterSpace::kData) == 0
                        ? counters.EnsureReserved(CounterSpace::kData)
                        : counters.RunPendingSave());
                EXPECT_EQ(CounterStatus::kStoreError,
                          counters.Next(CounterSpace::kData, value));
            }
            FrameCounters restarted(store);
            ASSERT_EQ(CounterStatus::kOk, restarted.Start());
            sealer.Seal(restarted, CounterSpace::kData, 60);
        }
    }
}

TEST(FrameCountersTest, CountersSurviveATornSaveWithoutRestart) {
    for (const bool record_persists : {false, true}) {
        SCOPED_TRACE(record_persists);
        auto store = std::make_shared<TornWriteStore>();
        FrameCounters counters(store);
        Sealer sealer(store);
        ASSERT_EQ(CounterStatus::kOk, counters.Start());
        sealer.Seal(counters, CounterSpace::kData, 20, RefillPolicy::kLazy);

        uint32_t value = 0;
        while (counters.Next(CounterSpace::kData, value) ==
               CounterStatus::kOk) {
            sealer.Record(CounterSpace::kData, value);
        }
        store->ArmTornWrite(record_persists);
        EXPECT_EQ(CounterStatus::kStoreError, counters.RunPendingSave());
        sealer.Seal(counters, CounterSpace::kData, 100);
    }
}

// --- Ceiling and exhaustion ---------------------------------------------------

TEST(FrameCountersTest, SpacesEndBelowTheirAllOnesValue) {
    const uint32_t all_ones[] = {0x00FFFFFFu, 0xFFFFFFFFu, 0x3FFFFFFFu,
                                 0xFFFFFFFFu};
    for (const CounterSpace space : kAllSpaces) {
        SCOPED_TRACE(Index(space));
        const size_t index = Index(space);
        CounterRecord record;
        record.limit[index] = all_ones[index] - 10;
        auto store = MakeProvisionedStore(record);
        FrameCounters counters(store);
        Sealer sealer(store);
        ASSERT_EQ(CounterStatus::kOk, counters.Start());
        EXPECT_EQ(all_ones[index], counters.GetCeiling(space));
        EXPECT_TRUE(counters.IsLow(space));
        EXPECT_FALSE(counters.IsExhausted(space));

        sealer.Seal(counters, space, 10);
        EXPECT_EQ(all_ones[index], store->GetRecord()->limit[index]);
        EXPECT_TRUE(counters.IsExhausted(space));
        EXPECT_FALSE(counters.HasPendingSave());

        const uint32_t writes = store->GetSaveCount();
        uint32_t value = 4242;
        EXPECT_EQ(CounterStatus::kExhausted, counters.Next(space, value));
        EXPECT_EQ(4242u, value);
        EXPECT_EQ(CounterStatus::kExhausted, counters.EnsureReserved(space));
        EXPECT_EQ(writes, store->GetSaveCount());

        // Still exhausted after a restart
        FrameCounters restarted(store);
        ASSERT_EQ(CounterStatus::kOk, restarted.Start());
        EXPECT_TRUE(restarted.IsExhausted(space));
        EXPECT_EQ(CounterStatus::kExhausted, restarted.Next(space, value));
    }
}

TEST(FrameCountersTest, ReservationStopsAtAConfiguredCeiling) {
    CounterRecord record;
    record.limit[2] = 1000;
    record.ceiling[2] = 1040;
    auto store = MakeProvisionedStore(record);
    FrameCounters counters(store);
    Sealer sealer(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    sealer.Seal(counters, CounterSpace::kData, 40);
    EXPECT_EQ(1040u, store->GetRecord()->limit[2]);
    EXPECT_EQ(1040u, store->GetRecord()->ceiling[2]);
    uint32_t value = 0;
    EXPECT_EQ(CounterStatus::kExhausted,
              counters.Next(CounterSpace::kData, value));
}

TEST(FrameCountersTest, LowThresholdsFollowTheSpaceWidth) {
    CounterRecord record;
    record.limit[0] = 0x00FFFFFFu - (1u << 18);
    record.limit[1] = 0xFFFFFFFFu - (1u << 24);
    record.limit[2] = 0x3FFFFFFFu - (1u << 24);
    record.limit[3] = 0xFFFFFFFFu - (1u << 24);
    auto store = MakeProvisionedStore(record);
    FrameCounters counters(store);
    Sealer sealer(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    for (const CounterSpace space : kAllSpaces) {
        SCOPED_TRACE(Index(space));
        EXPECT_FALSE(counters.IsLow(space));
        sealer.Seal(counters, space, 1);
        EXPECT_TRUE(counters.IsLow(space));
    }
}

// --- Epoch switch --------------------------------------------------------------

TEST(FrameCountersTest, EpochSwitchRestartsTrafficCountersAndKeepsTheRest) {
    CounterRecord record;
    record.tx_epoch = 4;
    record.ceiling[0] = 0x00800000u;
    auto store = MakeProvisionedStore(record);
    FrameCounters counters(store);
    Sealer sealer(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    for (const CounterSpace space : kAllSpaces) {
        sealer.Seal(counters, space, 30);
    }
    const uint32_t recovery_limit = store->GetRecord()->limit[3];
    const uint32_t writes = store->GetSaveCount();

    ASSERT_EQ(CounterStatus::kOk, counters.SwitchEpoch(5));
    EXPECT_EQ(writes + 1, store->GetSaveCount());
    EXPECT_EQ(5u, counters.GetEpoch());
    const CounterRecord saved = *store->GetRecord();
    EXPECT_EQ(5u, saved.tx_epoch);
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        EXPECT_EQ(0u, saved.limit[i]);
        EXPECT_EQ(0u, counters.GetNext(kTrafficSpaces[i]));
    }
    EXPECT_EQ(0x00800000u, saved.ceiling[0]);
    EXPECT_EQ(recovery_limit, saved.limit[3]);
    EXPECT_EQ(recovery_limit, counters.GetNext(CounterSpace::kRecovery));
    EXPECT_FALSE(counters.HasPendingSave());

    // The ramp restarts at a block of 1 and values start at 0 again
    sealer.NewEpoch();
    uint32_t value = 0;
    EXPECT_EQ(CounterStatus::kStoreError,
              counters.Next(CounterSpace::kData, value));
    ASSERT_EQ(CounterStatus::kOk, counters.EnsureReserved(CounterSpace::kData));
    EXPECT_EQ(1u, store->GetRecord()->limit[2]);
    ASSERT_EQ(CounterStatus::kOk, counters.Next(CounterSpace::kData, value));
    EXPECT_EQ(0u, value);

    // A restart continues in the new epoch
    FrameCounters restarted(store);
    ASSERT_EQ(CounterStatus::kOk, restarted.Start());
    EXPECT_EQ(5u, restarted.GetEpoch());
}

TEST(FrameCountersTest, EpochNeverGoesBackOrBeyondTheLastOne) {
    CounterRecord record;
    record.tx_epoch = 9;
    auto store = MakeProvisionedStore(record);
    FrameCounters counters(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    EXPECT_EQ(CounterStatus::kInvalidArgument, counters.SwitchEpoch(8));
    EXPECT_EQ(CounterStatus::kInvalidArgument, counters.SwitchEpoch(9));
    EXPECT_EQ(CounterStatus::kInvalidArgument, counters.SwitchEpoch(255));
    EXPECT_EQ(0u, store->GetSaveCount());
    EXPECT_EQ(9u, counters.GetEpoch());
    // Epochs may be skipped upwards, up to the last one
    EXPECT_EQ(CounterStatus::kOk, counters.SwitchEpoch(12));
    EXPECT_EQ(CounterStatus::kOk, counters.SwitchEpoch(254));
    EXPECT_EQ(CounterStatus::kInvalidArgument, counters.SwitchEpoch(254));
}

TEST(FrameCountersTest, FailedEpochSwitchChangesNothing) {
    auto store = MakeProvisionedStore();
    FrameCounters counters(store);
    Sealer sealer(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    sealer.Seal(counters, CounterSpace::kControl, 10);
    const CounterRecord before = *store->GetRecord();
    const uint32_t next = counters.GetNext(CounterSpace::kControl);

    store->SetSaveFailure(true);
    EXPECT_EQ(CounterStatus::kStoreError, counters.SwitchEpoch(1));
    EXPECT_EQ(0u, counters.GetEpoch());
    EXPECT_EQ(next, counters.GetNext(CounterSpace::kControl));
    EXPECT_TRUE(*store->GetRecord() == before);
}

// --- Deep sleep snapshot ---------------------------------------------------------

TEST(FrameCountersTest, SnapshotRoundTripWastesNothing) {
    auto store = MakeProvisionedStore();
    Sealer sealer(store);
    FrameCounters::State snapshot;
    {
        FrameCounters counters(store);
        ASSERT_EQ(CounterStatus::kOk, counters.Start());
        sealer.Seal(counters, CounterSpace::kBeacon, 5);
        sealer.Seal(counters, CounterSpace::kControl, 9);
        sealer.Seal(counters, CounterSpace::kRecovery, 2);
        snapshot = counters.Capture();
    }
    EXPECT_EQ(5u, snapshot.next[0]);
    EXPECT_EQ(7u, snapshot.limit[0]);
    EXPECT_EQ(2u, snapshot.reservation_log2[0]);
    EXPECT_EQ(9u, snapshot.next[1]);
    EXPECT_EQ(14u, snapshot.limit[1]);
    EXPECT_EQ(3u, snapshot.reservation_log2[1]);
    EXPECT_EQ(FrameCounters::kNoReservation, snapshot.reservation_log2[2]);
    const uint32_t writes = store->GetSaveCount();

    FrameCounters resumed(store);
    ASSERT_EQ(CounterStatus::kOk, resumed.Restore(snapshot));
    EXPECT_TRUE(resumed.IsStarted());
    EXPECT_EQ(writes, store->GetSaveCount());
    EXPECT_EQ(2u, resumed.GetNext(CounterSpace::kRecovery));
    EXPECT_FALSE(resumed.HasPendingSave());

    // The reserved values are used and the ramp continues with a block of 8
    uint32_t value = 0;
    ASSERT_EQ(CounterStatus::kOk, resumed.Next(CounterSpace::kBeacon, value));
    EXPECT_EQ(5u, value);
    sealer.Record(CounterSpace::kBeacon, value);
    EXPECT_EQ(writes, store->GetSaveCount());
    ASSERT_TRUE(resumed.HasPendingSave());
    ASSERT_EQ(CounterStatus::kOk, resumed.RunPendingSave());
    EXPECT_EQ(writes + 1, store->GetSaveCount());
    EXPECT_EQ(6u + 8u, store->GetRecord()->limit[0]);
    sealer.Seal(resumed, CounterSpace::kControl, 3);
    EXPECT_EQ(12u + 16u, store->GetRecord()->limit[1]);
}

TEST(FrameCountersTest, RestoreRecomputesAnOutstandingRefill) {
    auto store = MakeProvisionedStore();
    Sealer sealer(store);
    FrameCounters counters(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    sealer.Seal(counters, CounterSpace::kData, 5);
    sealer.Seal(counters, CounterSpace::kData, 1, RefillPolicy::kLazy);
    ASSERT_TRUE(counters.HasPendingSave());
    const FrameCounters::State snapshot = counters.Capture();

    FrameCounters resumed(store);
    ASSERT_EQ(CounterStatus::kOk, resumed.Restore(snapshot));
    EXPECT_TRUE(resumed.HasPendingSave());
    sealer.Seal(resumed, CounterSpace::kData, 20);
}

TEST(FrameCountersTest, CaptureLocksSealingUntilResumeOrDiscard) {
    auto store = MakeProvisionedStore();
    FrameCounters counters(store);
    Sealer sealer(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    sealer.Seal(counters, CounterSpace::kData, 5);
    sealer.Seal(counters, CounterSpace::kData, 1, RefillPolicy::kLazy);
    ASSERT_TRUE(counters.HasPendingSave());
    const FrameCounters::State snapshot = counters.Capture();

    uint32_t value = 55;
    EXPECT_EQ(CounterStatus::kSealLocked,
              counters.Next(CounterSpace::kData, value));
    EXPECT_EQ(CounterStatus::kSealLocked,
              counters.Next(CounterSpace::kRecovery, value));
    EXPECT_EQ(55u, value);
    EXPECT_TRUE(counters.Capture() == snapshot);

    // No save may move the record away from the snapshot either
    const uint32_t writes = store->GetSaveCount();
    EXPECT_EQ(CounterStatus::kSealLocked,
              counters.EnsureReserved(CounterSpace::kBeacon));
    EXPECT_EQ(CounterStatus::kSealLocked, counters.RunPendingSave());
    EXPECT_EQ(CounterStatus::kSealLocked, counters.SwitchEpoch(1));
    EXPECT_EQ(writes, store->GetSaveCount());

    // Sleep did not happen and the snapshot was thrown away
    counters.DiscardCapture();
    sealer.Seal(counters, CounterSpace::kData, 1);

    // After a resume sealing works again
    const FrameCounters::State second = counters.Capture();
    ASSERT_EQ(CounterStatus::kOk, counters.Restore(second));
    sealer.Seal(counters, CounterSpace::kData, 1);
}

TEST(FrameCountersTest, RestoreRejectsASnapshotThatDoesNotMatchTheStore) {
    auto store = MakeProvisionedStore();
    Sealer sealer(store);
    FrameCounters counters(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    sealer.Seal(counters, CounterSpace::kData, 3);
    const FrameCounters::State stale = counters.Capture();
    counters.DiscardCapture();
    sealer.Seal(counters, CounterSpace::kData, 30);
    const FrameCounters::State fresh = counters.Capture();

    // Older than the store
    FrameCounters resumed(store);
    EXPECT_EQ(CounterStatus::kInvalidArgument, resumed.Restore(stale));
    EXPECT_FALSE(resumed.IsStarted());

    // Position beyond its own limit
    FrameCounters::State beyond = fresh;
    beyond.next[2] = beyond.limit[2] + 1;
    EXPECT_EQ(CounterStatus::kInvalidArgument, resumed.Restore(beyond));

    // Reservation size no ramp can produce
    FrameCounters::State bad_block = fresh;
    bad_block.reservation_log2[0] = 7;
    EXPECT_EQ(CounterStatus::kInvalidArgument, resumed.Restore(bad_block));

    // Another epoch
    FrameCounters::State other_epoch = fresh;
    other_epoch.tx_epoch = 1;
    EXPECT_EQ(CounterStatus::kInvalidArgument, resumed.Restore(other_epoch));

    // Unreadable store
    store->SetLoadFailure(true);
    EXPECT_EQ(CounterStatus::kStoreError, resumed.Restore(fresh));
    store->SetLoadFailure(false);
    EXPECT_FALSE(resumed.IsStarted());

    // The fallback is a cold start, which skips the open block
    ASSERT_EQ(CounterStatus::kOk, resumed.Start());
    sealer.Seal(resumed, CounterSpace::kData, 10);
}

TEST(FrameCountersTest, SnapshotOfAnOlderEpochIsRejectedAfterASwitch) {
    auto store = MakeProvisionedStore();
    FrameCounters counters(store);
    ASSERT_EQ(CounterStatus::kOk, counters.Start());
    const FrameCounters::State before_switch = counters.Capture();
    counters.DiscardCapture();
    ASSERT_EQ(CounterStatus::kOk, counters.SwitchEpoch(1));

    FrameCounters resumed(store);
    EXPECT_EQ(CounterStatus::kInvalidArgument, resumed.Restore(before_switch));
}

// --- Volatile mode ------------------------------------------------------------------

TEST(FrameCountersTest, VolatileCountersStartAtZeroWithoutAStore) {
    FrameCounters counters(nullptr);
    ASSERT_EQ(CounterStatus::kOk, counters.StartVolatile());
    EXPECT_EQ(0u, counters.GetEpoch());
    for (const CounterSpace space : kAllSpaces) {
        ASSERT_EQ(CounterStatus::kOk, counters.EnsureReserved(space));
        for (uint32_t expected = 0; expected < 600; ++expected) {
            uint32_t value = 0;
            if (counters.Next(space, value) != CounterStatus::kOk) {
                ASSERT_EQ(CounterStatus::kOk, counters.EnsureReserved(space));
                ASSERT_EQ(CounterStatus::kOk, counters.Next(space, value));
            }
            ASSERT_EQ(expected, value);
            ASSERT_EQ(CounterStatus::kOk, counters.RunPendingSave());
        }
    }
    EXPECT_EQ(0u, counters.GetStoreWrites());
    EXPECT_EQ(CounterStatus::kOk, counters.SwitchEpoch(1));
    EXPECT_EQ(0u, counters.GetNext(CounterSpace::kData));
}

}  // namespace
}  // namespace security
}  // namespace loramesher
