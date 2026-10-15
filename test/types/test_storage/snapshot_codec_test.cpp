/**
 * @file snapshot_codec_test.cpp
 * @brief Tests of the NetworkSnapshot encoding and the RAM state store
 */

#include <gtest/gtest.h>

#include "types/storage/memory_state_store.hpp"
#include "types/storage/snapshot_codec.hpp"

namespace loramesher {
namespace storage {
namespace {

NetworkSnapshot MakeManagerSnapshot() {
    NetworkSnapshot snapshot;
    snapshot.node_address = 0x1000;
    snapshot.was_network_manager = true;
    snapshot.network_id = 0xBEEF;
    snapshot.last_sequence = 200;
    snapshot.network_depth = 3;
    snapshot.superframe_duration_ms = 123456;
    snapshot.reservations = {{0x1001, 1}, {0x1002, 2}, {0x1007, 5}};
    return snapshot;
}

NetworkSnapshot MakeMemberSnapshot() {
    NetworkSnapshot snapshot;
    snapshot.node_address = 0x2001;
    snapshot.network_id = 0x1234;
    snapshot.last_sequence = 7;
    snapshot.network_depth = 2;
    snapshot.superframe_duration_ms = 5000;
    return snapshot;
}

std::vector<uint8_t> EncodeOrFail(const NetworkSnapshot& snapshot) {
    auto blob = SnapshotCodec::Encode(snapshot);
    EXPECT_TRUE(blob.has_value());
    return blob.value_or(std::vector<uint8_t>{});
}

TEST(SnapshotCodecTest, ManagerSnapshotRoundTrips) {
    const NetworkSnapshot snapshot = MakeManagerSnapshot();
    const auto blob = EncodeOrFail(snapshot);
    EXPECT_EQ(blob.size(), SnapshotCodec::EncodedSize(snapshot));
    EXPECT_EQ(blob.size(), 17u + 3u * 3u + 4u);

    auto decoded = SnapshotCodec::Decode(blob);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, snapshot);
}

TEST(SnapshotCodecTest, MemberSnapshotWithoutReservationsRoundTrips) {
    const NetworkSnapshot snapshot = MakeMemberSnapshot();
    const auto blob = EncodeOrFail(snapshot);
    EXPECT_EQ(blob.size(), 21u);

    auto decoded = SnapshotCodec::Decode(blob);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, snapshot);
}

TEST(SnapshotCodecTest, LayoutIsLittleEndianWithMagicFirst) {
    const auto blob = EncodeOrFail(MakeMemberSnapshot());
    ASSERT_GE(blob.size(), 17u);
    EXPECT_EQ(blob[0], 'L');
    EXPECT_EQ(blob[1], 'M');
    EXPECT_EQ(blob[2], 'S');
    EXPECT_EQ(blob[3], '1');
    EXPECT_EQ(blob[4], SnapshotCodec::kFormatVersion);
    EXPECT_EQ(blob[5], 0u);  // Flags: not a network manager
    EXPECT_EQ(blob[6], 0x01);
    EXPECT_EQ(blob[7], 0x20);
    EXPECT_EQ(blob[8], 0x34);
    EXPECT_EQ(blob[9], 0x12);
}

TEST(SnapshotCodecTest, LargestSnapshotRoundTrips) {
    NetworkSnapshot snapshot = MakeManagerSnapshot();
    snapshot.reservations.clear();
    for (size_t i = 1; i <= SnapshotCodec::kMaxReservations; ++i) {
        snapshot.reservations.push_back(
            {static_cast<AddressType>(0x3000 + i), static_cast<uint8_t>(i)});
    }
    auto decoded = SnapshotCodec::Decode(EncodeOrFail(snapshot));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, snapshot);
}

TEST(SnapshotCodecTest, EveryFlippedBitIsRejected) {
    const auto blob = EncodeOrFail(MakeManagerSnapshot());
    for (size_t byte = 0; byte < blob.size(); ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            auto corrupted = blob;
            corrupted[byte] ^= static_cast<uint8_t>(1u << bit);
            EXPECT_FALSE(SnapshotCodec::Decode(corrupted).has_value())
                << "byte " << byte << " bit " << bit;
        }
    }
}

TEST(SnapshotCodecTest, TruncatedOrExtendedBlobIsRejected) {
    const auto blob = EncodeOrFail(MakeManagerSnapshot());
    for (size_t size = 0; size < blob.size(); ++size) {
        std::vector<uint8_t> truncated(blob.begin(), blob.begin() + size);
        EXPECT_FALSE(SnapshotCodec::Decode(truncated).has_value())
            << "size " << size;
    }
    auto extended = blob;
    extended.push_back(0);
    EXPECT_FALSE(SnapshotCodec::Decode(extended).has_value());
}

/// Re-encode a snapshot field by field so tests can craft blobs Encode()
/// refuses to produce, with a valid CRC.
std::vector<uint8_t> EncodeUnchecked(const NetworkSnapshot& snapshot,
                                     uint8_t version, uint8_t flags) {
    std::vector<uint8_t> blob = {
        'L',
        'M',
        'S',
        '1',
        version,
        flags,
        static_cast<uint8_t>(snapshot.node_address),
        static_cast<uint8_t>(snapshot.node_address >> 8),
        static_cast<uint8_t>(snapshot.network_id),
        static_cast<uint8_t>(snapshot.network_id >> 8),
        snapshot.last_sequence,
        snapshot.network_depth,
        0,
        0,
        0,
        0,
        static_cast<uint8_t>(snapshot.reservations.size())};
    for (const auto& reservation : snapshot.reservations) {
        blob.push_back(static_cast<uint8_t>(reservation.address));
        blob.push_back(static_cast<uint8_t>(reservation.address >> 8));
        blob.push_back(reservation.control_slot_index);
    }
    const uint32_t crc = SnapshotCodec::Crc32(blob);
    for (int shift = 0; shift < 32; shift += 8) {
        blob.push_back(static_cast<uint8_t>(crc >> shift));
    }
    return blob;
}

