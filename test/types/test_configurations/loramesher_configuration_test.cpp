// test/types/test_configurations/loramesher_configuration.cpp
#include <gtest/gtest.h>

#include "types/configurations/loramesher_configuration.hpp"

namespace loramesher {
namespace test {

class ConfigTest : public ::testing::Test {
   protected:
    void SetUp() override { defaultConfig = Config::createDefault(); }

    Config defaultConfig;
};

TEST_F(ConfigTest, DefaultConstructorCreatesValidConfig) {
    EXPECT_TRUE(defaultConfig.isValid());
    EXPECT_TRUE(defaultConfig.getPinConfig().isValid());
    EXPECT_TRUE(defaultConfig.getRadioConfig().isValid());
    EXPECT_TRUE(defaultConfig.getProtocolConfig().isValid());
    EXPECT_GT(defaultConfig.getSleepDuration(), 0);
    EXPECT_TRUE(defaultConfig.isDeepSleepEnabled());
}

TEST_F(ConfigTest, SettersValidateConfigs) {
    PinConfig invalidPins(-1, -1, -1, -1);
    EXPECT_THROW(defaultConfig.setPinConfig(invalidPins),
                 std::invalid_argument);

    RadioConfig invalidRadio(100.0F, 5, -1.0F, 4, 25);
    EXPECT_THROW(defaultConfig.setRadioConfig(invalidRadio),
                 std::invalid_argument);

    ProtocolConfig invalidProtocol(500, 400, 0);
    EXPECT_THROW(defaultConfig.setProtocolConfig(invalidProtocol),
                 std::invalid_argument);
}

TEST_F(ConfigTest, SleepDurationValidation) {
    EXPECT_THROW(defaultConfig.setSleepDuration(0), std::invalid_argument);
    EXPECT_NO_THROW(defaultConfig.setSleepDuration(1000));
}

TEST_F(ConfigTest, CombinedValidation) {
    Config config(PinConfig(-1, -1, -1, -1),
                  RadioConfig(100.0F, 5, -1.0F, 4, 25),
                  ProtocolConfig(500, 400, 0), 0, true);

    EXPECT_FALSE(config.isValid());
    std::string errors = config.validate();
    EXPECT_TRUE(errors.find("Pin config errors") != std::string::npos);
    EXPECT_TRUE(errors.find("Radio config errors") != std::string::npos);
    EXPECT_TRUE(errors.find("Protocol config errors") != std::string::npos);
    EXPECT_TRUE(errors.find("Invalid sleep duration") != std::string::npos);
}

}  // namespace test
}  // namespace loramesher