/**
 * @file delivery_windows.cpp
 * @brief Implementation of the per-stream delivery de-duplication windows.
 */

#include "delivery_windows.hpp"

#include <algorithm>

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

std::vector<storage::DeliveryStream> DeliveryWindows::GetStreams() const {
    std::vector<const Stream*> used;
    for (const auto& stream : streams_) {
        if (stream.valid) {
            used.push_back(&stream);
        }
    }
    std::sort(used.begin(), used.end(), [](const Stream* a, const Stream* b) {
        return a->last_used < b->last_used;
    });

    std::vector<storage::DeliveryStream> result;
    result.reserve(used.size());
    for (const Stream* stream : used) {
        result.push_back({stream->source, stream->kind == StreamKind::kGroup,
                          stream->highest, stream->bitmap, stream->last_ts});
    }
    return result;
}

bool DeliveryWindows::RestoreStreams(
    std::span<const storage::DeliveryStream> streams) {
    if (streams.size() > kCapacity) {
        return false;
    }
    for (size_t i = 0; i < streams.size(); ++i) {
        if ((streams[i].bitmap & 1u) == 0) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (streams[j].source == streams[i].source &&
                streams[j].group == streams[i].group) {
                return false;
            }
        }
    }

    streams_.fill(Stream{});
    use_clock_ = 0;
    for (size_t i = 0; i < streams.size(); ++i) {
        Stream& stream = streams_[i];
        stream.valid = true;
        stream.source = streams[i].source;
        stream.kind =
            streams[i].group ? StreamKind::kGroup : StreamKind::kUnicast;
        stream.highest = streams[i].highest;
        stream.bitmap = streams[i].bitmap;
        stream.last_ts = streams[i].last_send_ts;
        stream.last_used = ++use_clock_;
    }
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
