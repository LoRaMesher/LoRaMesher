/**
 * @file rtt_estimator_test.cpp
 * @brief Unit tests for the retransmission-timeout estimator.
 */

#include <gtest/gtest.h>

#include "protocols/reliability/rtt_estimator.hpp"

namespace loramesher {
namespace protocols {
namespace reliability {
namespace test {

using types::protocols::lora_mesh::PathRtt;

constexpr uint32_t kFloor = 500;
constexpr uint32_t kMax = 1000000;

TEST(RttEstimatorTest, NoSampleUsesFallback) {
    PathRtt rtt;
    EXPECT_FALSE(HasRttSample(rtt));
    EXPECT_EQ(ComputeRto(rtt, 5000, kFloor, kMax), 5000u);
}

TEST(RttEstimatorTest, FirstSampleInitializesEstimate) {
    PathRtt rtt;
    AddRttSample(rtt, 1000);

    EXPECT_TRUE(HasRttSample(rtt));
    EXPECT_EQ(rtt.srtt_ms, 1000u);
    EXPECT_EQ(rtt.rttvar_ms, 500u);
    // RTO = SRTT + 4 * RTTVAR
    EXPECT_EQ(ComputeRto(rtt, 5000, kFloor, kMax), 3000u);
}

TEST(RttEstimatorTest, LaterSamplesAreSmoothed) {
    PathRtt rtt;
    AddRttSample(rtt, 1000);
    AddRttSample(rtt, 2000);

    // RTTVAR = 3/4 * 500 + 1/4 * |1000 - 2000| = 625
    // SRTT   = 7/8 * 1000 + 1/8 * 2000 = 1125
    EXPECT_EQ(rtt.rttvar_ms, 625u);
    EXPECT_EQ(rtt.srtt_ms, 1125u);
    EXPECT_EQ(ComputeRto(rtt, 5000, kFloor, kMax), 3625u);
}

TEST(RttEstimatorTest, RtoIsClampedToFloorAndMax) {
    PathRtt fast;
    AddRttSample(fast, 10);
    EXPECT_EQ(ComputeRto(fast, 5000, kFloor, kMax), kFloor);

    PathRtt slow;
    AddRttSample(slow, 400000);
    EXPECT_EQ(ComputeRto(slow, 5000, kFloor, 600000), 600000u);

    PathRtt none;
    EXPECT_EQ(ComputeRto(none, 100, kFloor, kMax), kFloor);
    EXPECT_EQ(ComputeRto(none, 2000000, kFloor, kMax), kMax);
}

TEST(RttEstimatorTest, ZeroSampleStillCountsAsSample) {
    PathRtt rtt;
    AddRttSample(rtt, 0);
    EXPECT_TRUE(HasRttSample(rtt));
}

}  // namespace test
}  // namespace reliability
}  // namespace protocols
}  // namespace loramesher
