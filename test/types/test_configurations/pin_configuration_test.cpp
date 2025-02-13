// test/types/test_configurations/pin_configuration_test.cpp
#include <gtest/gtest.h>

#include "../src/types/configurations/pin_configuration.hpp"

namespace loramesher {
namespace test {

class PinConfigTest : public ::testing::Test {
   protected:
    void SetUp() override { defaultConfig = PinConfig::CreateDefault(); }

    PinConfig defaultConfig;
};

TEST_F(PinConfigTest, DefaultConstructorCreatesValidConfig) {
    EXPECT_TRUE(defaultConfig.IsValid());
    EXPECT_EQ(defaultConfig.getNss(), 18);
    EXPECT_EQ(defaultConfig.getReset(), 23);
    EXPECT_EQ(defaultConfig.getDio0(), 26);
    EXPECT_EQ(defaultConfig.getDio1(), 33);
}

TEST_F(PinConfigTest, CustomConstructorSetsValues) {
    PinConfig config(1, 2, 3, 4);
    EXPECT_EQ(config.getNss(), 1);
    EXPECT_EQ(config.getReset(), 2);
    EXPECT_EQ(config.getDio0(), 3);
    EXPECT_EQ(config.getDio1(), 4);
}

TEST_F(PinConfigTest, SettersValidateInput) {
    EXPECT_THROW(defaultConfig.setNss(-1), std::invalid_argument);
    EXPECT_THROW(defaultConfig.setReset(-1), std::invalid_argument);
    EXPECT_THROW(defaultConfig.setDio0(-1), std::invalid_argument);
    EXPECT_THROW(defaultConfig.setDio1(-1), std::invalid_argument);
}

TEST_F(PinConfigTest, ValidationWorksCorrectly) {
    PinConfig config(-1, -1, -1, -1);
    EXPECT_FALSE(config.IsValid());
    std::string errors = config.Validate();
    EXPECT_TRUE(errors.find("Invalid NSS pin") != std::string::npos);
    EXPECT_TRUE(errors.find("Invalid Reset pin") != std::string::npos);
    EXPECT_TRUE(errors.find("Invalid DIO0 pin") != std::string::npos);
    EXPECT_TRUE(errors.find("Invalid DIO1 pin") != std::string::npos);
}

}  // namespace test
}  // namespace loramesher
