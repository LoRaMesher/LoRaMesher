/**
 * @file delivery_windows_test.cpp
 * @brief Unit tests for the per-stream delivery de-duplication windows.
 */

#include <gtest/gtest.h>

#include "protocols/reliability/delivery_windows.hpp"

namespace loramesher {
namespace protocols {
namespace reliability {
namespace test {

namespace {

constexpr AddressType kSource = 0x0010;
constexpr uint32_t kRegressionMs = 10000;

}  // namespace

class DeliveryWindowsTest : public ::testing::Test {
   protected:
    bool Accept(uint8_t seq, uint32_t ts = 1000,
                StreamKind kind = StreamKind::kUnicast,
                AddressType source = kSource) {
        return windows_.Accept(source, kind, seq, ts, kRegressionMs);
    }

    DeliveryWindows windows_;
};

TEST_F(DeliveryWindowsTest, FirstMessageOfAStreamIsNew) {
    EXPECT_TRUE(Accept(7));
}

TEST_F(DeliveryWindowsTest, RepeatedSequenceIsDuplicate) {
    EXPECT_TRUE(Accept(7));
    EXPECT_FALSE(Accept(7));
}

TEST_F(DeliveryWindowsTest, OutOfOrderInsideWindowIsDeliveredOnce) {
    EXPECT_TRUE(Accept(10));
    EXPECT_TRUE(Accept(12));
    EXPECT_TRUE(Accept(11));
    EXPECT_FALSE(Accept(11));
    EXPECT_FALSE(Accept(10));
    EXPECT_FALSE(Accept(12));
}

TEST_F(DeliveryWindowsTest, LargeJumpAheadClearsOlderHistory) {
    EXPECT_TRUE(Accept(10));
    EXPECT_TRUE(Accept(10 + DeliveryWindows::kWindow));
    // 10 is now a full window behind: treated as a new stream start.
    EXPECT_TRUE(Accept(10));
}

TEST_F(DeliveryWindowsTest, SequenceWrapIsAhead) {
    EXPECT_TRUE(Accept(250));
    EXPECT_TRUE(Accept(3));
    EXPECT_FALSE(Accept(250));
    EXPECT_FALSE(Accept(3));
}

TEST_F(DeliveryWindowsTest, WrappedSequenceIsNewAfterAFullCycle) {
    // A sender that wraps its 8-bit counter reuses 7 for a new message.
    uint8_t seq = 7;
    EXPECT_TRUE(Accept(seq));
    for (int i = 0; i < 256; ++i) {
        ++seq;
        EXPECT_TRUE(Accept(seq)) << "seq " << static_cast<int>(seq);
    }
    EXPECT_EQ(seq, 7);
}

TEST_F(DeliveryWindowsTest, FarBehindIsANewStreamStart) {
    EXPECT_TRUE(Accept(100));
    EXPECT_TRUE(Accept(100 - DeliveryWindows::kWindow));
    EXPECT_FALSE(Accept(100 - DeliveryWindows::kWindow));
}

TEST_F(DeliveryWindowsTest, SenderRestartResetsTheWindow) {
    EXPECT_TRUE(Accept(5, /*ts=*/50000));
    // Same sequence, but the sender's clock went back beyond the threshold.
    EXPECT_TRUE(Accept(5, /*ts=*/1000));
}

TEST_F(DeliveryWindowsTest, SmallClockRegressionKeepsTheWindow) {
    EXPECT_TRUE(Accept(5, /*ts=*/50000));
    // A retransmission stamped earlier than a later message still dedups.
    EXPECT_TRUE(Accept(6, /*ts=*/52000));
    EXPECT_FALSE(Accept(5, /*ts=*/51000));
}

TEST_F(DeliveryWindowsTest, UnicastAndGroupStreamsAreIndependent) {
    EXPECT_TRUE(Accept(9, 1000, StreamKind::kUnicast));
    EXPECT_TRUE(Accept(9, 1000, StreamKind::kGroup));
    EXPECT_FALSE(Accept(9, 1000, StreamKind::kUnicast));
    EXPECT_FALSE(Accept(9, 1000, StreamKind::kGroup));
}

TEST_F(DeliveryWindowsTest, SourcesAreIndependent) {
    EXPECT_TRUE(Accept(9, 1000, StreamKind::kUnicast, 0x0010));
    EXPECT_TRUE(Accept(9, 1000, StreamKind::kUnicast, 0x0011));
}

TEST_F(DeliveryWindowsTest, LeastRecentlyUsedStreamIsEvictedWhenFull) {
    for (size_t i = 0; i < DeliveryWindows::kCapacity; ++i) {
        ASSERT_TRUE(Accept(1, 1000, StreamKind::kUnicast,
                           static_cast<AddressType>(0x100 + i)));
    }
    // Touch the first stream so the second becomes least recently used.
    EXPECT_FALSE(Accept(1, 1000, StreamKind::kUnicast, 0x100));

    EXPECT_TRUE(Accept(1, 1000, StreamKind::kUnicast, 0x200));

    EXPECT_FALSE(Accept(1, 1000, StreamKind::kUnicast, 0x100));
    // The evicted stream has lost its history.
    EXPECT_TRUE(Accept(1, 1000, StreamKind::kUnicast, 0x101));
}

}  // namespace test
}  // namespace reliability
}  // namespace protocols
}  // namespace loramesher
