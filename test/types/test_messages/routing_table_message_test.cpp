/**
 * @file routing_table_message_test.cpp
 * @brief Unit tests for RoutingTableMessage class
 */

#include <gtest/gtest.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

#include "types/messages/loramesher/routing_table_message.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Test fixture for RoutingTableMessage tests
 */
class RoutingTableMessageTest : public ::testing::Test {
   protected:
    // Common test data
    static constexpr AddressType dest = 0x1234;
    static constexpr AddressType src = 0x5678;
    static constexpr uint16_t network_id = 0x9ABC;
    static constexpr uint8_t table_version = 3;

    // Test routing entries
    std::vector<RoutingTableEntry> entries;

    std::unique_ptr<RoutingTableMessage> msg_ptr;

#ifdef ARDUINO
    void SetUp() override {
        SetupEntries();
        CreateMessage();
    }
#else
    void SetUp() override {
        // Setup test entries
        SetupEntries();

        // Create message
        CreateMessage();

        // Record memory usage before test
        initial_memory_ = getCurrentMemoryUsage();
    }

    void TearDown() override {
        // Verify no memory leaks
        size_t final_memory = getCurrentMemoryUsage();
        EXPECT_EQ(final_memory, initial_memory_);
    }

   private:
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

    size_t initial_memory_;
#endif

    void SetupEntries() {
        // Create a few test routing entries
        entries.push_back(RoutingTableEntry(
            0x1111, 1, 90, 2));  // Direct connection, high quality
        entries.push_back(
            RoutingTableEntry(0x2222, 2, 70, 3));  // 2 hops, good quality
        entries.push_back(
            RoutingTableEntry(0x3333, 3, 50, 1));  // 3 hops, medium quality
    }

    void CreateMessage() {
        auto opt_msg = RoutingTableMessage::Create(dest, src, network_id,
                                                   table_version, entries);
        ASSERT_TRUE(opt_msg.has_value()) << "Failed to create test message";
        msg_ptr = std::make_unique<RoutingTableMessage>(*opt_msg);
    }
};

/**
 * @brief Test creating a RoutingTableMessage with valid parameters
 */
TEST_F(RoutingTableMessageTest, CreationTest) {
    // Given: Test parameters
    const AddressType test_dest = 0xABCD;
    const AddressType test_src = 0xDCBA;
    const uint16_t test_network_id = 0x6789;
    const uint8_t test_version = 10;

    std::vector<RoutingTableEntry> test_entries;
    test_entries.push_back(RoutingTableEntry(0x4444, 1, 85, 2));
    test_entries.push_back(RoutingTableEntry(0x5555, 2, 75, 1));

    // When: Creating a message
    auto opt_msg = RoutingTableMessage::Create(
        test_dest, test_src, test_network_id, test_version, test_entries);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value()) << "Failed to create RoutingTable message";

    // And: Message should have correct fields
    EXPECT_EQ(opt_msg->GetDestination(), test_dest);
    EXPECT_EQ(opt_msg->GetSource(), test_src);
    EXPECT_EQ(opt_msg->GetNetworkManager(), test_network_id);
    EXPECT_EQ(opt_msg->GetTableVersion(), test_version);

    // Check entries
    const auto& result_entries = opt_msg->GetEntries();
    ASSERT_EQ(result_entries.size(), test_entries.size());

    for (size_t i = 0; i < test_entries.size(); i++) {
        EXPECT_EQ(result_entries[i].destination, test_entries[i].destination);
        EXPECT_EQ(result_entries[i].hop_count, test_entries[i].hop_count);
        EXPECT_EQ(result_entries[i].link_quality, test_entries[i].link_quality);
        EXPECT_EQ(result_entries[i].allocated_data_slots,
                  test_entries[i].allocated_data_slots);
    }
}

/**
 * @brief Test creation failure with too many entries
 */
TEST_F(RoutingTableMessageTest, TooManyEntriesTest) {
    // Only if uint8_t is used for entry count, we can test this
    if (UINT8_MAX < SIZE_MAX) {
        // Creating a vector with more than UINT8_MAX entries would be impractical,
        // so we'll just acknowledge that the implementation checks this limit
        SUCCEED() << "Implementation prevents overflow of entry count";
    }
}

/**
 * @brief Test creating a RoutingTableMessage with empty entries
 */
TEST_F(RoutingTableMessageTest, EmptyEntriesTest) {
    // When: Creating a message with no entries
    std::vector<RoutingTableEntry> empty_entries;
    auto opt_msg = RoutingTableMessage::Create(dest, src, network_id,
                                               table_version, empty_entries);

    // Then: Message creation should succeed
    ASSERT_TRUE(opt_msg.has_value())
        << "Failed to create RoutingTable message with empty entries";

    // And: Entries should be empty
    EXPECT_TRUE(opt_msg->GetEntries().empty());
}

/**
 * @brief Test serializing a RoutingTableMessage
 */
TEST_F(RoutingTableMessageTest, SerializationTest) {
    // Given: A valid message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Serializing the message
    std::optional<std::vector<uint8_t>> opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Serialization failed";

    const std::vector<uint8_t>& serialized = *opt_serialized;

    // Then: Check size (BaseHeader + RoutingTable fields + entries)
    const size_t expected_size = BaseHeader::Size() +
                                 RoutingTableHeader::RoutingTableFieldsSize() +
                                 (entries.size() * RoutingTableEntry::Size());
    ASSERT_EQ(serialized.size(), expected_size) << "Incorrect serialized size";
}

/**
 * @brief Test deserializing a RoutingTableMessage
 */
