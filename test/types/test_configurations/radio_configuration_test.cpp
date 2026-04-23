// test/types/test_configurations/radio_configuration_test.cpp
#include <gtest/gtest.h>

#include "types/configurations/radio_configuration.hpp"

namespace loramesher {
namespace test {

class RadioConfigTest : public ::testing::Test {
   protected:
    void SetUp() override {
        defaultConfig = RadioConfig::CreateDefaultSx1276();
    }

    RadioConfig defaultConfig;
};

TEST_F(RadioConfigTest, DefaultConstructorCreatesValidConfig) {
    EXPECT_TRUE(defaultConfig.IsValid());
    EXPECT_FLOAT_EQ(defaultConfig.getFrequency(), 869.900F);
    EXPECT_EQ(defaultConfig.getSpreadingFactor(), 7);
    EXPECT_FLOAT_EQ(defaultConfig.getBandwidth(), 125.0);
    EXPECT_EQ(defaultConfig.getCodingRate(), 5);
    EXPECT_EQ(defaultConfig.getPower(), 17);
}

TEST_F(RadioConfigTest, FrequencyValidation) {
    EXPECT_THROW(defaultConfig.setFrequency(100.0F), std::invalid_argument);
    EXPECT_THROW(defaultConfig.setFrequency(1100.0F), std::invalid_argument);
    EXPECT_NO_THROW(defaultConfig.setFrequency(868.0F));
}

TEST_F(RadioConfigTest, SpreadingFactorValidation) {
    EXPECT_THROW(defaultConfig.setSpreadingFactor(5), std::invalid_argument);
    EXPECT_THROW(defaultConfig.setSpreadingFactor(13), std::invalid_argument);
    EXPECT_NO_THROW(defaultConfig.setSpreadingFactor(7));
}

TEST_F(RadioConfigTest, CodingRateValidation) {
    EXPECT_THROW(defaultConfig.setCodingRate(4), std::invalid_argument);
    EXPECT_THROW(defaultConfig.setCodingRate(9), std::invalid_argument);
    EXPECT_NO_THROW(defaultConfig.setCodingRate(5));
}

TEST_F(RadioConfigTest, ValidationMessages) {
    EXPECT_THROW(
        RadioConfig config(RadioType::kSx1276, 100.0F, 5, -1.0F, 4, 25),
        std::invalid_argument);
}

// ===========================================================================
// RadioConfigCoverageTest — targets uncovered lines in radio_configuration.cpp
// ===========================================================================

class RadioConfigCoverageTest : public ::testing::Test {
   protected:
    RadioConfig cfg_;  // default-constructed SX1276 config
};

TEST_F(RadioConfigCoverageTest, CreateDefaultSx1278IsValid) {
    RadioConfig c = RadioConfig::CreateDefaultSx1278();
    EXPECT_TRUE(c.IsValid());
    EXPECT_EQ(c.getRadioTypeString(), "SX1278");
}

TEST_F(RadioConfigCoverageTest, CreateDefaultSx1262IsValid) {
    RadioConfig c = RadioConfig::CreateDefaultSx1262();
    EXPECT_TRUE(c.IsValid());
    EXPECT_EQ(c.getRadioTypeString(), "SX1262");
}

TEST_F(RadioConfigCoverageTest, GetRadioTypeStringAllValues) {
    RadioConfig sx1276 = RadioConfig::CreateDefaultSx1276();
    EXPECT_EQ(sx1276.getRadioTypeString(), "SX1276");

    RadioConfig mock(RadioType::kMockRadio, 869.9f, 7, 125.0f, 5, 17);
    EXPECT_EQ(mock.getRadioTypeString(), "MockRadio");
}

TEST_F(RadioConfigCoverageTest, SetterMethodsSucceedOnValidConfig) {
    RadioConfig c = RadioConfig::CreateDefaultSx1276();
    EXPECT_TRUE(c.setSyncWord(0x12).IsSuccess());
    EXPECT_TRUE(c.setCRC(true).IsSuccess());
    EXPECT_TRUE(c.setCRC(false).IsSuccess());
    EXPECT_TRUE(c.setPreambleLength(16).IsSuccess());
}

TEST_F(RadioConfigCoverageTest, SetBandwidthInvalidThrows) {
    RadioConfig c = RadioConfig::CreateDefaultSx1276();
    EXPECT_THROW(c.setBandwidth(-1.0f), std::invalid_argument);
    EXPECT_THROW(c.setBandwidth(0.0f), std::invalid_argument);
}

TEST_F(RadioConfigCoverageTest, SetBandwidthValid) {
    RadioConfig c = RadioConfig::CreateDefaultSx1276();
    EXPECT_NO_THROW(c.setBandwidth(250.0f));
}

TEST_F(RadioConfigCoverageTest, SetPowerValid) {
    RadioConfig c = RadioConfig::CreateDefaultSx1276();
    EXPECT_NO_THROW(c.setPower(14));
}

TEST_F(RadioConfigCoverageTest, SetPowerTooHighThrows) {
    RadioConfig c = RadioConfig::CreateDefaultSx1276();
    EXPECT_THROW(c.setPower(21), std::invalid_argument);
}

TEST_F(RadioConfigCoverageTest, ValidateMultipleErrors) {
    try {
        RadioConfig bad(RadioType::kSx1276, 100.0f, 5, -1.0f, 4, 25);
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_FALSE(msg.empty());
    }
}

TEST_F(RadioConfigCoverageTest, GetRadioTypeReturnsConfiguredType) {
    RadioConfig sx1276 = RadioConfig::CreateDefaultSx1276();
    EXPECT_EQ(sx1276.getRadioType(), RadioType::kSx1276);

    RadioConfig sx1278 = RadioConfig::CreateDefaultSx1278();
    EXPECT_EQ(sx1278.getRadioType(), RadioType::kSx1278);

    RadioConfig sx1262 = RadioConfig::CreateDefaultSx1262();
    EXPECT_EQ(sx1262.getRadioType(), RadioType::kSx1262);

    RadioConfig mock(RadioType::kMockRadio, 869.9f, 7, 125.0f, 5, 17);
    EXPECT_EQ(mock.getRadioType(), RadioType::kMockRadio);
}

TEST_F(RadioConfigCoverageTest, GetSyncWordReturnsConfiguredValue) {
    RadioConfig c = RadioConfig::CreateDefaultSx1276();
    EXPECT_EQ(c.getSyncWord(), 20U);

    c.setSyncWord(0x34);
    EXPECT_EQ(c.getSyncWord(), 0x34);
}

TEST_F(RadioConfigCoverageTest, GetCRCReturnsConfiguredValue) {
    RadioConfig c = RadioConfig::CreateDefaultSx1276();
    EXPECT_TRUE(c.getCRC());

    c.setCRC(false);
    EXPECT_FALSE(c.getCRC());

    c.setCRC(true);
    EXPECT_TRUE(c.getCRC());
}

TEST_F(RadioConfigCoverageTest, GetPreambleLengthReturnsConfiguredValue) {
    RadioConfig c = RadioConfig::CreateDefaultSx1276();
    EXPECT_EQ(c.getPreambleLength(), 8U);

    c.setPreambleLength(16);
    EXPECT_EQ(c.getPreambleLength(), 16U);
}

TEST_F(RadioConfigCoverageTest, SetRadioTypeChangesType) {
    RadioConfig c = RadioConfig::CreateDefaultSx1276();
    EXPECT_EQ(c.getRadioType(), RadioType::kSx1276);

    c.setRadioType(RadioType::kSx1262);
    EXPECT_EQ(c.getRadioType(), RadioType::kSx1262);
}

TEST_F(RadioConfigCoverageTest, TcxoVoltageDefaultIsZero) {
    RadioConfig c;
    EXPECT_FLOAT_EQ(c.getTcxoVoltage(), 0.0F);
}

TEST_F(RadioConfigCoverageTest, SetTcxoVoltageValidRange) {
    RadioConfig c;
    c.setTcxoVoltage(1.8F);
    EXPECT_FLOAT_EQ(c.getTcxoVoltage(), 1.8F);

    c.setTcxoVoltage(0.0F);
    EXPECT_FLOAT_EQ(c.getTcxoVoltage(), 0.0F);

    c.setTcxoVoltage(3.3F);
    EXPECT_FLOAT_EQ(c.getTcxoVoltage(), 3.3F);
}

TEST_F(RadioConfigCoverageTest, SetTcxoVoltageRejectsInvalid) {
    RadioConfig c;
    EXPECT_THROW(c.setTcxoVoltage(-0.1F), std::invalid_argument);
    EXPECT_THROW(c.setTcxoVoltage(3.4F), std::invalid_argument);
}

// ===========================================================================
// GetMaxPacketSizeForSf — LoRaWAN EU868-derived table with BW scaling
// ===========================================================================

TEST(RadioConfigMaxPacketForSfTest, TableAtBw125) {
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(7, 125.0F), 242);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(8, 125.0F), 242);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(9, 125.0F), 115);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(10, 125.0F), 51);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(11, 125.0F), 51);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(12, 125.0F), 51);
}

