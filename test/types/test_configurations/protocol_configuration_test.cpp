// test/types/test_configurations/protocol_configuration_test.cpp
#include <gtest/gtest.h>

#include "types/configurations/protocol_configuration.hpp"

namespace loramesher {
namespace test {

class ProtocolConfigTest : public ::testing::Test {
   protected:
    void SetUp() override { defaultConfig = ProtocolConfig::CreateDefault(); }

    ProtocolConfig defaultConfig;
};

TEST_F(ProtocolConfigTest, DefaultConstructorCreatesValidConfig) {
    EXPECT_TRUE(defaultConfig.IsValid());
    // EXPECT_EQ(defaultConfig.getHelloInterval(), 120000);
    // EXPECT_EQ(defaultConfig.getSyncInterval(), 300000);
    // EXPECT_EQ(defaultConfig.getMaxTimeouts(), 10);
}

// TEST_F(ProtocolConfigTest, IntervalValidation) {
//     EXPECT_THROW(defaultConfig.setHelloInterval(500), std::invalid_argument);
//     EXPECT_THROW(defaultConfig.setHelloInterval(4000000),
//                  std::invalid_argument);
//     EXPECT_NO_THROW(defaultConfig.setHelloInterval(60000));
// }

// TEST_F(ProtocolConfigTest, SyncIntervalMustBeGreaterThanHelloInterval) {
//     defaultConfig.setHelloInterval(60000);
//     EXPECT_THROW(defaultConfig.setSyncInterval(30000), std::invalid_argument);
//     EXPECT_NO_THROW(defaultConfig.setSyncInterval(120000));
// }

// TEST_F(ProtocolConfigTest, ValidationMessages) {
//     ProtocolConfig config(500, 400, 0);
//     EXPECT_FALSE(config.IsValid());
//     std::string errors = config.Validate();
//     EXPECT_TRUE(errors.find("Hello interval out of range") !=
//                 std::string::npos);
//     EXPECT_TRUE(errors.find("Sync interval must be greater") !=
//                 std::string::npos);
//     EXPECT_TRUE(errors.find("Max timeouts must be greater than 0") !=
//                 std::string::npos);
// }

// ---- PingPongProtocolConfig tests ----

class PingPongConfigTest : public ::testing::Test {
   protected:
    PingPongProtocolConfig config_{0x1234, 2000, 3};
};

TEST_F(PingPongConfigTest, DefaultValues) {
    PingPongProtocolConfig cfg;
    EXPECT_EQ(cfg.getNodeAddress(), 0u);
    EXPECT_EQ(cfg.getDefaultTimeout(), 2000u);
    EXPECT_EQ(cfg.getRetryCount(), 3u);
}

TEST_F(PingPongConfigTest, GettersSetters) {
    config_.setDefaultTimeout(5000);
    EXPECT_EQ(config_.getDefaultTimeout(), 5000u);

    config_.setRetryCount(7);
    EXPECT_EQ(config_.getRetryCount(), 7u);

    config_.setNodeAddress(0xABCD);
    EXPECT_EQ(config_.getNodeAddress(), 0xABCD);
}

TEST_F(PingPongConfigTest, IsValidTrue) {
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

TEST_F(PingPongConfigTest, IsValidTooShortTimeout) {
    config_.setDefaultTimeout(50);  // < 100ms
    EXPECT_FALSE(config_.IsValid());
    EXPECT_NE(config_.Validate(), "");
}

TEST_F(PingPongConfigTest, IsValidTooLongTimeout) {
    config_.setDefaultTimeout(60000);  // > 30000ms
    EXPECT_FALSE(config_.IsValid());
    EXPECT_NE(config_.Validate(), "");
}

TEST_F(PingPongConfigTest, IsValidTooManyRetries) {
    config_.setRetryCount(11);  // > 10
    EXPECT_FALSE(config_.IsValid());
    EXPECT_NE(config_.Validate(), "");
}

TEST_F(PingPongConfigTest, ProtocolType) {
    EXPECT_EQ(config_.getProtocolType(), protocols::ProtocolType::kPingPong);
}

// ---- LoRaMeshProtocolConfig tests ----

class LoRaMeshConfigTest : public ::testing::Test {
   protected:
    LoRaMeshProtocolConfig config_{0x1001, 60000, 180000, 5, 255,
                                   1,      30000, 50,     50};
};

TEST_F(LoRaMeshConfigTest, DefaultValues) {
    LoRaMeshProtocolConfig cfg;
    EXPECT_EQ(cfg.getNodeAddress(), 0u);
    EXPECT_EQ(cfg.getMaxHops(), 5u);
}

TEST_F(LoRaMeshConfigTest, GettersSetters) {
    config_.setHelloInterval(30000);
    EXPECT_EQ(config_.getHelloInterval(), 30000u);

    config_.setRouteTimeout(300000);
    EXPECT_EQ(config_.getRouteTimeout(), 300000u);

    config_.setMaxHops(10);
    EXPECT_EQ(config_.getMaxHops(), 10u);

    config_.setMaxPacketSize(200);
    EXPECT_EQ(config_.getMaxPacketSize(), 200u);

    config_.setDefaultDataSlots(4);
    EXPECT_EQ(config_.getDefaultDataSlots(), 4u);

    config_.setJoiningTimeout(60000);
    EXPECT_EQ(config_.getJoiningTimeout(), 60000u);

    config_.setMaxNetworkNodes(20);
    EXPECT_EQ(config_.getMaxNetworkNodes(), 20u);

    config_.setMaxDataSlots(30);
    EXPECT_EQ(config_.getMaxDataSlots(), 30u);

    config_.setGuardTime(100);
    EXPECT_EQ(config_.getGuardTime(), 100u);
}

TEST_F(LoRaMeshConfigTest, IsValidTrue) {
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

TEST_F(LoRaMeshConfigTest, ProtocolType) {
    EXPECT_EQ(config_.getProtocolType(), protocols::ProtocolType::kLoraMesh);
}

TEST_F(LoRaMeshConfigTest, SetMinSleepFraction) {
    config_.setMinSleepFraction(0.10f);
    EXPECT_NEAR(config_.getMinSleepFraction(), 0.10f, 0.001f);

    // Clamped to [0.0, 0.9]
    config_.setMinSleepFraction(-0.1f);
    EXPECT_NEAR(config_.getMinSleepFraction(), 0.0f, 0.001f);

    config_.setMinSleepFraction(0.95f);
    EXPECT_NEAR(config_.getMinSleepFraction(), 0.9f, 0.001f);
}

TEST_F(LoRaMeshConfigTest, SetTargetDutyCycle) {
    config_.setTargetDutyCycle(0.05f);
    EXPECT_NEAR(config_.getTargetDutyCycle(), 0.05f, 0.001f);

    // Clamped to [0.001, 1.0]
    config_.setTargetDutyCycle(0.0f);
    EXPECT_GE(config_.getTargetDutyCycle(), 0.001f);

    config_.setTargetDutyCycle(1.5f);
    EXPECT_NEAR(config_.getTargetDutyCycle(), 1.0f, 0.001f);
}

TEST_F(LoRaMeshConfigTest, NodeRoleGetterSetter) {
    config_.setNodeRole(NodeRole::NETWORK_MANAGER);
    EXPECT_EQ(config_.getNodeRole(), NodeRole::NETWORK_MANAGER);

    config_.setNodeRole(NodeRole::NODE_ONLY);
    EXPECT_EQ(config_.getNodeRole(), NodeRole::NODE_ONLY);

    config_.setNodeRole(NodeRole::AUTO);
    EXPECT_EQ(config_.getNodeRole(), NodeRole::AUTO);
}

TEST_F(LoRaMeshConfigTest, NodeCapabilities) {
    config_.setNodeCapabilities(0xFF);
    EXPECT_EQ(config_.getNodeCapabilities(), 0xFF);
}

// ---- ProtocolConfig wrapper tests ----

class ProtocolConfigWrapperTest : public ::testing::Test {};

TEST_F(ProtocolConfigWrapperTest, CreateDefaultIsValid) {
    ProtocolConfig cfg = ProtocolConfig::CreateDefault();
    EXPECT_TRUE(cfg.IsValid());
}

TEST_F(ProtocolConfigWrapperTest, NodeAddressPassthrough) {
    ProtocolConfig cfg = ProtocolConfig::CreateDefault();
    cfg.setNodeAddress(0x5678);
    EXPECT_EQ(cfg.getNodeAddress(), 0x5678u);
}

TEST_F(ProtocolConfigWrapperTest, PingPongConfigAccess) {
    PingPongProtocolConfig pp_cfg(0x1234, 3000, 2);
    ProtocolConfig cfg;
    cfg.setPingPongConfig(pp_cfg);

    EXPECT_EQ(cfg.getProtocolType(), protocols::ProtocolType::kPingPong);
    EXPECT_EQ(cfg.getPingPongConfig().getDefaultTimeout(), 3000u);
}

TEST_F(ProtocolConfigWrapperTest, LoRaMeshConfigAccess) {
    LoRaMeshProtocolConfig lora_cfg(0x1001);
    lora_cfg.setMaxHops(8);
    ProtocolConfig cfg;
    cfg.setLoRaMeshConfig(lora_cfg);

    EXPECT_EQ(cfg.getProtocolType(), protocols::ProtocolType::kLoraMesh);
    EXPECT_EQ(cfg.getLoRaMeshConfig().getMaxHops(), 8u);
}

// ---- PingPongProtocolConfig additional coverage ----

TEST_F(PingPongConfigTest, ValidateMessageTooShortTimeout) {
    config_.setDefaultTimeout(50);
    EXPECT_EQ(config_.Validate(), "Default timeout too short (minimum 100ms)");
}

TEST_F(PingPongConfigTest, ValidateMessageTooLongTimeout) {
    config_.setDefaultTimeout(31000);
    EXPECT_EQ(config_.Validate(), "Default timeout too long (maximum 30s)");
}

TEST_F(PingPongConfigTest, ValidateMessageTooManyRetries) {
    config_.setRetryCount(11);
    EXPECT_EQ(config_.Validate(), "Too many retries (maximum 10)");
}

TEST_F(PingPongConfigTest, BoundaryTimeoutMin) {
    config_.setDefaultTimeout(100);
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

TEST_F(PingPongConfigTest, BoundaryTimeoutMax) {
    config_.setDefaultTimeout(30000);
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

TEST_F(PingPongConfigTest, BoundaryRetryCountMax) {
    config_.setRetryCount(10);
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

TEST_F(PingPongConfigTest, BoundaryRetryCountZero) {
    config_.setRetryCount(0);
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

// ---- LoRaMeshProtocolConfig Validate() error conditions ----

TEST_F(LoRaMeshConfigTest, ValidateDefaultValues) {
    LoRaMeshProtocolConfig cfg;
    EXPECT_EQ(cfg.getHelloInterval(), 60000u);
    EXPECT_EQ(cfg.getMaxHops(), 5u);
    EXPECT_EQ(cfg.getMaxPacketSize(), 255u);
    EXPECT_EQ(cfg.getDefaultDataSlots(), 1u);
    EXPECT_EQ(cfg.getMaxNetworkNodes(), 50u);
    EXPECT_EQ(cfg.getMaxDataSlots(), 50u);
    EXPECT_EQ(cfg.getGuardTime(), 50u);
    EXPECT_NEAR(cfg.getTargetDutyCycle(), 0.01f, 0.001f);
    EXPECT_NEAR(cfg.getMinSleepFraction(), 0.30f, 0.001f);
    EXPECT_EQ(cfg.getNodeRole(), NodeRole::AUTO);
    EXPECT_EQ(cfg.getNodeCapabilities(), 0u);
}

TEST_F(LoRaMeshConfigTest, ValidateHelloIntervalTooShort) {
    config_.setHelloInterval(4000);
    config_.setRouteTimeout(180000);
    EXPECT_FALSE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "Hello interval too short (minimum 5s)");
}

TEST_F(LoRaMeshConfigTest, ValidateHelloIntervalTooLong) {
    config_.setHelloInterval(3600001);
    config_.setRouteTimeout(3600001 * 2);
    EXPECT_FALSE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "Hello interval too long (maximum 1h)");
}

TEST_F(LoRaMeshConfigTest, ValidateRouteTimeoutNotGreaterThanHelloInterval) {
    config_.setHelloInterval(60000);
    config_.setRouteTimeout(60000);  // equal, not greater
    EXPECT_FALSE(config_.IsValid());
    EXPECT_EQ(config_.Validate(),
              "Route timeout must be greater than hello interval");
}

TEST_F(LoRaMeshConfigTest, ValidateRouteTimeoutLessThanHelloInterval) {
    config_.setHelloInterval(60000);
    config_.setRouteTimeout(30000);  // less than hello interval
    EXPECT_FALSE(config_.IsValid());
    EXPECT_EQ(config_.Validate(),
              "Route timeout must be greater than hello interval");
}

TEST_F(LoRaMeshConfigTest, ValidateMaxHopsZero) {
    config_.setMaxHops(0);
    EXPECT_FALSE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "Max hops must be at least 1");
}

TEST_F(LoRaMeshConfigTest, ValidateMaxHopsTooLarge) {
    config_.setMaxHops(17);
    EXPECT_FALSE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "Max hops too large (maximum 16)");
}

TEST_F(LoRaMeshConfigTest, ValidateGuardTimeTooShort) {
    config_.setGuardTime(5);
    EXPECT_FALSE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "Guard time too short (minimum 10ms)");
}

TEST_F(LoRaMeshConfigTest, ValidateGuardTimeTooLong) {
    config_.setGuardTime(501);
    EXPECT_FALSE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "Guard time too long (maximum 500ms)");
}

TEST_F(LoRaMeshConfigTest, ValidateBoundaryHelloIntervalMin) {
    config_.setHelloInterval(5000);
    config_.setRouteTimeout(180000);
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

TEST_F(LoRaMeshConfigTest, ValidateBoundaryHelloIntervalMax) {
    config_.setHelloInterval(3600000);
    config_.setRouteTimeout(3600000u * 2u);
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

TEST_F(LoRaMeshConfigTest, ValidateBoundaryMaxHopsMin) {
    config_.setMaxHops(1);
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

TEST_F(LoRaMeshConfigTest, ValidateBoundaryMaxHopsMax) {
    config_.setMaxHops(16);
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

TEST_F(LoRaMeshConfigTest, ValidateBoundaryGuardTimeMin) {
    config_.setGuardTime(10);
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

TEST_F(LoRaMeshConfigTest, ValidateBoundaryGuardTimeMax) {
    config_.setGuardTime(500);
    EXPECT_TRUE(config_.IsValid());
    EXPECT_EQ(config_.Validate(), "");
}

// ---- LoRaMeshProtocolConfig power callbacks ----

TEST_F(LoRaMeshConfigTest, PrepareSleepCallbackNullByDefault) {
    LoRaMeshProtocolConfig cfg;
    EXPECT_FALSE(cfg.getPrepareSleepCallback());
}

TEST_F(LoRaMeshConfigTest, WakeUpCallbackNullByDefault) {
    LoRaMeshProtocolConfig cfg;
    EXPECT_FALSE(cfg.getWakeUpCallback());
}

TEST_F(LoRaMeshConfigTest, SetPrepareSleepCallback) {
    bool called = false;
    config_.setPrepareSleepCallback([&called](const power::SleepContext&) {
        called = true;
        return power::SleepResult{true};
    });
    ASSERT_TRUE(config_.getPrepareSleepCallback());
    power::SleepContext ctx{power::PowerState::LIGHT_SLEEP, 1000, 0, false};
    auto result = config_.getPrepareSleepCallback()(ctx);
    EXPECT_TRUE(called);
    EXPECT_TRUE(result.allow_sleep);
}

TEST_F(LoRaMeshConfigTest, SetWakeUpCallback) {
    bool called = false;
    config_.setWakeUpCallback([&called](power::PowerState) { called = true; });
    ASSERT_TRUE(config_.getWakeUpCallback());
    config_.getWakeUpCallback()(power::PowerState::LIGHT_SLEEP);
    EXPECT_TRUE(called);
}

TEST_F(LoRaMeshConfigTest, SetPrepareSleepCallbackToNull) {
    config_.setPrepareSleepCallback(
        [](const power::SleepContext&) { return power::SleepResult{true}; });
    EXPECT_TRUE(config_.getPrepareSleepCallback());
    config_.setPrepareSleepCallback(nullptr);
    EXPECT_FALSE(config_.getPrepareSleepCallback());
}

TEST_F(LoRaMeshConfigTest, SetWakeUpCallbackToNull) {
    config_.setWakeUpCallback([](power::PowerState) {});
    EXPECT_TRUE(config_.getWakeUpCallback());
    config_.setWakeUpCallback(nullptr);
    EXPECT_FALSE(config_.getWakeUpCallback());
}

// ---- LoRaMeshProtocolConfig subslot configs ----

TEST_F(LoRaMeshConfigTest, GetSyncBeaconSubslotConfigDefault) {
    const auto& cfg = config_.getSyncBeaconSubslotConfig();
    EXPECT_EQ(cfg.num_subslots, 5u);
    EXPECT_EQ(cfg.strategy,
              protocols::lora_mesh::SubslotAssignment::ADDRESS_HASH);
}

TEST_F(LoRaMeshConfigTest, SetSyncBeaconSubslotConfig) {
    protocols::lora_mesh::SubslotConfig new_cfg{
        3, 20, protocols::lora_mesh::SubslotAssignment::HOP_BASED};
    config_.setSyncBeaconSubslotConfig(new_cfg);
    const auto& result = config_.getSyncBeaconSubslotConfig();
    EXPECT_EQ(result.num_subslots, 3u);
    EXPECT_EQ(result.guard_time_ms, 20u);
    EXPECT_EQ(result.strategy,
              protocols::lora_mesh::SubslotAssignment::HOP_BASED);
}

TEST_F(LoRaMeshConfigTest, GetDiscoverySubslotConfigDefault) {
    const auto& cfg = config_.getDiscoverySubslotConfig();
    EXPECT_EQ(cfg.num_subslots, 5u);
    EXPECT_EQ(cfg.strategy, protocols::lora_mesh::SubslotAssignment::RANDOM);
}

TEST_F(LoRaMeshConfigTest, SetDiscoverySubslotConfig) {
    protocols::lora_mesh::SubslotConfig new_cfg{
        4, 15, protocols::lora_mesh::SubslotAssignment::ADDRESS_MODULO};
    config_.setDiscoverySubslotConfig(new_cfg);
    const auto& result = config_.getDiscoverySubslotConfig();
    EXPECT_EQ(result.num_subslots, 4u);
    EXPECT_EQ(result.guard_time_ms, 15u);
    EXPECT_EQ(result.strategy,
              protocols::lora_mesh::SubslotAssignment::ADDRESS_MODULO);
}

// ---- ProtocolConfig wrapper additional coverage ----

TEST_F(ProtocolConfigWrapperTest, CopyConstructorPingPong) {
    PingPongProtocolConfig pp_cfg(0x1234, 5000, 5);
    ProtocolConfig original;
    original.setPingPongConfig(pp_cfg);

    ProtocolConfig copy(original);
    EXPECT_EQ(copy.getProtocolType(), protocols::ProtocolType::kPingPong);
    EXPECT_EQ(copy.getPingPongConfig().getDefaultTimeout(), 5000u);
    EXPECT_EQ(copy.getPingPongConfig().getRetryCount(), 5u);
}

TEST_F(ProtocolConfigWrapperTest, CopyConstructorLoRaMesh) {
    LoRaMeshProtocolConfig lora_cfg(0x1001);
    lora_cfg.setMaxHops(12);
    ProtocolConfig original;
    original.setLoRaMeshConfig(lora_cfg);

    ProtocolConfig copy(original);
    EXPECT_EQ(copy.getProtocolType(), protocols::ProtocolType::kLoraMesh);
    EXPECT_EQ(copy.getLoRaMeshConfig().getMaxHops(), 12u);
}

TEST_F(ProtocolConfigWrapperTest, CopyAssignmentPingPong) {
    PingPongProtocolConfig pp_cfg(0x5678, 4000, 2);
    ProtocolConfig original;
    original.setPingPongConfig(pp_cfg);

    ProtocolConfig assigned;
    assigned = original;
    EXPECT_EQ(assigned.getProtocolType(), protocols::ProtocolType::kPingPong);
    EXPECT_EQ(assigned.getPingPongConfig().getDefaultTimeout(), 4000u);
}

TEST_F(ProtocolConfigWrapperTest, CopyAssignmentLoRaMesh) {
    LoRaMeshProtocolConfig lora_cfg(0x2002);
    lora_cfg.setMaxHops(7);
    ProtocolConfig original;
    original.setLoRaMeshConfig(lora_cfg);

    ProtocolConfig assigned;
    assigned = original;
    EXPECT_EQ(assigned.getProtocolType(), protocols::ProtocolType::kLoraMesh);
    EXPECT_EQ(assigned.getLoRaMeshConfig().getMaxHops(), 7u);
}

TEST_F(ProtocolConfigWrapperTest, CopyAssignmentSelf) {
    ProtocolConfig cfg = ProtocolConfig::CreateDefault();
    cfg.setNodeAddress(0xABCD);
    // Self-assignment should be a no-op
    cfg = cfg;
    EXPECT_EQ(cfg.getNodeAddress(), 0xABCDu);
}

TEST_F(ProtocolConfigWrapperTest, MoveConstructor) {
    PingPongProtocolConfig pp_cfg(0x9999, 6000, 4);
    ProtocolConfig original;
    original.setPingPongConfig(pp_cfg);

    ProtocolConfig moved(std::move(original));
    EXPECT_EQ(moved.getProtocolType(), protocols::ProtocolType::kPingPong);
    EXPECT_EQ(moved.getPingPongConfig().getDefaultTimeout(), 6000u);
}

TEST_F(ProtocolConfigWrapperTest, MoveAssignment) {
    LoRaMeshProtocolConfig lora_cfg(0x3003);
    lora_cfg.setMaxHops(9);
    ProtocolConfig original;
    original.setLoRaMeshConfig(lora_cfg);

    ProtocolConfig moved;
    moved = std::move(original);
    EXPECT_EQ(moved.getProtocolType(), protocols::ProtocolType::kLoraMesh);
    EXPECT_EQ(moved.getLoRaMeshConfig().getMaxHops(), 9u);
}

TEST_F(ProtocolConfigWrapperTest, MoveAssignmentSelf) {
    ProtocolConfig cfg;
    cfg.setNodeAddress(0x1111);
    // Self-move-assignment should be safe (branch not taken)
    cfg = std::move(cfg);
    // After self-move, just verify it doesn't crash; state may be unspecified
    // but the object should remain in a valid (if unspecified) state.
}

TEST_F(ProtocolConfigWrapperTest, ConstructFromUniquePtr) {
    auto pp_cfg = std::make_unique<PingPongProtocolConfig>(0xAAAA, 7000, 6);
    ProtocolConfig cfg(std::move(pp_cfg));
    EXPECT_EQ(cfg.getProtocolType(), protocols::ProtocolType::kPingPong);
    EXPECT_EQ(cfg.getPingPongConfig().getDefaultTimeout(), 7000u);
}

TEST_F(ProtocolConfigWrapperTest, ConstructFromUniquePtrLoRaMesh) {
    auto lora_cfg = std::make_unique<LoRaMeshProtocolConfig>(0xBBBB);
    lora_cfg->setMaxHops(11);
    ProtocolConfig cfg(std::move(lora_cfg));
    EXPECT_EQ(cfg.getProtocolType(), protocols::ProtocolType::kLoraMesh);
    EXPECT_EQ(cfg.getLoRaMeshConfig().getMaxHops(), 11u);
}

TEST_F(ProtocolConfigWrapperTest, GetPingPongConfigThrowsWhenLoRaMesh) {
    LoRaMeshProtocolConfig lora_cfg;
    ProtocolConfig cfg;
    cfg.setLoRaMeshConfig(lora_cfg);
    EXPECT_THROW(cfg.getPingPongConfig(), std::bad_cast);
}

TEST_F(ProtocolConfigWrapperTest, GetLoRaMeshConfigThrowsWhenPingPong) {
    ProtocolConfig cfg = ProtocolConfig::CreateDefault();
    // Default is PingPong
    EXPECT_THROW(cfg.getLoRaMeshConfig(), std::bad_cast);
}

TEST_F(ProtocolConfigWrapperTest, ValidateReturnsEmptyForValidPingPong) {
    ProtocolConfig cfg = ProtocolConfig::CreateDefault();
    EXPECT_EQ(cfg.Validate(), "");
}

TEST_F(ProtocolConfigWrapperTest, ValidateReturnsEmptyForValidLoRaMesh) {
    LoRaMeshProtocolConfig lora_cfg(0x1001, 60000, 180000, 5, 255, 1, 30000, 50,
                                    50);
    ProtocolConfig cfg;
    cfg.setLoRaMeshConfig(lora_cfg);
    EXPECT_EQ(cfg.Validate(), "");
}

TEST_F(ProtocolConfigWrapperTest, ValidateReturnsErrorForInvalidLoRaMesh) {
    LoRaMeshProtocolConfig lora_cfg;
    lora_cfg.setHelloInterval(4000);  // too short
    lora_cfg.setRouteTimeout(180000);
    ProtocolConfig cfg;
    cfg.setLoRaMeshConfig(lora_cfg);
    EXPECT_FALSE(cfg.IsValid());
    EXPECT_NE(cfg.Validate(), "");
}

TEST_F(ProtocolConfigWrapperTest, IsValidReturnsFalseForInvalidPingPong) {
    PingPongProtocolConfig pp_cfg;
    pp_cfg.setDefaultTimeout(50);  // < 100ms, invalid
    ProtocolConfig cfg;
    cfg.setPingPongConfig(pp_cfg);
    EXPECT_FALSE(cfg.IsValid());
}

}  // namespace test
}  // namespace loramesher