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

}  // namespace
}  // namespace test
}  // namespace loramesher
