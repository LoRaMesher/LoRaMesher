/**
 * @file sync_beacon_message_test.cpp
 * @brief Unit tests for SyncBeaconMessage class
 */

#include <gtest/gtest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

#include "types/messages/loramesher/sync_beacon_message.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test fixture for SyncBeaconMessage tests
 */
class SyncBeaconMessageTest : public ::testing::Test {
   protected:
    // Common test data
    static constexpr AddressType dest = 0xFFFF;  // Broadcast
    static constexpr AddressType src = 0x1234;   // Network Manager
    static constexpr uint16_t network_id = 1;
    static constexpr uint8_t total_slots = 20;
    static constexpr uint16_t slot_duration_ms = 50;
    static constexpr uint16_t original_timestamp_ms =
        45678;  // 16-bit timestamp
    static constexpr uint8_t max_hops = 5;

    std::optional<SyncBeaconMessage> original_msg;
    std::optional<SyncBeaconMessage> forwarded_msg;

#ifdef ARDUINO
    void SetUp() override { CreateMessages(); }
#else
    void SetUp() override {
        // Create messages
        CreateMessages();

        // Record memory usage before test
        initial_memory_ = getCurrentMemoryUsage();
    }

    void TearDown() override {
        // Verify no memory leaks
        size_t final_memory = getCurrentMemoryUsage();
        size_t memory_diff = final_memory - initial_memory_;

        // Allow for small differences (1KB threshold)
        if (memory_diff > 1024) {
            GTEST_NONFATAL_FAILURE_(("Memory leak detected: " +
                                     std::to_string(memory_diff) + " bytes")
                                        .c_str());
        }
    }

    size_t getCurrentMemoryUsage() {
#ifdef _WIN32
        MEMORYSTATUSEX memStatus;
        memStatus.dwLength = sizeof(memStatus);
        GlobalMemoryStatusEx(&memStatus);
        return memStatus.ullTotalPhys;
#elif defined(__linux__)
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        return usage.ru_maxrss;
#else
        return 0;
#endif
    }

   private:
    size_t initial_memory_;
#endif

    void CreateMessages() {
        // Create original sync beacon
        original_msg = SyncBeaconMessage::CreateOriginal(
            dest, src, network_id, total_slots, slot_duration_ms,
            original_timestamp_ms, max_hops);

        // Create forwarded sync beacon
        forwarded_msg = SyncBeaconMessage::CreateForwarded(
            dest, 0x5678,  // Different forwarding node
            network_id, total_slots, slot_duration_ms,
            2,                           // Hop count 2
            original_timestamp_ms, 100,  // 100ms propagation delay
            max_hops);
    }
};

/**
 * @brief Test original sync beacon creation
 */
TEST_F(SyncBeaconMessageTest, CreateOriginalBeacon) {
    ASSERT_TRUE(original_msg.has_value());

    // Verify basic fields
    EXPECT_EQ(original_msg->GetSource(), src);
    EXPECT_EQ(original_msg->GetDestination(), dest);
    EXPECT_EQ(original_msg->GetNetworkId(), network_id);
    EXPECT_EQ(original_msg->GetTotalSlots(), total_slots);
    EXPECT_EQ(original_msg->GetSlotDuration(), slot_duration_ms);

    // Verify calculated superframe duration
    EXPECT_EQ(original_msg->GetSuperframeDuration(),
              total_slots * slot_duration_ms);

    // Verify forwarding fields for original beacon
    EXPECT_EQ(original_msg->GetHopCount(), 0);  // Original beacon
    EXPECT_EQ(original_msg->GetOriginalTimestamp(), original_timestamp_ms);
    EXPECT_EQ(original_msg->GetPropagationDelay(), 0);  // No delay for original
    EXPECT_EQ(original_msg->GetMaxHops(), max_hops);

    // Verify original beacon check
    EXPECT_TRUE(original_msg->IsOriginalBeacon());
}

