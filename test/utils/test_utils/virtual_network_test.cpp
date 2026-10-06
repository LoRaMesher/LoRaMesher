/**
 * @file virtual_network_test.cpp
 * @brief Determinism tests for the VirtualNetwork test harness
 */
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "../test/utils/network_testing_impl.hpp"

namespace loramesher {
namespace test {
namespace {

/**
 * @brief Receiver that is always listening and records every packet index
 */
class RecordingReceiver : public IRadioReceiver {
   public:
    void ReceiveMessage(const std::vector<uint8_t>& data, float /*rssi*/,
                        float /*snr*/) override {
        if (!data.empty()) {
            received.push_back(data[0]);
        }
    }

    bool CanReceive() const override { return true; }

    radio::RadioState GetRadioState() const override {
        return radio::RadioState::kReceive;
    }

    std::vector<uint8_t> received;
};

constexpr uint32_t kSender = 0x0001;
constexpr uint32_t kReceiver = 0x0002;
constexpr int kPackets = 200;
constexpr uint32_t kGapMs = 100;  // Longer than one packet's time on air

/**
 * @brief Send kPackets packets over a lossy link and return the indices that
 * arrived.
 */
std::vector<uint8_t> RunLossyLink(VirtualNetwork& network, float loss_rate) {
    RecordingReceiver sender;
    RecordingReceiver receiver;
    network.RegisterNode(kSender, &sender);
    network.RegisterNode(kReceiver, &receiver);
    network.SetLinkStatus(kSender, kReceiver, true);
    network.SetPacketLossRate(loss_rate);

    for (int i = 0; i < kPackets; ++i) {
        network.TransmitMessage(kSender, {static_cast<uint8_t>(i), 0xAA, 0xBB});
        network.AdvanceTime(kGapMs);
    }
    network.UnregisterNode(kSender);
    network.UnregisterNode(kReceiver);
    return receiver.received;
}

TEST(VirtualNetworkDeterminismTest, GlobalPacketLossIsReproducible) {
    VirtualNetwork first;
    VirtualNetwork second;

    std::vector<uint8_t> first_run = RunLossyLink(first, 0.5f);
    std::vector<uint8_t> second_run = RunLossyLink(second, 0.5f);

    // Some packets must be lost and some delivered for the check to be
    // meaningful.
    ASSERT_GT(first_run.size(), 0u);
    ASSERT_LT(first_run.size(), static_cast<size_t>(kPackets));
    EXPECT_EQ(first_run, second_run);
}

TEST(VirtualNetworkDeterminismTest, GlobalPacketLossFollowsSeed) {
    VirtualNetwork first;
    VirtualNetwork second;
    VirtualNetwork other_seed;
    first.SetSeed(7);
    second.SetSeed(7);
    other_seed.SetSeed(8);

    std::vector<uint8_t> first_run = RunLossyLink(first, 0.3f);
    std::vector<uint8_t> second_run = RunLossyLink(second, 0.3f);
    std::vector<uint8_t> other_run = RunLossyLink(other_seed, 0.3f);

    EXPECT_EQ(first_run, second_run);
    EXPECT_NE(first_run, other_run);
}

TEST(TestSeedTest, ParsesSeedFromEnvironmentValue) {
    EXPECT_EQ(TestSeedFromEnvironmentValue(nullptr), kDefaultTestSeed);
    EXPECT_EQ(TestSeedFromEnvironmentValue(""), kDefaultTestSeed);
    EXPECT_EQ(TestSeedFromEnvironmentValue("123"), 123u);
    EXPECT_EQ(TestSeedFromEnvironmentValue("0x10"), 16u);
    EXPECT_EQ(TestSeedFromEnvironmentValue("abc"), kDefaultTestSeed);
    EXPECT_EQ(TestSeedFromEnvironmentValue("12abc"), kDefaultTestSeed);
}

/**
 * @brief Three nodes in range of each other, stepped in 1 ms increments
 */
class VirtualNetworkAirtimeTest : public ::testing::Test {
   protected:
    static constexpr uint32_t kA = 0x000A;
    static constexpr uint32_t kB = 0x000B;
    static constexpr uint32_t kR = 0x000C;

    void SetUp() override {
        network_.RegisterNode(kA, &a_);
        network_.RegisterNode(kB, &b_);
        network_.RegisterNode(kR, &r_);
        network_.SetLinkStatus(kA, kR, true);
        network_.SetLinkStatus(kB, kR, true);
        network_.SetLinkStatus(kA, kB, true);
    }

    void TearDown() override {
        network_.UnregisterNode(kA);
        network_.UnregisterNode(kB);
        network_.UnregisterNode(kR);
    }

    void Step(uint32_t ms) {
        for (uint32_t i = 0; i < ms; ++i) {
            network_.AdvanceTime(1);
        }
    }

    // 3-byte packets at SF7/BW125: 31 ms on air
    static std::vector<uint8_t> Packet(uint8_t id) { return {id, 0x00, 0x00}; }

    VirtualNetwork network_;
    RecordingReceiver a_;
    RecordingReceiver b_;
    RecordingReceiver r_;
};

TEST_F(VirtualNetworkAirtimeTest, OverlapDeliveredInDifferentStepsCollides) {
    network_.TransmitMessage(kA, Packet(1));  // on air [0, 31)
    Step(20);
    network_.TransmitMessage(kB, Packet(2));  // on air [20, 51)
    Step(100);

    EXPECT_TRUE(r_.received.empty());
    EXPECT_EQ(network_.GetCollisionCount(kR), 2u);
}

TEST_F(VirtualNetworkAirtimeTest, ReceiverTransmittingDuringArrivalMissesIt) {
    network_.TransmitMessage(kA, Packet(1));  // on air [0, 31)
    Step(10);
    network_.TransmitMessage(kR, Packet(3));  // R busy [10, 41)
    Step(100);

    EXPECT_TRUE(r_.received.empty());
    EXPECT_EQ(network_.GetCollisionCount(kR), 0u);
}

TEST_F(VirtualNetworkAirtimeTest, BackToBackPacketsBothArrive) {
    network_.TransmitMessage(kA, Packet(1));  // on air [0, 31)
    Step(31);
    network_.TransmitMessage(kB, Packet(2));  // on air [31, 62)
    Step(100);

    EXPECT_EQ(r_.received, (std::vector<uint8_t>{1, 2}));
    EXPECT_EQ(network_.GetCollisionCount(kR), 0u);
}

}  // namespace
}  // namespace test
}  // namespace loramesher
