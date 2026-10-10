/**
 * @file address_generator_test.cpp
 * @brief Unit tests for AddressGenerator class
 */

#include <gtest/gtest.h>

#include "utils/address_generator.hpp"

namespace loramesher {
namespace utils {
namespace test {

class AddressGeneratorTest : public ::testing::Test {
   protected:
    // 6-byte MAC-like hardware ID for testing
    static constexpr uint8_t kValidId6[6] = {0x01, 0x02, 0x03,
                                             0x04, 0xAB, 0xCD};
    // Short ID (less than 6 bytes)
    static constexpr uint8_t kShortId3[3] = {0x01, 0x02, 0x03};
    // ID that produces 0x0000 via legacy formula (bytes 4/5 = 0)
    static constexpr uint8_t kIdProducesZero[6] = {0x01, 0x02, 0x03,
                                                   0x04, 0x00, 0x00};
    // ID that produces 0xFFFF via legacy formula
    static constexpr uint8_t kIdProducesFFFF[6] = {0x01, 0x02, 0x03,
                                                   0x04, 0xFF, 0xFF};
};

// ---- IsValidAddress ----

TEST_F(AddressGeneratorTest, IsValidAddressNormalAddress) {
    EXPECT_TRUE(AddressGenerator::IsValidAddress(0x1234, true));
    EXPECT_TRUE(AddressGenerator::IsValidAddress(0x0001, true));
    EXPECT_TRUE(AddressGenerator::IsValidAddress(0xFFFE, true));
}

TEST_F(AddressGeneratorTest, IsValidAddressReservedWithAvoidance) {
    EXPECT_FALSE(AddressGenerator::IsValidAddress(0x0000, true));
    EXPECT_FALSE(AddressGenerator::IsValidAddress(0xFFFF, true));
}

TEST_F(AddressGeneratorTest, IsValidAddressReservedWithoutAvoidance) {
    EXPECT_TRUE(AddressGenerator::IsValidAddress(0x0000, false));
    EXPECT_TRUE(AddressGenerator::IsValidAddress(0xFFFF, false));
}

// ---- GenerateFallback ----

TEST_F(AddressGeneratorTest, GenerateFallbackReturnsValidAddress) {
    AddressGenerator::Config config;
    AddressType addr = AddressGenerator::GenerateFallback(config);
    EXPECT_NE(addr, 0x0000);
    EXPECT_NE(addr, 0xFFFF);
    EXPECT_TRUE(AddressGenerator::IsValidAddress(addr, true));
}

TEST_F(AddressGeneratorTest, GenerateFallbackUpdatesSource) {
    AddressGenerator::Config config;
    AddressGenerator::GenerateFallback(config);
    const char* src = AddressGenerator::GetLastGenerationSource();
    ASSERT_NE(src, nullptr);
    EXPECT_NE(std::string(src), "");
}

// ---- GenerateFromHardwareId — null/empty inputs ----

TEST_F(AddressGeneratorTest, NullHardwareIdReturnsFallback) {
    AddressGenerator::Config config;
    AddressType addr =
        AddressGenerator::GenerateFromHardwareId(nullptr, 6, config);
    EXPECT_TRUE(AddressGenerator::IsValidAddress(addr, true));
    EXPECT_NE(addr, 0x0000);
    EXPECT_NE(addr, 0xFFFF);
}

TEST_F(AddressGeneratorTest, ZeroLengthHardwareIdReturnsFallback) {
    AddressGenerator::Config config;
    AddressType addr =
        AddressGenerator::GenerateFromHardwareId(kValidId6, 0, config);
    EXPECT_TRUE(AddressGenerator::IsValidAddress(addr, true));
}

// ---- GenerateFromHardwareId — hardware_id disabled ----

TEST_F(AddressGeneratorTest, HardwareIdDisabledUsesFallback) {
    AddressGenerator::Config config;
    config.use_hardware_id = false;
    AddressType addr =
        AddressGenerator::GenerateFromHardwareId(kValidId6, 6, config);
    EXPECT_TRUE(AddressGenerator::IsValidAddress(addr, true));
}

// ---- Legacy MAC formula ----

TEST_F(AddressGeneratorTest, LegacyMacFormulaUsesBytes4And5) {
    AddressGenerator::Config config;
    config.use_legacy_mac_formula = true;
    config.avoid_reserved_addresses = true;
    // Disable unicast-range restriction to verify the raw legacy formula output.
    config.restrict_to_unicast_range = false;

    AddressType addr =
        AddressGenerator::GenerateFromHardwareId(kValidId6, 6, config);
    // Legacy: (mac[4]<<8) | mac[5] = 0xABCD
    EXPECT_EQ(addr, 0xABCD);

    const char* src = AddressGenerator::GetLastGenerationSource();
    EXPECT_NE(std::string(src).find("Legacy"), std::string::npos);
}

TEST_F(AddressGeneratorTest, LegacyFormulaShortIdFallsBack) {
    AddressGenerator::Config config;
    config.use_legacy_mac_formula = true;

    // 3-byte ID is too short for legacy formula
    AddressType addr =
        AddressGenerator::GenerateFromHardwareId(kShortId3, 3, config);
    // Should return a valid fallback address
    EXPECT_TRUE(AddressGenerator::IsValidAddress(addr, true));
}

TEST_F(AddressGeneratorTest, LegacyFormulaAvoidsReservedZero) {
    AddressGenerator::Config config;
    config.use_legacy_mac_formula = true;
    config.avoid_reserved_addresses = true;

    // bytes 4+5 = 0x0000 → should be remapped to 0x0001
    AddressType addr =
        AddressGenerator::GenerateFromHardwareId(kIdProducesZero, 6, config);
    EXPECT_EQ(addr, 0x0001);
}

TEST_F(AddressGeneratorTest, LegacyFormulaAvoidsReservedFFFF) {
    AddressGenerator::Config config;
    config.use_legacy_mac_formula = true;
    config.avoid_reserved_addresses = true;
    // Disable unicast-range restriction to verify the avoid-reserved remap.
    config.restrict_to_unicast_range = false;

    // bytes 4+5 = 0xFFFF → should be remapped to 0xFFFE
    AddressType addr =
        AddressGenerator::GenerateFromHardwareId(kIdProducesFFFF, 6, config);
    EXPECT_EQ(addr, 0xFFFE);
}

// ---- Address space partition helpers ----

TEST_F(AddressGeneratorTest, AddressSpacePartitionBoundaries) {
    EXPECT_FALSE(IsUnicastAddress(0x0000));
    EXPECT_FALSE(IsGroupAddress(0x0000));

    EXPECT_TRUE(IsUnicastAddress(0x0001));
    EXPECT_TRUE(IsUnicastAddress(0x7FFF));
    EXPECT_FALSE(IsGroupAddress(0x7FFF));

    EXPECT_TRUE(IsGroupAddress(0x8000));
    EXPECT_TRUE(IsGroupAddress(0xFFFE));
    EXPECT_FALSE(IsUnicastAddress(0x8000));

    EXPECT_FALSE(IsGroupAddress(0xFFFF));
    EXPECT_FALSE(IsUnicastAddress(0xFFFF));
}

// ---- Unicast-range restriction (default) ----

TEST_F(AddressGeneratorTest, LegacyFormulaRestrictsToUnicastRangeByDefault) {
    AddressGenerator::Config config;
    config.use_legacy_mac_formula = true;
    // Default config restricts node addresses to the unicast range.

    // kValidId6 → raw legacy 0xABCD which lies in the group range; the
    // generator must fold it back into the unicast range 0x0001-0x7FFF.
    AddressType addr =
        AddressGenerator::GenerateFromHardwareId(kValidId6, 6, config);
    EXPECT_TRUE(IsUnicastAddress(addr))
        << "Generated 0x" << std::hex << addr << " is not a unicast address";
    EXPECT_FALSE(IsGroupAddress(addr));
}

TEST_F(AddressGeneratorTest, HashFormulaRestrictsToUnicastRangeByDefault) {
    AddressGenerator::Config config;
    config.use_legacy_mac_formula = false;

    static constexpr uint8_t id_group[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    AddressType addr =
        AddressGenerator::GenerateFromHardwareId(id_group, 6, config);
    EXPECT_TRUE(IsUnicastAddress(addr));
}

TEST_F(AddressGeneratorTest, FallbackRestrictsToUnicastRangeByDefault) {
    AddressGenerator::Config config;
    for (int i = 0; i < 10; ++i) {
        AddressType addr = AddressGenerator::GenerateFallback(config);
        EXPECT_TRUE(IsUnicastAddress(addr))
            << "Fallback produced non-unicast 0x" << std::hex << addr;
    }
}

TEST_F(AddressGeneratorTest, IsValidAddressRejectsGroupRangeWhenRestricted) {
    EXPECT_FALSE(AddressGenerator::IsValidAddress(0x8000, true, true));
    EXPECT_FALSE(AddressGenerator::IsValidAddress(0xC000, true, true));
    EXPECT_TRUE(AddressGenerator::IsValidAddress(0x7FFF, true, true));
    // Without restriction, group-range addresses remain valid.
    EXPECT_TRUE(AddressGenerator::IsValidAddress(0x8000, true, false));
}

// ---- Hash-based generation (legacy disabled) ----

TEST_F(AddressGeneratorTest, HashBasedGenerationProducesValidAddress) {
    AddressGenerator::Config config;
    config.use_legacy_mac_formula = false;
    config.avoid_reserved_addresses = true;

    AddressType addr =
        AddressGenerator::GenerateFromHardwareId(kValidId6, 6, config);
    EXPECT_NE(addr, 0x0000);
    EXPECT_NE(addr, 0xFFFF);
}

TEST_F(AddressGeneratorTest, HashBasedGenerationIsDeterministic) {
    AddressGenerator::Config config;
    config.use_legacy_mac_formula = false;

    AddressType addr1 =
        AddressGenerator::GenerateFromHardwareId(kValidId6, 6, config);
    AddressType addr2 =
        AddressGenerator::GenerateFromHardwareId(kValidId6, 6, config);
    EXPECT_EQ(addr1, addr2);
}

TEST_F(AddressGeneratorTest, DifferentInputsProduceDifferentAddresses) {
    AddressGenerator::Config config;
    config.use_legacy_mac_formula = false;

    static constexpr uint8_t id_a[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    static constexpr uint8_t id_b[6] = {0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};

    AddressType addr_a =
        AddressGenerator::GenerateFromHardwareId(id_a, 6, config);
    AddressType addr_b =
        AddressGenerator::GenerateFromHardwareId(id_b, 6, config);
    // Different inputs should (almost certainly) produce different outputs
    // Note: hash collisions are theoretically possible but extremely unlikely
    EXPECT_NE(addr_a, addr_b);
}

// ---- GetLastGenerationSource ----

TEST_F(AddressGeneratorTest, GetLastGenerationSourceNotNull) {
    AddressGenerator::Config config;
    AddressGenerator::GenerateFromHardwareId(kValidId6, 6, config);
    EXPECT_NE(AddressGenerator::GetLastGenerationSource(), nullptr);
}

// ---- AddressMask ----

TEST_F(AddressGeneratorTest, AddressMaskApplied) {
    AddressGenerator::Config config;
    config.use_legacy_mac_formula = false;
    config.avoid_reserved_addresses = false;
    config.address_mask = 0x00FF;  // Only lower 8 bits

    // Run multiple times to check mask is respected
    for (int i = 0; i < 5; ++i) {
        AddressType addr =
            AddressGenerator::GenerateFromHardwareId(kValidId6, 6, config);
        EXPECT_EQ(addr & 0xFF00, 0) << "Address 0x" << std::hex << addr
                                    << " has bits outside mask 0x00FF";
    }
}

}  // namespace test
}  // namespace utils
}  // namespace loramesher

// ---- TaskMonitor coverage ----
#include <atomic>
#include <chrono>
#include <thread>

#include "os/os_port.hpp"
#include "utils/task_monitor.hpp"

namespace loramesher {
namespace utils {
namespace test {

class TaskMonitorTest : public ::testing::Test {};

TEST_F(TaskMonitorTest, MonitorSystemTasksDoesNotCrash) {
    // MonitorSystemTasks exercises the loop over system task stats
    TaskMonitor::MonitorSystemTasks();
}

TEST_F(TaskMonitorTest, MonitorTaskNullHandleDoesNotCrash) {
    // DEBUG path: returns early on null handle; release path: (void)-casts args
    TaskMonitor::MonitorTask(nullptr, "TestTask", 512);
    TaskMonitor::MonitorTask(nullptr, "TestTask2", 0);
}

TEST_F(TaskMonitorTest, MonitorTaskWithValidHandle) {
    // Create a real RTOS task so we can pass a non-null handle
    std::atomic<bool> running{true};
    os::TaskHandle_t handle = nullptr;

    auto task_fn = [](void* param) {
        auto* r = static_cast<std::atomic<bool>*>(param);
        while (r->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    bool created = GetRTOS().CreateTask(task_fn, "MonitorTestTask", 2048,
                                        &running, 1, &handle);
    ASSERT_TRUE(created);
    ASSERT_NE(handle, nullptr);

    // Wait for task to start
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Call MonitorTask — exercises lines 35-46 in DEBUG build
    TaskMonitor::MonitorTask(handle, "MonitorTestTask", 512);

    // Also call with very high min_stack_watermark to trigger log_stack_warning
    TaskMonitor::MonitorTask(handle, "MonitorTestTask", 999999);

    // Stop task
    running.store(false);
    GetRTOS().DeleteTask(handle);
}

}  // namespace test
}  // namespace utils
}  // namespace loramesher