/**
 * @brief Test forwarded sync beacon creation
 */
TEST_F(SyncBeaconMessageTest, CreateForwardedBeacon) {
    ASSERT_TRUE(forwarded_msg.has_value());

    // Verify basic fields (should match original)
    EXPECT_EQ(forwarded_msg->GetNetworkId(), network_id);
    EXPECT_EQ(forwarded_msg->GetTotalSlots(), total_slots);
    EXPECT_EQ(forwarded_msg->GetSlotDuration(), slot_duration_ms);

    // Verify calculated superframe duration
    EXPECT_EQ(forwarded_msg->GetSuperframeDuration(),
              total_slots * slot_duration_ms);

    // Verify forwarding fields
    EXPECT_EQ(forwarded_msg->GetHopCount(), 2);
    EXPECT_EQ(forwarded_msg->GetOriginalTimestamp(), original_timestamp_ms);
    EXPECT_EQ(forwarded_msg->GetPropagationDelay(), 100);

    // Verify not original beacon
    EXPECT_FALSE(forwarded_msg->IsOriginalBeacon());
}

/**
 * @brief Test forwarding decision logic
 */
TEST_F(SyncBeaconMessageTest, ForwardingDecisionLogic) {
    ASSERT_TRUE(original_msg.has_value());
    ASSERT_TRUE(forwarded_msg.has_value());

    // Original beacon should be forwarded by nodes at hop 1
    EXPECT_TRUE(original_msg->ShouldBeForwardedBy(1));

    // Original beacon should not be forwarded by nodes at hop 0 (NM itself)
    EXPECT_FALSE(original_msg->ShouldBeForwardedBy(0));

    // Forwarded beacon (hop 2) should be forwarded by nodes at hop 3
    EXPECT_TRUE(forwarded_msg->ShouldBeForwardedBy(3));

    // Forwarded beacon should not be forwarded by nodes at wrong hops
    EXPECT_FALSE(forwarded_msg->ShouldBeForwardedBy(1));
    EXPECT_FALSE(forwarded_msg->ShouldBeForwardedBy(2));
}

/**
 * @brief Test creating forwarded beacon from original
 */
TEST_F(SyncBeaconMessageTest, CreateForwardedFromOriginal) {
    ASSERT_TRUE(original_msg.has_value());

    AddressType forwarding_node = 0x9999;
    uint32_t processing_delay = 25;

    auto forwarded_opt =
        original_msg->CreateForwardedBeacon(forwarding_node, processing_delay);

    ASSERT_TRUE(forwarded_opt.has_value());

    // Verify forwarding node is the new source
    EXPECT_EQ(forwarded_opt->GetSource(), forwarding_node);

    // Verify hop count incremented
    EXPECT_EQ(forwarded_opt->GetHopCount(), 1);

    // Verify original fields preserved
    EXPECT_EQ(forwarded_opt->GetOriginalTimestamp(), original_timestamp_ms);

    // Verify propagation delay updated
    EXPECT_EQ(forwarded_opt->GetPropagationDelay(), processing_delay);
}

/**
 * @brief Test timing calculation
 */
TEST_F(SyncBeaconMessageTest, TimingCalculation) {
    ASSERT_TRUE(forwarded_msg.has_value());

    uint32_t reception_time = 12346000;  // 322ms after original timestamp
    uint32_t calculated_original =
        forwarded_msg->CalculateOriginalTiming(reception_time);

    // Should compensate for the 100ms propagation delay
    uint32_t expected_original = reception_time - 100;
    EXPECT_EQ(calculated_original, expected_original);
}

/**
 * @brief Test serialization and deserialization
 */
