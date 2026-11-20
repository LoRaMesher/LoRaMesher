/**
 * @file security_context.hpp
 * @brief Security fields a data frame keeps between opening and forwarding
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace loramesher {
namespace security {

/// Size of a truncated MIC or body tag on the wire, in bytes
constexpr size_t kMicSize = 4;

/**
 * @brief What the body of a data frame holds
 */
enum class BodyState : uint8_t {
    kPlain = 0,   ///< Application bytes, as the origin wrote them
    kCipher = 1,  ///< Ciphertext exactly as it was received
};

/**
 * @brief Body layer fields of a data frame (10 bytes of content)
 *
 * The codec fills it when it opens a data frame. A relay hands it back
 * unchanged when it forwards the frame, so the counter field, the tag and
 * the ciphertext reach the next hop byte for byte. A message built by the
 * application keeps the default value.
 */
struct SecurityContext {
    /// Counter field as on the wire: bit 31 E flag, bit 30 H flag, bits 29
    /// to 0 the data counter of the origin
    uint32_t ctr_field = 0;
    /// Body tag as received
    std::array<uint8_t, kMicSize> tag{};
    /// Key epoch the frame was sealed under
    uint8_t epoch = 0;
    BodyState body_state = BodyState::kPlain;

    bool operator==(const SecurityContext& other) const {
        return ctr_field == other.ctr_field && tag == other.tag &&
               epoch == other.epoch && body_state == other.body_state;
    }
};

}  // namespace security
}  // namespace loramesher
