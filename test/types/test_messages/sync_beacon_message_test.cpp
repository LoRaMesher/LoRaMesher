/**
 * @file sync_beacon_message_test.cpp
 * @brief Unit tests for SyncBeaconMessage class
 */

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

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
    static constexpr uint8_t max_hops = 5;

    std::optional<SyncBeaconMessage> original_msg;
    std::optional<SyncBeaconMessage> forwarded_msg;

    void SetUp() override { CreateMessages(); }

    void CreateMessages() {
        // Create original sync beacon
        original_msg = SyncBeaconMessage::CreateOriginal(
            dest, src, network_id, total_slots, slot_duration_ms, src,
            50,  // Guard time 50ms
            max_hops);

        // Create forwarded sync beacon
        forwarded_msg = SyncBeaconMessage::CreateForwarded(
            dest, 0x5678,  // Different forwarding node
            network_id, total_slots, slot_duration_ms, src,
            2,    // Hop count 2
            100,  // 100ms propagation delay
            50,   // Guard time 50ms
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
    EXPECT_EQ(original_msg->GetPropagationDelay(),
              50);  // Guard time added to propagation delay
    EXPECT_EQ(original_msg->GetMaxHops(), max_hops);

    // Verify node count defaults to 1
    EXPECT_EQ(original_msg->GetNodeCount(), 1);

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
    EXPECT_EQ(forwarded_msg->GetPropagationDelay(),
              150);  // 100ms + 50ms guard time

    // Verify not original beacon
    EXPECT_FALSE(forwarded_msg->IsOriginalBeacon());
}

/**
 * @brief Test creating forwarded beacon from original
 */
TEST_F(SyncBeaconMessageTest, CreateForwardedFromOriginal) {
    ASSERT_TRUE(original_msg.has_value());

    AddressType forwarding_node = 0x9999;
    uint32_t processing_delay = 25;

    uint32_t guard_time_ms = 50;
    auto forwarded_opt = original_msg->CreateForwardedBeacon(
        forwarding_node, processing_delay, guard_time_ms);

    ASSERT_TRUE(forwarded_opt.has_value());

    // Verify forwarding node is the new source
    EXPECT_EQ(forwarded_opt->GetSource(), forwarding_node);

    // Verify hop count incremented
    EXPECT_EQ(forwarded_opt->GetHopCount(), 1);

    // Verify propagation delay updated (original 50ms + processing 25ms)
    EXPECT_EQ(forwarded_opt->GetPropagationDelay(), 50 + processing_delay);
}

/**
 * @brief Test timing calculation
 */
TEST_F(SyncBeaconMessageTest, TimingCalculation) {
    ASSERT_TRUE(forwarded_msg.has_value());

    uint32_t reception_time = 12346000;  // 322ms after original timestamp
    uint32_t calculated_original =
        forwarded_msg->CalculateOriginalTiming(reception_time);

    // Should compensate for the 150ms propagation delay (100ms + 50ms guard time)
    uint32_t expected_original = reception_time - 150;
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
    EXPECT_EQ(deserialized_opt->GetPropagationDelay(),
              original_msg->GetPropagationDelay());
    EXPECT_EQ(deserialized_opt->GetMaxHops(), original_msg->GetMaxHops());
    EXPECT_EQ(deserialized_opt->GetNodeCount(), original_msg->GetNodeCount());
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
    EXPECT_EQ(base_msg.GetPayload().size(),
              SyncBeaconHeader::SyncBeaconFieldsSize());
}

/**
 * @brief Test node_count field with custom value
 */
TEST_F(SyncBeaconMessageTest, NodeCountField) {
    // Create beacon with custom node_count
    auto msg = SyncBeaconMessage::CreateOriginal(
        dest, src, network_id, total_slots, slot_duration_ms, src, 50, max_hops,
        5);  // node_count = 5

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->GetNodeCount(), 5);

    // Verify round-trip preserves node_count
    auto serialized = msg->Serialize();
    ASSERT_TRUE(serialized.has_value());
    auto deserialized = SyncBeaconMessage::CreateFromSerialized(*serialized);
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(deserialized->GetNodeCount(), 5);

    // Verify forwarded beacon preserves node_count
    auto forwarded = msg->CreateForwardedBeacon(0x9999, 25, 50);
    ASSERT_TRUE(forwarded.has_value());
    EXPECT_EQ(forwarded->GetNodeCount(), 5);
}

/**
 * @brief Test invalid parameter handling
 */