TEST(RadioConfigMaxPacketForSfTest, ScalesAtBw250) {
    // SF7/8: 242 * 2 = 484 -> clamped to 255
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(7, 250.0F), 255);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(8, 250.0F), 255);
    // SF9: 115 * 2 = 230 (below 255)
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(9, 250.0F), 230);
    // SF10-12: 51 * 2 = 102
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(10, 250.0F), 102);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(11, 250.0F), 102);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(12, 250.0F), 102);
}

TEST(RadioConfigMaxPacketForSfTest, ScalesAtBw500) {
    // All SF7-9 clamp to 255 at x4
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(7, 500.0F), 255);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(9, 500.0F), 255);
    // SF10-12: 51 * 4 = 204
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(10, 500.0F), 204);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(12, 500.0F), 204);
}

TEST(RadioConfigMaxPacketForSfTest, ReturnsInValidRange) {
    for (uint8_t sf = 6; sf <= 12; ++sf) {
        for (float bw : {125.0F, 250.0F, 500.0F}) {
            uint8_t v = RadioConfig::GetMaxPacketSizeForSf(sf, bw);
            EXPECT_GE(v, 1);
            EXPECT_LE(v, 255);
        }
    }
}

TEST(RadioConfigMaxPacketForSfTest, UnknownSfReturnsConservativeDefault) {
    // Out-of-range SF falls back to the most conservative cap (51)
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(6, 125.0F), 51);
    EXPECT_EQ(RadioConfig::GetMaxPacketSizeForSf(13, 125.0F), 51);
}

}  // namespace test
}  // namespace loramesher