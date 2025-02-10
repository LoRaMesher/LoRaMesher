// test/types/test_configurations/protocol_configuration_test.cpp
#include <gtest/gtest.h>

#include "types/configurations/protocol_configuration.hpp"

namespace loramesher {
namespace test {

class ProtocolConfigTest : public ::testing::Test {
   protected:
    void SetUp() override { defaultConfig = ProtocolConfig::createDefault(); }

    ProtocolConfig defaultConfig;
};

TEST_F(ProtocolConfigTest, DefaultConstructorCreatesValidConfig) {
    EXPECT_TRUE(defaultConfig.isValid());
    EXPECT_EQ(defaultConfig.getHelloInterval(), 120000);
    EXPECT_EQ(defaultConfig.getSyncInterval(), 300000);
    EXPECT_EQ(defaultConfig.getMaxTimeouts(), 10);
}

TEST_F(ProtocolConfigTest, IntervalValidation) {
    EXPECT_THROW(defaultConfig.setHelloInterval(500), std::invalid_argument);
    EXPECT_THROW(defaultConfig.setHelloInterval(4000000),
                 std::invalid_argument);
    EXPECT_NO_THROW(defaultConfig.setHelloInterval(60000));
}

TEST_F(ProtocolConfigTest, SyncIntervalMustBeGreaterThanHelloInterval) {
    defaultConfig.setHelloInterval(60000);
    EXPECT_THROW(defaultConfig.setSyncInterval(30000), std::invalid_argument);
    EXPECT_NO_THROW(defaultConfig.setSyncInterval(120000));
}

TEST_F(ProtocolConfigTest, ValidationMessages) {
    ProtocolConfig config(500, 400, 0);
    EXPECT_FALSE(config.isValid());
    std::string errors = config.validate();
    EXPECT_TRUE(errors.find("Hello interval out of range") !=
                std::string::npos);
    EXPECT_TRUE(errors.find("Sync interval must be greater") !=
                std::string::npos);
    EXPECT_TRUE(errors.find("Max timeouts must be greater than 0") !=
                std::string::npos);
}

}  // namespace test
}  // namespace loramesher