TEST_F(SyncBeaconMessageTest, InvalidParameters) {
    // Test invalid total slots
    auto invalid_msg =
        SyncBeaconMessage::CreateOriginal(dest, src, network_id,
                                          0,  // Invalid: zero slots
                                          slot_duration_ms, src, 50, max_hops);

    EXPECT_FALSE(invalid_msg.has_value());

    // Test invalid slot duration
    auto invalid_duration_msg =
        SyncBeaconMessage::CreateOriginal(dest, src, network_id, total_slots,
                                          0,  // Invalid: zero slot duration
                                          src, 50, max_hops);

    EXPECT_FALSE(invalid_duration_msg.has_value());

    // Test forwarded beacon with hop count exceeding max
    auto invalid_forwarded = SyncBeaconMessage::CreateForwarded(
        dest, 0x5678, network_id, total_slots, slot_duration_ms, src,
        10,  // Hop count exceeds max_hops (5)
        100, 50, max_hops);

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

/**
 * @brief Test the 6-arg SyncBeaconHeader constructor directly
 */
TEST_F(SyncBeaconMessageTest, DirectHeaderConstructorOriginal) {
    // Construct a SyncBeaconHeader directly using the 6-arg constructor
    SyncBeaconHeader header(dest, src, network_id, total_slots,
                            slot_duration_ms, src);

    // Verify sync fields
    EXPECT_EQ(header.GetDestination(), dest);
    EXPECT_EQ(header.GetSource(), src);
    EXPECT_EQ(header.GetNetworkId(), network_id);
    EXPECT_EQ(header.GetTotalSlots(), total_slots);
    EXPECT_EQ(header.GetSlotDuration(), slot_duration_ms);
    EXPECT_EQ(header.GetNetworkManager(), src);

    // Verify defaults set by the 6-arg constructor
    EXPECT_EQ(header.GetHopCount(), 1);
    EXPECT_EQ(header.GetPropagationDelay(), 0u);
    EXPECT_EQ(header.GetMaxHops(), 5);
}

/**
 * @brief Test SetSyncInfo with valid parameters updates fields and returns success
 */
TEST_F(SyncBeaconMessageTest, SetSyncInfoSuccess) {
    SyncBeaconHeader header(dest, src, network_id, total_slots,
                            slot_duration_ms, src);

    Result result = header.SetSyncInfo(2, 10, 100);

    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(header.GetNetworkId(), 2u);
    EXPECT_EQ(header.GetTotalSlots(), 10u);
    EXPECT_EQ(header.GetSlotDuration(), 100u);
}

/**
 * @brief Test SetSyncInfo with zero total_slots returns kInvalidParameter
 */
TEST_F(SyncBeaconMessageTest, SetSyncInfoZeroSlotsFails) {
    SyncBeaconHeader header(dest, src, network_id, total_slots,
                            slot_duration_ms, src);

    Result result = header.SetSyncInfo(1, 0, 100);

    EXPECT_FALSE(result.IsSuccess());
}

/**
 * @brief Test SetSyncInfo with zero slot_duration_ms returns error
 */
TEST_F(SyncBeaconMessageTest, SetSyncInfoZeroDurationFails) {
    SyncBeaconHeader header(dest, src, network_id, total_slots,
                            slot_duration_ms, src);

    Result result = header.SetSyncInfo(1, 10, 0);

    EXPECT_FALSE(result.IsSuccess());
}

/**
 * @brief Test SetForwardingInfo with valid parameters returns success and updates fields
 */
TEST_F(SyncBeaconMessageTest, SetForwardingInfoSuccess) {
    SyncBeaconHeader header(dest, src, network_id, total_slots,
                            slot_duration_ms, src);

    Result result = header.SetForwardingInfo(2, 500, 5);

    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(header.GetHopCount(), 2u);
    EXPECT_EQ(header.GetPropagationDelay(), 500u);
    EXPECT_EQ(header.GetMaxHops(), 5u);
}

/**
 * @brief Test SetForwardingInfo where hop_count exceeds max_hops returns error
 */
TEST_F(SyncBeaconMessageTest, SetForwardingInfoHopExceedsMaxFails) {
    SyncBeaconHeader header(dest, src, network_id, total_slots,
                            slot_duration_ms, src);

    // hop_count (6) > max_hops (5) — should fail
    Result result = header.SetForwardingInfo(6, 500, 5);

    EXPECT_FALSE(result.IsSuccess());
}

/**
 * @brief Test that deserialization fails when data is truncated to just a few bytes
 */
TEST_F(SyncBeaconMessageTest, DeserializeFromTruncatedDataFails) {
    ASSERT_TRUE(original_msg.has_value());

    // Serialize a valid message first
    auto serialized_opt = original_msg->Serialize();
    ASSERT_TRUE(serialized_opt.has_value());

    // Truncate to 3 bytes — too short for even the BaseHeader (6 bytes)
    std::vector<uint8_t> truncated(serialized_opt->begin(),
                                   serialized_opt->begin() + 3);

    auto result = SyncBeaconMessage::CreateFromSerialized(truncated);
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// GetHeader() const accessor (line 138-140)
// =============================================================================

/**
 * @brief Test GetHeader() const returns a reference to the internal header
 */
TEST_F(SyncBeaconMessageTest, GetHeaderReturnsCorrectHeader) {
    ASSERT_TRUE(original_msg.has_value());

    // Call the const GetHeader() accessor (covers lines 138-140)
    const SyncBeaconHeader& h = original_msg->GetHeader();

    EXPECT_EQ(h.GetSource(), src);
    EXPECT_EQ(h.GetDestination(), dest);
    EXPECT_EQ(h.GetNetworkId(), network_id);
    EXPECT_EQ(h.GetTotalSlots(), total_slots);
    EXPECT_EQ(h.GetSlotDuration(), slot_duration_ms);
}

// =============================================================================
// ToBaseMessage() and Serialize() error branches
// These are hard to reach (require serializer failures) but the happy-path
// variants above don't call GetHeader(), so we add a minimal additional test
// to be sure GetHeader() on the forwarded beacon also works.
// =============================================================================

/**
 * @brief Test GetHeader() on a forwarded beacon returns correct forwarding fields
 */
TEST_F(SyncBeaconMessageTest,
       GetHeaderOnForwardedBeaconReturnsForwardingFields) {
    ASSERT_TRUE(forwarded_msg.has_value());

    const SyncBeaconHeader& h = forwarded_msg->GetHeader();
    EXPECT_EQ(h.GetHopCount(), 2u);
    EXPECT_EQ(h.GetPropagationDelay(), 150u);  // 100 + 50 guard
    EXPECT_EQ(h.GetMaxHops(), max_hops);
}

// =============================================================================
// MutablePayload direct-write tests
// =============================================================================

/**
 * @brief Round-trip validation that the offset constant is correct
 */
TEST_F(SyncBeaconMessageTest, MutablePayloadDirectWrite) {
    ASSERT_TRUE(original_msg.has_value());
    auto base_msg = original_msg->ToBaseMessage();

    uint32_t new_delay = 12345;
    auto payload = base_msg.MutablePayload();
    constexpr size_t kOffset = SyncBeaconHeader::kPropagationDelayPayloadOffset;
    ASSERT_GE(payload.size(), kOffset + sizeof(uint32_t));
    std::memcpy(payload.data() + kOffset, &new_delay, sizeof(uint32_t));

    auto serialized = base_msg.Serialize();
    ASSERT_TRUE(serialized.has_value());
    auto beacon = SyncBeaconMessage::CreateFromSerialized(*serialized);
    ASSERT_TRUE(beacon.has_value());
    EXPECT_EQ(beacon->GetPropagationDelay(), new_delay);
}

/**
 * @brief Ensures writing propagation delay doesn't corrupt adjacent fields
 */
TEST_F(SyncBeaconMessageTest, MutablePayloadPreservesOtherFields) {
    ASSERT_TRUE(forwarded_msg.has_value());
    auto base_msg = forwarded_msg->ToBaseMessage();

    auto orig_serialized = base_msg.Serialize();
    ASSERT_TRUE(orig_serialized.has_value());
    auto orig_beacon =
        SyncBeaconMessage::CreateFromSerialized(*orig_serialized);
    ASSERT_TRUE(orig_beacon.has_value());
    auto orig_network_id = orig_beacon->GetNetworkId();
    auto orig_hop_count = orig_beacon->GetHopCount();
    auto orig_max_hops = orig_beacon->GetMaxHops();
    auto orig_total_slots = orig_beacon->GetTotalSlots();

    uint32_t new_delay = 99999;
    auto payload = base_msg.MutablePayload();
    constexpr size_t kOffset = SyncBeaconHeader::kPropagationDelayPayloadOffset;
    std::memcpy(payload.data() + kOffset, &new_delay, sizeof(uint32_t));

    auto updated_serialized = base_msg.Serialize();
    ASSERT_TRUE(updated_serialized.has_value());
    auto updated_beacon =
        SyncBeaconMessage::CreateFromSerialized(*updated_serialized);
    ASSERT_TRUE(updated_beacon.has_value());
    EXPECT_EQ(updated_beacon->GetPropagationDelay(), new_delay);
    EXPECT_EQ(updated_beacon->GetNetworkId(), orig_network_id);
    EXPECT_EQ(updated_beacon->GetHopCount(), orig_hop_count);
    EXPECT_EQ(updated_beacon->GetMaxHops(), orig_max_hops);
    EXPECT_EQ(updated_beacon->GetTotalSlots(), orig_total_slots);
}

/**
 * @brief Tests the pre-send callback mechanism end-to-end
 */
TEST_F(SyncBeaconMessageTest, PreSendCallbackUpdatesMessage) {
    ASSERT_TRUE(original_msg.has_value());
    auto base_msg = original_msg->ToBaseMessage();

    uint32_t injected_delay = 42;
    base_msg.SetPreSendCallback([&](BaseMessage& msg) {
        auto payload = msg.MutablePayload();
        constexpr size_t kOffset =
            SyncBeaconHeader::kPropagationDelayPayloadOffset;
        std::memcpy(payload.data() + kOffset, &injected_delay,
                    sizeof(uint32_t));
    });

    EXPECT_TRUE(base_msg.InvokePreSendCallback());

    auto serialized = base_msg.Serialize();
    ASSERT_TRUE(serialized.has_value());
    auto beacon = SyncBeaconMessage::CreateFromSerialized(*serialized);
    ASSERT_TRUE(beacon.has_value());
    EXPECT_EQ(beacon->GetPropagationDelay(), injected_delay);
}

}  // namespace test
}  // namespace loramesher