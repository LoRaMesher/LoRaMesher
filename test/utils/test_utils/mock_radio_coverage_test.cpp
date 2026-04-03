/**
 * @file mock_radio_coverage_test.cpp
 * @brief Coverage tests for MockRadio forwarding methods in mock_radio.cpp.
 *
 * This file lives in the utils/test_utils suite, which produces the FINAL
 * binary used by llvm-cov for coverage reporting. mock_radio.cpp is compiled
 * into all test binaries, but only functions called from utils/test_utils
 * code show non-zero coverage in the report.
 *
 * Each TEST_F exercises exactly one uncovered forwarding method.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>
#include <memory>

#include "mocks/mock_radio.hpp"
#include "mocks/mock_radio_test_helpers.hpp"
#include "types/configurations/radio_configuration.hpp"
#include "types/error_codes/result.hpp"
#include "types/radio/radio_event.hpp"
#include "types/radio/radio_state.hpp"

using namespace loramesher;
using namespace loramesher::radio;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;

namespace loramesher {
namespace test {

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class MockRadioCoverageTest : public ::testing::Test {
   protected:
    MockRadio radio_;

    void SetUp() override {
        loramesher::radio::test::MockRadio& mock = GetMockForTesting(radio_);

        ON_CALL(mock, Configure(_)).WillByDefault(Return(Result::Success()));
        ON_CALL(mock, setSpreadingFactor(_))
            .WillByDefault(Return(Result::Success()));
        ON_CALL(mock, setBandwidth(_)).WillByDefault(Return(Result::Success()));
        ON_CALL(mock, setCodingRate(_))
            .WillByDefault(Return(Result::Success()));
        ON_CALL(mock, setPower(_)).WillByDefault(Return(Result::Success()));
        ON_CALL(mock, setSyncWord(_)).WillByDefault(Return(Result::Success()));
        ON_CALL(mock, setCRC(_)).WillByDefault(Return(Result::Success()));
        ON_CALL(mock, setPreambleLength(_))
            .WillByDefault(Return(Result::Success()));
        ON_CALL(mock, getLastPacketRSSI()).WillByDefault(Return(-80));
        ON_CALL(mock, getLastPacketSNR()).WillByDefault(Return(7));
        ON_CALL(mock, IsTransmitting()).WillByDefault(Return(false));
        ON_CALL(mock, getFrequency()).WillByDefault(Return(869.9f));
        ON_CALL(mock, getSpreadingFactor()).WillByDefault(Return(7));
        ON_CALL(mock, getBandwidth()).WillByDefault(Return(125.0f));
        ON_CALL(mock, getCodingRate()).WillByDefault(Return(5));
        ON_CALL(mock, getPower()).WillByDefault(Return(14));
        ON_CALL(
            mock,
            setActionReceive(
                ::testing::An<
                    std::function<void(std::unique_ptr<radio::RadioEvent>)>>()))
            .WillByDefault(Return(Result::Success()));
        ON_CALL(mock, setState(_)).WillByDefault(Return(Result::Success()));
        ON_CALL(mock, getState()).WillByDefault(Return(RadioState::kIdle));
    }
};

// ---------------------------------------------------------------------------
// Configure
// ---------------------------------------------------------------------------

TEST_F(MockRadioCoverageTest, Configure) {
    RadioConfig cfg;
    cfg.setFrequency(868.0f);
    Result r = radio_.Configure(cfg);
    EXPECT_TRUE(r.IsSuccess());
}

// ---------------------------------------------------------------------------
// Parameter setters
// ---------------------------------------------------------------------------

TEST_F(MockRadioCoverageTest, SetSpreadingFactor) {
    Result r = radio_.setSpreadingFactor(7);
    EXPECT_TRUE(r.IsSuccess());
}

TEST_F(MockRadioCoverageTest, SetBandwidth) {
    Result r = radio_.setBandwidth(125.0f);
    EXPECT_TRUE(r.IsSuccess());
}

TEST_F(MockRadioCoverageTest, SetCodingRate) {
    Result r = radio_.setCodingRate(5);
    EXPECT_TRUE(r.IsSuccess());
}

TEST_F(MockRadioCoverageTest, SetPower) {
    Result r = radio_.setPower(14);
    EXPECT_TRUE(r.IsSuccess());
}

TEST_F(MockRadioCoverageTest, SetSyncWord) {
    Result r = radio_.setSyncWord(0x12);
    EXPECT_TRUE(r.IsSuccess());
}

TEST_F(MockRadioCoverageTest, SetCRC) {
    Result r = radio_.setCRC(true);
    EXPECT_TRUE(r.IsSuccess());
}

TEST_F(MockRadioCoverageTest, SetPreambleLength) {
    Result r = radio_.setPreambleLength(8u);
    EXPECT_TRUE(r.IsSuccess());
}

// ---------------------------------------------------------------------------
// Radio status getters
// ---------------------------------------------------------------------------

TEST_F(MockRadioCoverageTest, GetLastPacketRSSI) {
    float rssi = radio_.getLastPacketRSSI();
    (void)rssi;
    SUCCEED();
}

TEST_F(MockRadioCoverageTest, GetLastPacketSNR) {
    float snr = radio_.getLastPacketSNR();
    (void)snr;
    SUCCEED();
}

TEST_F(MockRadioCoverageTest, IsTransmitting) {
    bool tx = radio_.IsTransmitting();
    (void)tx;
    SUCCEED();
}

TEST_F(MockRadioCoverageTest, GetFrequency) {
    float freq = radio_.getFrequency();
    (void)freq;
    SUCCEED();
}

TEST_F(MockRadioCoverageTest, GetSpreadingFactor) {
    uint8_t sf = radio_.getSpreadingFactor();
    (void)sf;
    SUCCEED();
}

TEST_F(MockRadioCoverageTest, GetBandwidth) {
    float bw = radio_.getBandwidth();
    (void)bw;
    SUCCEED();
}

TEST_F(MockRadioCoverageTest, GetCodingRate) {
    uint8_t cr = radio_.getCodingRate();
    (void)cr;
    SUCCEED();
}

TEST_F(MockRadioCoverageTest, GetPower) {
    uint8_t pwr = radio_.getPower();
    (void)pwr;
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Event handling
// ---------------------------------------------------------------------------

TEST_F(MockRadioCoverageTest, SetActionReceiveLambda) {
    bool fired = false;
    std::function<void(std::unique_ptr<radio::RadioEvent>)> cb =
        [&fired](std::unique_ptr<radio::RadioEvent>) {
            fired = true;
        };
    Result r = radio_.setActionReceive(std::move(cb));
    EXPECT_TRUE(r.IsSuccess());
}

TEST_F(MockRadioCoverageTest, SetState) {
    Result r = radio_.setState(RadioState::kReceive);
    EXPECT_TRUE(r.IsSuccess());
}

TEST_F(MockRadioCoverageTest, GetState) {
    RadioState state = radio_.getState();
    (void)state;
    SUCCEED();
}

}  // namespace test
}  // namespace loramesher
