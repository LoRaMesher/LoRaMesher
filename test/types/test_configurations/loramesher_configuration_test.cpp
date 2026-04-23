// test/types/test_configurations/loramesher_configuration.cpp
#include <gtest/gtest.h>

#include "types/configurations/loramesher_configuration.hpp"

namespace loramesher {
namespace test {

class ConfigTest : public ::testing::Test {
   protected:
    void SetUp() override { defaultConfig = Config::CreateDefault(); }

    Config defaultConfig;
};

TEST_F(ConfigTest, DefaultConstructorCreatesValidConfig) {
    EXPECT_TRUE(defaultConfig.IsValid());
    EXPECT_TRUE(defaultConfig.getPinConfig().IsValid());
    EXPECT_TRUE(defaultConfig.getRadioConfig().IsValid());
    EXPECT_TRUE(defaultConfig.getProtocolConfig().IsValid());
    EXPECT_GT(defaultConfig.getSleepDuration(), 0);
    EXPECT_TRUE(defaultConfig.getDeepSleepEnabled());
}

TEST_F(ConfigTest, SettersValidateConfigs) {
    PinConfig invalidPins(-1, -1, -1, -1);
    EXPECT_THROW(defaultConfig.setPinConfig(invalidPins),
                 std::invalid_argument);
    // // TODO: // Implement
    //     ProtocolConfig invalidProtocol(500, 400, 0);
    //     EXPECT_THROW(defaultConfig.setProtocolConfig(invalidProtocol),
    //                  std::invalid_argument);
}

TEST_F(ConfigTest, SleepDurationValidation) {
    EXPECT_THROW(defaultConfig.setSleepDuration(0), std::invalid_argument);
    EXPECT_NO_THROW(defaultConfig.setSleepDuration(1000));
}

TEST_F(ConfigTest, SetRadioConfigValid) {
    RadioConfig valid_radio;
    EXPECT_NO_THROW(defaultConfig.setRadioConfig(valid_radio));
}

TEST_F(ConfigTest, SetProtocolConfigValid) {
    ProtocolConfig valid_protocol = ProtocolConfig::CreateDefault();
    EXPECT_NO_THROW(defaultConfig.setProtocolConfig(valid_protocol));
}

TEST_F(ConfigTest, SetDeepSleepEnabled) {
    defaultConfig.setDeepSleepEnabled(false);
    EXPECT_FALSE(defaultConfig.getDeepSleepEnabled());
    defaultConfig.setDeepSleepEnabled(true);
    EXPECT_TRUE(defaultConfig.getDeepSleepEnabled());
}

TEST_F(ConfigTest, ValidateReturnsEmptyForValid) {
    std::string errors = defaultConfig.Validate();
    EXPECT_EQ(errors, "");
}

TEST_F(ConfigTest, IsValidTrueForDefault) {
    EXPECT_TRUE(defaultConfig.IsValid());
}

TEST_F(ConfigTest, SetPinConfigValid) {
    PinConfig valid_pins;
    EXPECT_NO_THROW(defaultConfig.setPinConfig(valid_pins));
}

TEST_F(ConfigTest, ParameterizedConstructor) {
    PinConfig pins;
    RadioConfig radio;
    ProtocolConfig protocol = ProtocolConfig::CreateDefault();
    Config config(pins, radio, protocol, 2000, false);
    EXPECT_TRUE(config.IsValid());
    EXPECT_EQ(config.getSleepDuration(), 2000u);
    EXPECT_FALSE(config.getDeepSleepEnabled());
}

TEST_F(ConfigTest, ValidateWithInvalidPinConfig) {
    // Use the constructor to create a Config with invalid pins (bypasses setter validation)
    PinConfig invalid_pins(-1, -1, -1, -1);
    RadioConfig radio;
    ProtocolConfig protocol = ProtocolConfig::CreateDefault();
    // Direct construct — no validation in constructor
    Config bad_config(invalid_pins, radio, protocol, 1000, false);
    EXPECT_FALSE(bad_config.IsValid());
    std::string errors = bad_config.Validate();
    EXPECT_FALSE(errors.empty());
}

TEST_F(ConfigTest, ValidateWithZeroSleepDuration) {
    // Use the constructor to bypass the setter validation
    PinConfig pins;
    RadioConfig radio;
    ProtocolConfig protocol = ProtocolConfig::CreateDefault();
    Config bad_config(pins, radio, protocol, 0,
                      false);  // sleepDuration=0 → invalid
    EXPECT_FALSE(bad_config.IsValid());
    std::string errors = bad_config.Validate();
    EXPECT_FALSE(errors.empty());
}

// ===========================================================================
// LoRaMeshProtocolConfig::ApplySfDerivedDefaults — override semantics
// ===========================================================================

TEST(LoRaMeshProtocolConfigSfDefaultsTest, AppliesDefaultWhenNotUserSet) {
    LoRaMeshProtocolConfig config;
    EXPECT_FALSE(config.IsMaxPacketSizeUserSet());

    uint8_t cap = config.ApplySfDerivedDefaults(10, 125.0F);
    EXPECT_EQ(cap, 51);
    EXPECT_EQ(config.getMaxPacketSize(), 51);
}

TEST(LoRaMeshProtocolConfigSfDefaultsTest, RespectsUserOverride) {
    LoRaMeshProtocolConfig config;
    config.setMaxPacketSize(200);
    EXPECT_TRUE(config.IsMaxPacketSizeUserSet());

    uint8_t cap = config.ApplySfDerivedDefaults(10, 125.0F);
    EXPECT_EQ(cap, 51);  // Cap reported for caller's warning logic
    EXPECT_EQ(config.getMaxPacketSize(),
              200);  // User value preserved unchanged
}

TEST(LoRaMeshProtocolConfigSfDefaultsTest, UserValueBelowCapIsKept) {
    LoRaMeshProtocolConfig config;
    config.setMaxPacketSize(32);
    EXPECT_TRUE(config.IsMaxPacketSizeUserSet());

    uint8_t cap = config.ApplySfDerivedDefaults(7, 125.0F);
    EXPECT_EQ(cap, 242);
    EXPECT_EQ(config.getMaxPacketSize(), 32);
}

TEST(LoRaMeshProtocolConfigSfDefaultsTest, AppliesDefaultForEachSfAtBw125) {
    struct Expected {
        uint8_t sf;
        uint8_t size;
    };

    constexpr Expected table[] = {{7, 242}, {8, 242}, {9, 115},
                                  {10, 51}, {11, 51}, {12, 51}};
    for (const auto& e : table) {
        LoRaMeshProtocolConfig config;
        config.ApplySfDerivedDefaults(e.sf, 125.0F);
        EXPECT_EQ(config.getMaxPacketSize(), e.size)
            << "SF=" << static_cast<int>(e.sf);
    }
}

}  // namespace test
}  // namespace loramesher