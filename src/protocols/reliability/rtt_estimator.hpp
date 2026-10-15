/**
 * @file rtt_estimator.hpp
 * @brief Retransmission-timeout estimation from round-trip samples (RFC 6298).
 *
 * Stateless functions operating on a caller-owned PathRtt, so the estimate can
 * be stored wherever per-destination state already lives.
 */

#pragma once

#include <algorithm>
#include <cstdint>

#include "types/protocols/lora_mesh/path_rtt.hpp"

namespace loramesher {
namespace protocols {
namespace reliability {

/**
 * @brief Whether @p rtt holds at least one round-trip sample.
 */
inline bool HasRttSample(const types::protocols::lora_mesh::PathRtt& rtt) {
    return rtt.srtt_ms != 0;
}

/**
 * @brief Fold one round-trip sample into the estimate.
 *
 * The first sample sets SRTT = R and RTTVAR = R / 2; later samples use
 * RTTVAR = 3/4 RTTVAR + 1/4 |SRTT - R| and SRTT = 7/8 SRTT + 1/8 R.
 *
 * @param rtt Estimate to update
 * @param sample_ms Measured round-trip time in milliseconds
 */
inline void AddRttSample(types::protocols::lora_mesh::PathRtt& rtt,
                         uint32_t sample_ms) {
    // SRTT == 0 marks "no sample", so a zero measurement is stored as 1 ms.
    const uint32_t r = std::max<uint32_t>(sample_ms, 1);
    if (!HasRttSample(rtt)) {
        rtt.srtt_ms = r;
        rtt.rttvar_ms = r / 2;
        return;
    }
    const uint32_t deviation =
        rtt.srtt_ms > r ? rtt.srtt_ms - r : r - rtt.srtt_ms;
    rtt.rttvar_ms =
        static_cast<uint32_t>((3ull * rtt.rttvar_ms + deviation) / 4);
    rtt.srtt_ms = static_cast<uint32_t>((7ull * rtt.srtt_ms + r) / 8);
    rtt.srtt_ms = std::max<uint32_t>(rtt.srtt_ms, 1);
}

/**
 * @brief Retransmission timeout for the path described by @p rtt.
 *
 * @param rtt Current estimate
 * @param fallback_ms Timeout to use when no sample has been recorded
 * @param floor_ms Lower bound
 * @param max_ms Upper bound
 * @return uint32_t SRTT + 4 * RTTVAR (or @p fallback_ms), clamped
 */
inline uint32_t ComputeRto(const types::protocols::lora_mesh::PathRtt& rtt,
                           uint32_t fallback_ms, uint32_t floor_ms,
                           uint32_t max_ms) {
    uint64_t rto = HasRttSample(rtt) ? static_cast<uint64_t>(rtt.srtt_ms) +
                                           4ull * rtt.rttvar_ms
                                     : fallback_ms;
    rto = std::max<uint64_t>(rto, floor_ms);
    rto = std::min<uint64_t>(rto, max_ms);
    return static_cast<uint32_t>(rto);
}

}  // namespace reliability
}  // namespace protocols
}  // namespace loramesher
