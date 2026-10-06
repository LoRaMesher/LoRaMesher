/**
 * @file lora_airtime_test.cpp
 * @brief Tests for the Semtech time-on-air reference helper.
 *
 * These lock in the LDRO-correct airtime values that the TDMA slot sizing
 * depends on. The values mirror the SX127x/SX126x datasheet formula and are
 * independent of any radio driver, so they catch a driver that reports a
 * Low Data Rate Optimization state inconsistent with the configured
 * spreading factor and bandwidth.
 */

#include <gtest/gtest.h>

#include "utils/lora_airtime.hpp"

#ifdef ARDUINO

TEST(LoRaAirtimeTest, SkipOnArduino) {
    GTEST_SKIP();
}

#else

namespace loramesher {
namespace airtime {
namespace {

// SX1278 / SX1276 defaults used throughout: CR 4/7, 8-symbol preamble,
// CRC on, explicit header.
constexpr uint8_t kCodingRateDenom = 7;
constexpr uint16_t kPreamble = 8;
constexpr bool kCrc = true;
constexpr bool kExplicitHeader = true;

uint32_t Toa(uint8_t sf, float bw_khz, uint16_t bytes, bool ldro) {
    return CalculateTimeOnAirMs(sf, bw_khz, kCodingRateDenom, bytes, kPreamble,
                                kCrc, kExplicitHeader, ldro);
}

// ---------------------------------------------------------------------------
// LDRO auto decision: matches the SF/BW table observed on hardware.
// ---------------------------------------------------------------------------

TEST(LoRaAirtimeTest, LdroEnabledOnlyWhenSymbolTimeAtLeast16ms) {
    // Symbol time >= 16 ms -> LDRO on.
    EXPECT_TRUE(ShouldEnableLdro(12, 125.0f));  // 32.768 ms
    EXPECT_TRUE(ShouldEnableLdro(12, 250.0f));  // 16.384 ms
    EXPECT_TRUE(ShouldEnableLdro(11, 125.0f));  // 16.384 ms

    // Symbol time < 16 ms -> LDRO off.
    EXPECT_FALSE(ShouldEnableLdro(12, 500.0f));  // 8.192 ms
    EXPECT_FALSE(ShouldEnableLdro(11, 250.0f));  // 8.192 ms
    EXPECT_FALSE(ShouldEnableLdro(10, 125.0f));  // 8.192 ms
    EXPECT_FALSE(ShouldEnableLdro(9, 125.0f));   // 4.096 ms
    EXPECT_FALSE(ShouldEnableLdro(7, 125.0f));   // 1.024 ms
}

// ---------------------------------------------------------------------------
// Known-good airtime values at SF12/BW125 (LDRO on).
// ---------------------------------------------------------------------------

TEST(LoRaAirtimeTest, KnownValuesSf12Bw125WithLdro) {
    EXPECT_NEAR(Toa(12, 125.0f, 24, /*ldro=*/true), 1810u, 3u);
    EXPECT_NEAR(Toa(12, 125.0f, 48, /*ldro=*/true), 2957u, 3u);
}

// ---------------------------------------------------------------------------
// The bug signature: a stale (off) LDRO underestimates airtime by a whole
// code block at sizes that straddle a symbol-block boundary, but not at
// every size. Slot sizing built on the underestimate desynchronizes nodes.
// ---------------------------------------------------------------------------

TEST(LoRaAirtimeTest, StaleLdroUnderestimatesAirtimeAtSf12Bw125) {
    // 24 bytes: one 7-symbol block (~229 ms) shorter with LDRO off.
    uint32_t with_ldro_24 = Toa(12, 125.0f, 24, true);
    uint32_t without_ldro_24 = Toa(12, 125.0f, 24, false);
    EXPECT_GT(with_ldro_24, without_ldro_24);
    EXPECT_NEAR(with_ldro_24 - without_ldro_24, 229u, 5u);

    // 48 bytes: two blocks (~458 ms) shorter.
    uint32_t with_ldro_48 = Toa(12, 125.0f, 48, true);
    uint32_t without_ldro_48 = Toa(12, 125.0f, 48, false);
    EXPECT_GT(with_ldro_48, without_ldro_48);
    EXPECT_NEAR(with_ldro_48 - without_ldro_48, 458u, 5u);
}

TEST(LoRaAirtimeTest, LdroDoesNotChangeAirtimeAtBoundaryInsensitiveSize) {
    // 20 bytes lands in the same symbol block either way, so the stale LDRO
    // state happens to be harmless here.
    EXPECT_EQ(Toa(12, 125.0f, 20, true), Toa(12, 125.0f, 20, false));
}

// ---------------------------------------------------------------------------
// Model the begin() ordering bug directly: a driver that computes LDRO from
// the default spreading factor (9) during setBandwidth() and never refreshes
// it for the configured SF. The error appears only where the correct LDRO
// decision differs from that default-SF baseline.
// ---------------------------------------------------------------------------

TEST(LoRaAirtimeTest, StaleDefaultSfLdroOnlyMisestimatesAtSf11AndSf12Bw125) {
    constexpr float kBw = 125.0f;
    // LDRO state a buggy driver retains: computed from the default SF (9).
    const bool stale_ldro = ShouldEnableLdro(9, kBw);
    ASSERT_FALSE(stale_ldro);

    for (uint8_t sf = 7; sf <= 12; ++sf) {
        const bool correct_ldro = ShouldEnableLdro(sf, kBw);
        uint32_t correct = Toa(sf, kBw, 48, correct_ldro);
        uint32_t buggy = Toa(sf, kBw, 48, stale_ldro);
        if (sf >= 11) {
            EXPECT_GT(correct, buggy) << "SF" << static_cast<int>(sf);
        } else {
            EXPECT_EQ(correct, buggy) << "SF" << static_cast<int>(sf);
        }
    }
}

}  // namespace
}  // namespace airtime
}  // namespace loramesher

#endif  // ARDUINO
