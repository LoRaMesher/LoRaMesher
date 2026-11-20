/**
 * @file frame_counter.cpp
 * @brief Transmit frame counters that never repeat a value across resets
 */

#include "security/frame_counter.hpp"

#include <utility>

namespace loramesher {
namespace security {

namespace {

constexpr size_t kRecoveryIndex = static_cast<size_t>(CounterSpace::kRecovery);

/// log2 of the largest reservation of each space
constexpr uint8_t kMaxBlockLog2[kCounterSpaceCount] = {6, 8, 8, 0};

/// Distance to the ceiling below which a space counts as low
constexpr uint32_t kLowThreshold[kCounterSpaceCount] = {1u << 18, 1u << 24,
                                                        1u << 24, 1u << 24};

}  // namespace

bool FrameCounters::State::operator==(const State& other) const {
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        if (next[i] != other.next[i] || limit[i] != other.limit[i] ||
            reservation_log2[i] != other.reservation_log2[i]) {
            return false;
        }
    }
    return tx_epoch == other.tx_epoch;
}

FrameCounters::FrameCounters(std::shared_ptr<ICounterStore> store)
    : store_(std::move(store)) {}

CounterStatus FrameCounters::Start() {
    started_ = false;
    if (!store_) {
        return CounterStatus::kStoreError;
    }
    const std::optional<CounterRecord> record = store_->Load();
    if (!record || !IsValidCounterRecord(*record)) {
        return CounterStatus::kStoreError;
    }
    volatile_ = false;
    Load(*record);
    return CounterStatus::kOk;
}

CounterStatus FrameCounters::StartVolatile() {
    volatile_ = true;
    Load(CounterRecord{});
    return CounterStatus::kOk;
}

void FrameCounters::Load(const CounterRecord& record) {
    epoch_ = record.tx_epoch;
    for (size_t i = 0; i < kCounterSpaceCount; ++i) {
        next_[i] = record.limit[i];
        limit_[i] = record.limit[i];
        ceiling_[i] =
            i < kTrafficSpaceCount ? record.ceiling[i] : kCounterSpaceNone[i];
        reservation_log2_[i] = kNoReservation;
        refill_pending_[i] = false;
    }
    seal_locked_ = false;
    started_ = true;
}

CounterRecord FrameCounters::MakeRecord() const {
    CounterRecord record;
    record.tx_epoch = epoch_;
    for (size_t i = 0; i < kCounterSpaceCount; ++i) {
        record.limit[i] = limit_[i];
    }
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        record.ceiling[i] = ceiling_[i];
    }
    return record;
}

Result FrameCounters::Save(const CounterRecord& record) {
    if (volatile_) {
        return Result::Success();
    }
    Result result = store_->Save(record);
    if (result) {
        ++store_writes_;
    }
    return result;
}

CounterStatus FrameCounters::Reserve(const bool spaces[kCounterSpaceCount]) {
    CounterRecord record = MakeRecord();
    uint8_t new_log2[kCounterSpaceCount];
    bool any = false;
    for (size_t i = 0; i < kCounterSpaceCount; ++i) {
        if (!spaces[i] || next_[i] >= ceiling_[i]) {
            continue;
        }
        // The k-th reservation since boot covers min(2^(k-1), B) values
        new_log2[i] = reservation_log2_[i] == kNoReservation
                          ? 0
                          : (reservation_log2_[i] < kMaxBlockLog2[i]
                                 ? reservation_log2_[i] + 1
                                 : kMaxBlockLog2[i]);
        const uint32_t block = 1u << new_log2[i];
        const uint32_t new_limit =
            (ceiling_[i] - next_[i] < block) ? ceiling_[i] : next_[i] + block;
        if (new_limit > limit_[i]) {
            record.limit[i] = new_limit;
            any = true;
        }
    }
    if (any && !Save(record)) {
        return CounterStatus::kStoreError;
    }
    for (size_t i = 0; i < kCounterSpaceCount; ++i) {
        if (spaces[i]) {
            refill_pending_[i] = false;
            if (record.limit[i] != limit_[i]) {
                limit_[i] = record.limit[i];
                reservation_log2_[i] = new_log2[i];
            }
        }
    }
    return CounterStatus::kOk;
}

