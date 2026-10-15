/**
 * @file path_rtt.hpp
 * @brief Smoothed round-trip-time state for the path to one destination.
 */

#pragma once

#include <cstdint>

namespace loramesher {
namespace types {
namespace protocols {
namespace lora_mesh {

/**
 * @brief Smoothed round-trip time and its variation toward one destination.
 *
 * Plain data; the estimator that updates it lives in
 * protocols/reliability/rtt_estimator.hpp. Both fields are zero until the
 * first sample is recorded.
 */
struct PathRtt {
    uint32_t srtt_ms = 0;    ///< Smoothed round-trip time (0 = no sample)
    uint32_t rttvar_ms = 0;  ///< Round-trip time variation
};

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace types
}  // namespace loramesher
