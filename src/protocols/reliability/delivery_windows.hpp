/**
 * @file delivery_windows.hpp
 * @brief Per-stream de-duplication of reliable message delivery.
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "types/messages/base_header.hpp"
#include "types/storage/resume_snapshot.hpp"
#include "utils/compat/span.hpp"

namespace loramesher {
namespace protocols {
namespace reliability {

/// Sequence stream a reliable message belongs to at its sender.
enum class StreamKind : uint8_t {
    kUnicast,  ///< Messages addressed to this node
    kGroup,    ///< Acknowledged group messages (one stream per sender)
};

/**
 * @brief Sliding-window de-duplication of reliable messages per sender stream
 *
 * A sender numbers the messages of each stream consecutively and keeps every
 * unacknowledged message within kWindow sequence numbers of the newest one,
 * so a receiver sees every sequence number of the stream it is part of. Each
 * stream keeps the highest sequence seen and a bitmap of the kWindow sequence
 * numbers before it:
 *  - a sequence ahead of the highest (by less than half the 8-bit space) is new;
 *  - a sequence inside the window is new unless its bit is set;
 *  - a sequence a full window or more behind cannot be a retransmission and
 *    starts the stream afresh, as does a send timestamp that went back by more
 *    than the restart threshold (the sender rebooted).
 *
 * Fixed capacity; when full, the least recently used stream is replaced.
 */
class DeliveryWindows {
   public:
    static constexpr size_t kCapacity = 32;
    static constexpr uint8_t kWindow = 32;

    /**
     * @brief Record a received message and report whether it is new
     *
     * @param source Sender of the message
     * @param kind Stream the message belongs to
     * @param seq Message sequence within the stream
     * @param send_ts Sender's timestamp of this transmission (ms)
     * @param restart_regression_ms Backward jump of @p send_ts that marks a
     *        sender restart
     * @return true if the message has not been delivered before
     */
    bool Accept(AddressType source, StreamKind kind, uint8_t seq,
                uint32_t send_ts, uint32_t restart_regression_ms);

    /**
     * @brief Get every stream, least recently used first
     */
    std::vector<storage::DeliveryStream> GetStreams() const;

    /**
     * @brief Replace every stream with streams returned by GetStreams()
     *
     * @param streams Streams, least recently used first
     * @return true if restored; false (windows unchanged) if there are more
     *         than kCapacity streams, a stream appears twice or a stream has
     *         not delivered its highest sequence
     */
    bool RestoreStreams(std::span<const storage::DeliveryStream> streams);

   private:
    struct Stream {
        bool valid = false;
        AddressType source = 0;
        StreamKind kind = StreamKind::kUnicast;
        uint8_t highest = 0;
        uint32_t bitmap = 0;     ///< Bit b set: highest - b has been seen
        uint32_t last_ts = 0;    ///< Newest send timestamp seen
        uint32_t last_used = 0;  ///< Value of use_clock_ at the last access
    };

    Stream* Find(AddressType source, StreamKind kind);
    Stream& Allocate();
    static void Start(Stream& stream, uint8_t seq, uint32_t send_ts);

    std::array<Stream, kCapacity> streams_{};
    uint32_t use_clock_ = 0;
};

}  // namespace reliability
}  // namespace protocols
}  // namespace loramesher