TEST_F(SyncBeaconMessageTest, SerializationRoundTrip) {
    ASSERT_TRUE(original_msg.has_value());

    // Serialize the message
    auto serialized_opt = original_msg->Serialize();
    ASSERT_TRUE(serialized_opt.has_value());

    const auto& serialized = serialized_opt.value();
    EXPECT_GT(serialized.size(), 0);

    // Deserialize back
    auto deserialized_opt = SyncBeaconMessage::CreateFromSerialized(serialized);
    ASSERT_TRUE(deserialized_opt.has_value());

    // Verify all fields match
    EXPECT_EQ(deserialized_opt->GetSource(), original_msg->GetSource());
    EXPECT_EQ(deserialized_opt->GetDestination(),
              original_msg->GetDestination());
    EXPECT_EQ(deserialized_opt->GetNetworkId(), original_msg->GetNetworkId());
    EXPECT_EQ(deserialized_opt->GetSuperframeDuration(),
              original_msg->GetSuperframeDuration());
    EXPECT_EQ(deserialized_opt->GetTotalSlots(), original_msg->GetTotalSlots());
    EXPECT_EQ(deserialized_opt->GetSlotDuration(),
              original_msg->GetSlotDuration());
    EXPECT_EQ(deserialized_opt->GetHopCount(), original_msg->GetHopCount());
    EXPECT_EQ(deserialized_opt->GetOriginalTimestamp(),
              original_msg->GetOriginalTimestamp());
    EXPECT_EQ(deserialized_opt->GetPropagationDelay(),
              original_msg->GetPropagationDelay());
    EXPECT_EQ(deserialized_opt->GetMaxHops(), original_msg->GetMaxHops());
}

/**
 * @brief Test BaseMessage conversion
 */
TEST_F(SyncBeaconMessageTest, BaseMessageConversion) {
    ASSERT_TRUE(original_msg.has_value());

    BaseMessage base_msg = original_msg->ToBaseMessage();

    // Verify message type
    EXPECT_EQ(base_msg.GetType(), MessageType::SYNC_BEACON);

    // Verify addresses
    EXPECT_EQ(base_msg.GetSource(), src);
    EXPECT_EQ(base_msg.GetDestination(), dest);

    // Verify payload is empty (sync beacons store all data in header)
    EXPECT_EQ(base_msg.GetPayload().size(), 0);
}

/**
 * @brief Test invalid parameter handling
 */
TEST_F(SyncBeaconMessageTest, InvalidParameters) {
    // Test invalid total slots
    auto invalid_msg = SyncBeaconMessage::CreateOriginal(
        dest, src, network_id,
        0,  // Invalid: zero slots
        slot_duration_ms, original_timestamp_ms, max_hops);

    EXPECT_FALSE(invalid_msg.has_value());

    // Test invalid slot duration
    auto invalid_duration_msg =
        SyncBeaconMessage::CreateOriginal(dest, src, network_id, total_slots,
                                          0,  // Invalid: zero slot duration
                                          original_timestamp_ms, max_hops);

    EXPECT_FALSE(invalid_duration_msg.has_value());

    // Test forwarded beacon with hop count exceeding max
    auto invalid_forwarded = SyncBeaconMessage::CreateForwarded(
        dest, 0x5678, network_id, total_slots, slot_duration_ms,
        10,  // Hop count exceeds max_hops (5)
        original_timestamp_ms, 100, max_hops);

    EXPECT_FALSE(invalid_forwarded.has_value());
}

/**
 * @brief Test malformed serialized data handling
 */
TEST_F(SyncBeaconMessageTest, MalformedSerializedData) {
    // Test with empty data
    std::vector<uint8_t> empty_data;
    auto empty_result = SyncBeaconMessage::CreateFromSerialized(empty_data);
    EXPECT_FALSE(empty_result.has_value());

    // Test with insufficient data
    std::vector<uint8_t> short_data{0x01, 0x02, 0x03};
    auto short_result = SyncBeaconMessage::CreateFromSerialized(short_data);
    EXPECT_FALSE(short_result.has_value());
}

}  // namespace test
}  // namespace loramesher