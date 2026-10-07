/**
 * @file delivery_windows.cpp
 * @brief Implementation of the per-stream delivery de-duplication windows.
 */

#include "delivery_windows.hpp"

namespace loramesher {
namespace protocols {
namespace reliability {

bool DeliveryWindows::Accept(AddressType source, StreamKind kind, uint8_t seq,
                             uint32_t send_ts, uint32_t restart_regression_ms) {
    Stream* stream = Find(source, kind);
    if (stream == nullptr) {
        stream = &Allocate();
        stream->source = source;
        stream->kind = kind;
        Start(*stream, seq, send_ts);
        return true;
    }
    stream->last_used = ++use_clock_;

    const int32_t ts_delta = static_cast<int32_t>(send_ts - stream->last_ts);
    if (ts_delta < -static_cast<int32_t>(restart_regression_ms)) {
        Start(*stream, seq, send_ts);
        return true;
    }
    if (ts_delta > 0) {
        stream->last_ts = send_ts;
    }

    const uint8_t ahead = static_cast<uint8_t>(seq - stream->highest);
    if (ahead == 0) {
        return false;
    }
    if (ahead < 128) {
        stream->bitmap =
            (ahead >= kWindow) ? 1u : (stream->bitmap << ahead) | 1u;
        stream->highest = seq;
        return true;
    }

    const uint8_t behind = static_cast<uint8_t>(stream->highest - seq);
    if (behind >= kWindow) {
        Start(*stream, seq, send_ts);
        return true;
    }
    const uint32_t bit = 1u << behind;
    if ((stream->bitmap & bit) != 0) {
        return false;
    }
    stream->bitmap |= bit;
    return true;
}

DeliveryWindows::Stream* DeliveryWindows::Find(AddressType source,
                                               StreamKind kind) {
    for (auto& stream : streams_) {
        if (stream.valid && stream.source == source && stream.kind == kind) {
            return &stream;
        }
    }
    return nullptr;
}

DeliveryWindows::Stream& DeliveryWindows::Allocate() {
    Stream* oldest = &streams_[0];
    for (auto& stream : streams_) {
        if (!stream.valid) {
            oldest = &stream;
            break;
        }
        if (stream.last_used < oldest->last_used) {
            oldest = &stream;
        }
    }
    oldest->valid = true;
    oldest->last_used = ++use_clock_;
    return *oldest;
}

void DeliveryWindows::Start(Stream& stream, uint8_t seq, uint32_t send_ts) {
    stream.highest = seq;
    stream.bitmap = 1u;
    stream.last_ts = send_ts;
}

}  // namespace reliability
}  // namespace protocols
}  // namespace loramesher