CounterStatus FrameCounters::EnsureReserved(CounterSpace space) {
    if (!started_) {
        return CounterStatus::kNotStarted;
    }
    const size_t index = Index(space);
    if (next_[index] >= ceiling_[index]) {
        return CounterStatus::kExhausted;
    }
    if (next_[index] < limit_[index]) {
        return CounterStatus::kOk;
    }
    if (seal_locked_) {
        return CounterStatus::kSealLocked;
    }
    bool spaces[kCounterSpaceCount] = {false, false, false, false};
    spaces[index] = true;
    return Reserve(spaces);
}

CounterStatus FrameCounters::Next(CounterSpace space, uint32_t& value) {
    if (!started_) {
        return CounterStatus::kNotStarted;
    }
    if (seal_locked_) {
        return CounterStatus::kSealLocked;
    }
    const size_t index = Index(space);
    if (next_[index] >= ceiling_[index]) {
        return CounterStatus::kExhausted;
    }
    if (next_[index] >= limit_[index]) {
        refill_pending_[index] = index != kRecoveryIndex;
        return CounterStatus::kStoreError;
    }
    value = next_[index]++;
    ScheduleRefillIfDue(index);
    return CounterStatus::kOk;
}

void FrameCounters::ScheduleRefillIfDue(size_t index) {
    if (index == kRecoveryIndex || reservation_log2_[index] == kNoReservation ||
        limit_[index] >= ceiling_[index]) {
        return;
    }
    const uint32_t last_block = 1u << reservation_log2_[index];
    if (limit_[index] - next_[index] <= last_block / 4) {
        refill_pending_[index] = true;
    }
}

bool FrameCounters::HasPendingSave() const {
    for (const bool pending : refill_pending_) {
        if (pending) {
            return true;
        }
    }
    return false;
}

CounterStatus FrameCounters::RunPendingSave() {
    if (!started_) {
        return CounterStatus::kNotStarted;
    }
    if (!HasPendingSave()) {
        return CounterStatus::kOk;
    }
    if (seal_locked_) {
        return CounterStatus::kSealLocked;
    }
    return Reserve(refill_pending_);
}

CounterStatus FrameCounters::SwitchEpoch(uint8_t new_epoch) {
    if (!started_) {
        return CounterStatus::kNotStarted;
    }
    if (seal_locked_) {
        return CounterStatus::kSealLocked;
    }
    if (new_epoch <= epoch_ || new_epoch > kMaxEpoch) {
        return CounterStatus::kInvalidArgument;
    }
    CounterRecord record = MakeRecord();
    record.tx_epoch = new_epoch;
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        record.limit[i] = 0;
    }
    if (!Save(record)) {
        return CounterStatus::kStoreError;
    }
    epoch_ = new_epoch;
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        next_[i] = 0;
        limit_[i] = 0;
        reservation_log2_[i] = kNoReservation;
        refill_pending_[i] = false;
    }
    return CounterStatus::kOk;
}

FrameCounters::State FrameCounters::Capture() {
    State state;
    state.tx_epoch = epoch_;
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        state.next[i] = next_[i];
        state.limit[i] = limit_[i];
        state.reservation_log2[i] = reservation_log2_[i];
    }
    seal_locked_ = true;
    return state;
}

CounterStatus FrameCounters::Restore(const State& state) {
    CounterRecord record;
    if (volatile_) {
        record = MakeRecord();
    } else {
        if (!store_) {
            return CounterStatus::kStoreError;
        }
        const std::optional<CounterRecord> stored = store_->Load();
        if (!stored || !IsValidCounterRecord(*stored)) {
            return CounterStatus::kStoreError;
        }
        record = *stored;
    }
    if (state.tx_epoch != record.tx_epoch) {
        return CounterStatus::kInvalidArgument;
    }
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        const bool log2_valid = state.reservation_log2[i] == kNoReservation ||
                                state.reservation_log2[i] <= kMaxBlockLog2[i];
        if (state.limit[i] != record.limit[i] ||
            state.next[i] > state.limit[i] || !log2_valid) {
            return CounterStatus::kInvalidArgument;
        }
    }

    const bool was_volatile = volatile_;
    Load(record);
    volatile_ = was_volatile;
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        next_[i] = state.next[i];
        reservation_log2_[i] = state.reservation_log2[i];
        ScheduleRefillIfDue(i);
    }
    return CounterStatus::kOk;
}

bool FrameCounters::IsLow(CounterSpace space) const {
    const size_t index = Index(space);
    return started_ && ceiling_[index] - next_[index] < kLowThreshold[index];
}

}  // namespace security
}  // namespace loramesher
