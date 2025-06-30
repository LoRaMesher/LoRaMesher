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

}  // namespace test
}  // namespace loramesher