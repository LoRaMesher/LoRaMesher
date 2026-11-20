/**
 * @file frame_counter.cpp
 * @brief Transmit frame counter that never repeats a value across resets
 */

#include "security/frame_counter.hpp"

#include <utility>

namespace loramesher {
namespace security {

FrameCounter::FrameCounter(std::shared_ptr<ICounterStore> store,
                           uint32_t block_size)
    : store_(std::move(store)), block_size_(block_size == 0 ? 1 : block_size) {}

Result FrameCounter::Start() {
    started_ = false;
    if (!store_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "No counter store configured");
    }
    uint32_t mark = 0;
    Result result = store_->Load(mark);
    if (!result) {
        return result;
    }
    next_ = mark;
    reserved_end_ = mark;
    started_ = true;
    return Result::Success();
}

Result FrameCounter::Next(uint32_t& counter) {
    if (!started_) {
        return Result(LoraMesherErrorCode::kNotInitialized,
                      "Frame counter not started");
    }
    if (next_ == kCounterLimit) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Frame counter exhausted");
    }
    if (next_ == reserved_end_) {
        const uint32_t new_end = (kCounterLimit - reserved_end_ < block_size_)
                                     ? kCounterLimit
                                     : reserved_end_ + block_size_;
        Result result = store_->Store(new_end);
        if (!result) {
            return result;
        }
        reserved_end_ = new_end;
    }
    counter = next_++;
    return Result::Success();
}

Result FrameCounter::Restore(const State& state) {
    if (!store_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "No counter store configured");
    }
    uint32_t mark = 0;
    Result result = store_->Load(mark);
    if (!result) {
        return result;
    }
    if (state.reserved_end != mark || state.next > state.reserved_end) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Frame counter snapshot does not match the store");
    }
    next_ = state.next;
    reserved_end_ = state.reserved_end;
    started_ = true;
    return Result::Success();
}

}  // namespace security
}  // namespace loramesher