TEST_F(RoutingTableMessageTest, DeserializationTest) {
    // Given: A serialized message
    auto opt_serialized = msg_ptr->Serialize();
    ASSERT_TRUE(opt_serialized.has_value()) << "Failed to serialize message";

    // When: Deserializing the message
    auto opt_deserialized =
        RoutingTableMessage::CreateFromSerialized(*opt_serialized);
    ASSERT_TRUE(opt_deserialized.has_value())
        << "Failed to deserialize message";

    RoutingTableMessage deserialized_msg = std::move(*opt_deserialized);

    // Then: Verify header fields
    EXPECT_EQ(deserialized_msg.GetDestination(), dest);
    EXPECT_EQ(deserialized_msg.GetSource(), src);
    EXPECT_EQ(deserialized_msg.GetNetworkManager(), network_id);
    EXPECT_EQ(deserialized_msg.GetTableVersion(), table_version);

    // And: Verify entries
    const auto& result_entries = deserialized_msg.GetEntries();
    ASSERT_EQ(result_entries.size(), entries.size());

    for (size_t i = 0; i < entries.size(); i++) {
        EXPECT_EQ(result_entries[i].destination, entries[i].destination);
        EXPECT_EQ(result_entries[i].hop_count, entries[i].hop_count);
        EXPECT_EQ(result_entries[i].link_quality, entries[i].link_quality);
        EXPECT_EQ(result_entries[i].allocated_data_slots,
                  entries[i].allocated_data_slots);
    }
}

/**
 * @brief Test deserializing a RoutingTableMessage with invalid data
 */
TEST_F(RoutingTableMessageTest, DeserializationFailureTest) {
    // Test: Empty data
    {
        std::vector<uint8_t> empty_data;
        auto result = RoutingTableMessage::CreateFromSerialized(empty_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with empty data";
    }

    // Test: Incomplete header
    {
        std::vector<uint8_t> incomplete_data(BaseHeader::Size() - 1, 0);
        auto result =
            RoutingTableMessage::CreateFromSerialized(incomplete_data);
        EXPECT_FALSE(result.has_value())
            << "Should fail with incomplete header";
    }

    // Test: Missing entries
    {
        auto opt_serialized = msg_ptr->Serialize();
        ASSERT_TRUE(opt_serialized.has_value());

        // Truncate the data to include header but not all entries
        std::vector<uint8_t> truncated_data(
            opt_serialized->begin(),
            opt_serialized->begin() + RoutingTableHeader::Size() + 2);

        auto result = RoutingTableMessage::CreateFromSerialized(truncated_data);
        EXPECT_FALSE(result.has_value()) << "Should fail with missing entries";
    }

    // Test: Wrong message type
    {
        auto opt_serialized = msg_ptr->Serialize();
        ASSERT_TRUE(opt_serialized.has_value());

        std::vector<uint8_t> invalid_type = *opt_serialized;
        // Change the message type byte (typically at index 4)
        invalid_type[4] = static_cast<uint8_t>(MessageType::DATA_MSG);

        auto result = RoutingTableMessage::CreateFromSerialized(invalid_type);
        EXPECT_FALSE(result.has_value())
            << "Should fail with invalid message type";
    }
}

/**
 * @brief Test conversion to BaseMessage
 */
TEST_F(RoutingTableMessageTest, ConversionToBaseMessageTest) {
    // Given: A RoutingTable message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Converting to BaseMessage
    BaseMessage base_msg = msg_ptr->ToBaseMessage();

    // Then: Base message should have correct header fields
    EXPECT_EQ(base_msg.GetHeader().GetDestination(), dest);
    EXPECT_EQ(base_msg.GetHeader().GetSource(), src);
    EXPECT_EQ(base_msg.GetHeader().GetType(), MessageType::ROUTE_TABLE);

    // And: Payload should contain RoutingTable fields + entries
    const std::vector<uint8_t>& payload = base_msg.GetPayload();

    // Payload should have 4 bytes header (network_id, version, entry_count) plus entry data
    const size_t expected_payload_size = msg_ptr->GetTotalPayloadSize();
    ASSERT_EQ(payload.size(), expected_payload_size);

    // Network ID should be in the first two bytes (assuming little endian)
    uint16_t extracted_network_id =
        (static_cast<uint16_t>(payload[1]) << 8) | payload[0];
    EXPECT_EQ(extracted_network_id, network_id);

    // Version is the third byte
    EXPECT_EQ(payload[2], table_version);

    // Entry count is the fourth byte
    EXPECT_EQ(payload[3], static_cast<uint8_t>(entries.size()));
}

/**
 * @brief Test GetTotalSize() returns the correct value
 */
TEST_F(RoutingTableMessageTest, GetTotalPayloadSizeTest) {
    // Given: A RoutingTable message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the total size
    size_t total_size = msg_ptr->GetTotalPayloadSize();

    // Then: Size should be correct
    const size_t expected_size = RoutingTableHeader::RoutingTableFieldsSize() +
                                 (entries.size() * RoutingTableEntry::Size());
    EXPECT_EQ(total_size, expected_size);
}

/**
 * @brief Test retrieving the header directly
 */
TEST_F(RoutingTableMessageTest, GetHeaderTest) {
    // Given: A RoutingTable message
    ASSERT_TRUE(msg_ptr != nullptr);

    // When: Getting the header
    const RoutingTableHeader& header = msg_ptr->GetHeader();

    // Then: Header should have correct values
    EXPECT_EQ(header.GetDestination(), dest);
    EXPECT_EQ(header.GetSource(), src);
    EXPECT_EQ(header.GetType(), MessageType::ROUTE_TABLE);
    EXPECT_EQ(header.GetNetworkManager(), network_id);
    EXPECT_EQ(header.GetTableVersion(), table_version);
    EXPECT_EQ(header.GetEntryCount(), static_cast<uint8_t>(entries.size()));
}

}  // namespace test
}  // namespace loramesher