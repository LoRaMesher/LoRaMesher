// test/types/configurations/pin_config_test.cpp
#include <gtest/gtest.h>
#include "../src/types/configurations/pin_configuration.hpp"

namespace loramesher {
namespace test {

class PinConfigTest : public ::testing::Test {
   protected:
    void SetUp() override { defaultConfig = PinConfig::createDefault(); }

    PinConfig defaultConfig;
};

TEST_F(PinConfigTest, DefaultConstructorCreatesValidConfig) {
    EXPECT_TRUE(defaultConfig.isValid());
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
    EXPECT_FALSE(config.isValid());
    std::string errors = config.validate();
    EXPECT_TRUE(errors.find("Invalid NSS pin") != std::string::npos);
    EXPECT_TRUE(errors.find("Invalid Reset pin") != std::string::npos);
    EXPECT_TRUE(errors.find("Invalid DIO0 pin") != std::string::npos);
    EXPECT_TRUE(errors.find("Invalid DIO1 pin") != std::string::npos);
}

}  // namespace test
}  // namespace loramesher

#if defined(ARDUINO)
#include <Arduino.h>

void setup() {
    // should be the same value as for the `test_speed` option in "platformio.ini"
    // default value is test_speed=115200
    Serial.begin(115200);

    ::testing::InitGoogleTest();
}

void loop() {
    // Run tests
    if (RUN_ALL_TESTS())
        ;

    // sleep 1 sec
    delay(1000);
}

#else
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS())
        ;
    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}
#endif
