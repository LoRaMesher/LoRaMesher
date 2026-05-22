/**
 * @file routing_table_rotation_test.cpp
 * @brief Unit tests for routing-table broadcast slicing
 *
 * Exercises DistanceVectorRoutingTable::GetNextBroadcastSlice() — the
 * rotation cursor used by NetworkService to fit routing broadcasts
 * within the per-frame entry budget at high spreading factors.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "protocols/lora_mesh/routing/distance_vector_routing_table.hpp"
#include "types/protocols/lora_mesh/network_node_route.hpp"

namespace loramesher {
namespace test {

using protocols::lora_mesh::DistanceVectorRoutingTable;

class RoutingTableRotationTest : public ::testing::Test {
   protected:
    static constexpr AddressType kLocalAddress = 0x1000;
    static constexpr uint32_t kCurrentTime = 1'000'000;
    static constexpr uint8_t kMaxHops = 10;
    static constexpr uint8_t kQuality = 200;

    void SetUp() override {
        routing_table_ =
            std::make_unique<DistanceVectorRoutingTable>(kLocalAddress);
    }

    void TearDown() override { routing_table_.reset(); }

    void AddDirectNeighbor(AddressType neighbor) {
        routing_table_->UpdateRoute(neighbor, neighbor, 1, kQuality, 0, 0,
                                    kCurrentTime);
    }

    void SeedActiveRoutes(size_t count, AddressType base = 0x2000) {
        for (size_t i = 0; i < count; ++i) {
            AddDirectNeighbor(static_cast<AddressType>(base + i));
        }
    }

    std::unique_ptr<DistanceVectorRoutingTable> routing_table_;
};

TEST_F(RoutingTableRotationTest, GetNextBroadcastSlice_RotatesAndWraps) {
    SeedActiveRoutes(9);

    auto slice1 =
        routing_table_->GetNextBroadcastSlice(kLocalAddress, /*max=*/3);
    auto slice2 = routing_table_->GetNextBroadcastSlice(kLocalAddress, 3);
    auto slice3 = routing_table_->GetNextBroadcastSlice(kLocalAddress, 3);
    auto slice4 = routing_table_->GetNextBroadcastSlice(kLocalAddress, 3);

    EXPECT_EQ(slice1.size(), 3u);
    EXPECT_EQ(slice2.size(), 3u);
    EXPECT_EQ(slice3.size(), 3u);
    EXPECT_EQ(slice4.size(), 3u);

    std::set<AddressType> first_round;
    for (const auto& e : slice1)
        first_round.insert(e.destination);
    for (const auto& e : slice2)
        first_round.insert(e.destination);
    for (const auto& e : slice3)
        first_round.insert(e.destination);
    EXPECT_EQ(first_round.size(), 9u)
        << "First rotation must cover every active route exactly once";

    std::set<AddressType> first_slice_addrs;
    for (const auto& e : slice1)
        first_slice_addrs.insert(e.destination);
    std::set<AddressType> fourth_slice_addrs;
    for (const auto& e : slice4)
        fourth_slice_addrs.insert(e.destination);
    EXPECT_EQ(first_slice_addrs, fourth_slice_addrs)
        << "Cursor must wrap back to the start after exhausting the table";
}

TEST_F(RoutingTableRotationTest, GetNextBroadcastSlice_HandlesShrink) {
    SeedActiveRoutes(5);

    auto slice1 =
        routing_table_->GetNextBroadcastSlice(kLocalAddress, /*max=*/3);
    EXPECT_EQ(slice1.size(), 3u);

    routing_table_->RemoveNode(0x2003);
    routing_table_->RemoveNode(0x2004);
    routing_table_->RemoveNode(0x2002);

    auto slice2 = routing_table_->GetNextBroadcastSlice(kLocalAddress, 3);
    EXPECT_LE(slice2.size(), 2u)
        << "After shrinking to 2 entries, slice must be bounded by table size";

    auto slice3 = routing_table_->GetNextBroadcastSlice(kLocalAddress, 3);
    EXPECT_FALSE(slice3.empty())
        << "Cursor must recover after the table shrinks below it";
}

TEST_F(RoutingTableRotationTest, GetNextBroadcastSlice_SingleSliceWhenSmall) {
    SeedActiveRoutes(4);

    auto slice1 = routing_table_->GetNextBroadcastSlice(kLocalAddress,
                                                        /*max=*/24);
    EXPECT_EQ(slice1.size(), 4u);

    auto slice2 = routing_table_->GetNextBroadcastSlice(kLocalAddress, 24);
    EXPECT_EQ(slice2.size(), 4u)
        << "When the table fits in one slice, subsequent calls return the "
           "same content (no churn)";

    std::set<AddressType> s1, s2;
    for (const auto& e : slice1)
        s1.insert(e.destination);
    for (const auto& e : slice2)
        s2.insert(e.destination);
    EXPECT_EQ(s1, s2);
}

TEST_F(RoutingTableRotationTest, GetNextBroadcastSlice_EmptyTableReturnsEmpty) {
    auto slice = routing_table_->GetNextBroadcastSlice(kLocalAddress, 24);
    EXPECT_TRUE(slice.empty());
}

TEST_F(RoutingTableRotationTest, GetNextBroadcastSlice_ZeroMaxReturnsEmpty) {
    SeedActiveRoutes(4);
    auto slice = routing_table_->GetNextBroadcastSlice(kLocalAddress,
                                                       /*max=*/0);
    EXPECT_TRUE(slice.empty());
}

TEST_F(RoutingTableRotationTest, GetNextBroadcastSlice_ExcludesAddress) {
    SeedActiveRoutes(3);
    constexpr AddressType kExclude = 0x2001;

    auto slice = routing_table_->GetNextBroadcastSlice(kExclude, /*max=*/24);

    for (const auto& entry : slice) {
        EXPECT_NE(entry.destination, kExclude);
        EXPECT_NE(entry.destination, kLocalAddress);
    }
}

TEST_F(RoutingTableRotationTest, GetNextBroadcastSlice_ResetsOnClear) {
    SeedActiveRoutes(6);

    routing_table_->GetNextBroadcastSlice(kLocalAddress, /*max=*/3);
    routing_table_->Clear();
    SeedActiveRoutes(6);

    auto slice = routing_table_->GetNextBroadcastSlice(kLocalAddress, 3);
    EXPECT_EQ(slice.size(), 3u);
    EXPECT_EQ(slice.front().destination, 0x2000)
        << "Cursor must reset to 0 on Clear()";
}

}  // namespace test
}  // namespace loramesher
