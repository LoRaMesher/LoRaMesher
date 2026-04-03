#include <gtest/gtest.h>

#include "types/protocols/lora_mesh/sliding_window_pdr.hpp"

namespace loramesher {
namespace test {

class SlidingWindowPDRTest : public ::testing::Test {
   protected:
    SlidingWindowPDR<8> window;
};

TEST_F(SlidingWindowPDRTest, EmptyWindowReturnsZero) {
    EXPECT_EQ(window.GetPDR(), 0);
    EXPECT_FALSE(window.IsReady());
}

TEST_F(SlidingWindowPDRTest, AllReceivedReturnsFull) {
    for (int i = 0; i < 8; i++) {
        window.Expect();
        window.Received();
    }
    EXPECT_EQ(window.GetPDR(), 255);
    EXPECT_TRUE(window.IsReady());
}

TEST_F(SlidingWindowPDRTest, AllMissedReturnsZero) {
    for (int i = 0; i < 8; i++) {
        window.Expect();
    }
    EXPECT_EQ(window.GetPDR(), 0);
    EXPECT_TRUE(window.IsReady());
}

TEST_F(SlidingWindowPDRTest, PartialReception) {
    // 6 received out of 8
    for (int i = 0; i < 8; i++) {
        window.Expect();
        if (i < 6) {
            window.Received();
        }
    }
    // 6/8 * 255 = 191
    EXPECT_EQ(window.GetPDR(), 191);
}

TEST_F(SlidingWindowPDRTest, WarmupScaling) {
    // After 4 received out of 4, PDR should be 255 (4/4), not 127 (4/8)
    for (int i = 0; i < 4; i++) {
        window.Expect();
        window.Received();
    }
    EXPECT_EQ(window.GetPDR(), 255);
    EXPECT_FALSE(window.IsReady());
}

TEST_F(SlidingWindowPDRTest, IsReadyAfterWindowFilled) {
    for (int i = 0; i < 7; i++) {
        window.Expect();
        EXPECT_FALSE(window.IsReady());
    }
    window.Expect();
    EXPECT_TRUE(window.IsReady());
}

TEST_F(SlidingWindowPDRTest, OldSlotsOverwritten) {
    // Fill window with all received
    for (int i = 0; i < 8; i++) {
        window.Expect();
        window.Received();
    }
    EXPECT_EQ(window.GetPDR(), 255);

    // Now miss 3 in a row
    for (int i = 0; i < 3; i++) {
        window.Expect();
    }
    // 5 received out of 8 = 5*255/8 = 159
    EXPECT_EQ(window.GetPDR(), 159);
}

TEST_F(SlidingWindowPDRTest, ResetClearsAll) {
    for (int i = 0; i < 8; i++) {
        window.Expect();
        window.Received();
    }
    EXPECT_TRUE(window.IsReady());
    EXPECT_EQ(window.GetPDR(), 255);

    window.Reset();
    EXPECT_FALSE(window.IsReady());
    EXPECT_EQ(window.GetPDR(), 0);
}

TEST_F(SlidingWindowPDRTest, EightyPercentLoss) {
    // Simulate 80% loss: receive 1 out of every 5
    for (int i = 0; i < 40; i++) {
        window.Expect();
        if (i % 5 == 0) {
            window.Received();
        }
    }
    uint8_t pdr = window.GetPDR();
    // With 20% reception rate over 8 slots, expect ~1-2 hits = 31-63
    EXPECT_LE(pdr, 95);  // At most 3/8
    EXPECT_GE(pdr, 0);   // At least 0/8
}

}  // namespace test
}  // namespace loramesher