TEST(SnapshotCodecTest, CraftedBlobWithValidCrcIsAccepted) {
    NetworkSnapshot snapshot = MakeManagerSnapshot();
    snapshot.superframe_duration_ms = 0;
    auto decoded = SnapshotCodec::Decode(EncodeUnchecked(snapshot, 1, 1));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, snapshot);
}

TEST(SnapshotCodecTest, OtherFormatVersionIsRejected) {
    NetworkSnapshot snapshot = MakeManagerSnapshot();
    snapshot.superframe_duration_ms = 0;
    EXPECT_FALSE(
        SnapshotCodec::Decode(EncodeUnchecked(snapshot, 2, 1)).has_value());
}

TEST(SnapshotCodecTest, UnknownFlagIsRejected) {
    NetworkSnapshot snapshot = MakeManagerSnapshot();
    snapshot.superframe_duration_ms = 0;
    EXPECT_FALSE(
        SnapshotCodec::Decode(EncodeUnchecked(snapshot, 1, 0x03)).has_value());
}

struct InvalidSnapshotCase {
    const char* label;
    NetworkSnapshot snapshot;
};

std::vector<InvalidSnapshotCase> InvalidSnapshots() {
    std::vector<InvalidSnapshotCase> cases;
    auto with = [](auto mutate) {
        NetworkSnapshot snapshot = MakeManagerSnapshot();
        snapshot.superframe_duration_ms = 0;
        mutate(snapshot);
        return snapshot;
    };
    cases.push_back({"NodeAddressZero",
                     with([](NetworkSnapshot& s) { s.node_address = 0; })});
    cases.push_back({"NodeAddressBroadcast", with([](NetworkSnapshot& s) {
                         s.node_address = kBroadcastAddress;
                     })});
    cases.push_back({"MemberWithReservations", with([](NetworkSnapshot& s) {
                         s.was_network_manager = false;
                     })});
    cases.push_back({"ReservationForSelf", with([](NetworkSnapshot& s) {
                         s.reservations.push_back({s.node_address, 9});
                     })});
    cases.push_back({"ReservationForAddressZero", with([](NetworkSnapshot& s) {
                         s.reservations.push_back({0, 9});
                     })});
    cases.push_back({"ReservationForBroadcast", with([](NetworkSnapshot& s) {
                         s.reservations.push_back({kBroadcastAddress, 9});
                     })});
    cases.push_back({"ReservationOfManagerSlot", with([](NetworkSnapshot& s) {
                         s.reservations.push_back({0x1009, 0});
                     })});
    cases.push_back({"ReservationOfNoSlot", with([](NetworkSnapshot& s) {
                         s.reservations.push_back({0x1009, 0xFF});
                     })});
    cases.push_back({"DuplicateSlot", with([](NetworkSnapshot& s) {
                         s.reservations.push_back({0x1009, 1});
                     })});
    cases.push_back({"DuplicateAddress", with([](NetworkSnapshot& s) {
                         s.reservations.push_back({0x1001, 9});
                     })});
    return cases;
}

TEST(SnapshotCodecTest, InconsistentContentIsRejected) {
    for (const auto& invalid : InvalidSnapshots()) {
        SCOPED_TRACE(invalid.label);
        EXPECT_FALSE(SnapshotCodec::IsValid(invalid.snapshot));
        EXPECT_FALSE(SnapshotCodec::Encode(invalid.snapshot).has_value());
        const uint8_t flags = invalid.snapshot.was_network_manager ? 1 : 0;
        EXPECT_FALSE(
            SnapshotCodec::Decode(EncodeUnchecked(invalid.snapshot, 1, flags))
                .has_value());
    }
}

TEST(SnapshotCodecTest, TooManyReservationsAreRejected) {
    NetworkSnapshot snapshot = MakeManagerSnapshot();
    snapshot.reservations.clear();
    for (size_t i = 0; i <= SnapshotCodec::kMaxReservations; ++i) {
        snapshot.reservations.push_back(
            {static_cast<AddressType>(0x3000 + i), static_cast<uint8_t>(i)});
    }
    EXPECT_FALSE(SnapshotCodec::Encode(snapshot).has_value());
}

TEST(SnapshotCodecTest, Crc32MatchesTheStandardCheckValue) {
    const std::vector<uint8_t> check = {'1', '2', '3', '4', '5',
                                        '6', '7', '8', '9'};
    EXPECT_EQ(SnapshotCodec::Crc32(check), 0xCBF43926u);
    EXPECT_EQ(SnapshotCodec::Crc32({}), 0u);
}

TEST(MemoryStateStoreTest, StartsEmpty) {
    MemoryStateStore store;
    EXPECT_FALSE(store.Load().has_value());
    EXPECT_EQ(store.GetSaveCount(), 0u);
}

TEST(MemoryStateStoreTest, SaveReplacesAndClearErases) {
    MemoryStateStore store;
    const std::vector<uint8_t> first = {1, 2, 3};
    const std::vector<uint8_t> second = {9};

    ASSERT_TRUE(store.Save(first));
    ASSERT_TRUE(store.Save(second));
    EXPECT_EQ(store.Load(), std::optional<std::vector<uint8_t>>(second));
    EXPECT_EQ(store.GetSaveCount(), 2u);

    // Loading does not consume the blob
    EXPECT_TRUE(store.Load().has_value());

    store.Clear();
    EXPECT_FALSE(store.Load().has_value());
}

}  // namespace
}  // namespace storage
}  // namespace loramesher
