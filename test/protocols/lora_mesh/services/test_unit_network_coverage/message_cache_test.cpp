/**
 * @file message_cache_test.cpp
 * @brief Unit tests for the per-node sequence counter and de-duplication cache
 */

#include <gtest/gtest.h>

#include "protocols/lora_mesh/services/message_cache.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

namespace {
constexpr AddressType kSender = 0x1001;
constexpr AddressType kOther = 0x2002;
}  // namespace

TEST(MessageCacheTest, RecordIfNewRejectsDuplicates) {
    MessageCache cache;
    EXPECT_TRUE(cache.RecordIfNew(kSender, 7));
    EXPECT_FALSE(cache.RecordIfNew(kSender, 7));
    EXPECT_TRUE(cache.RecordIfNew(kOther, 7));
    EXPECT_TRUE(cache.Contains(kSender, 7));
}

TEST(MessageCacheTest, OldestEntryIsEvictedWhenFull) {
    MessageCache cache;
    for (size_t i = 0; i <= MessageCache::kCapacity; ++i) {
        cache.Record(kSender, static_cast<uint8_t>(i));
    }
    EXPECT_FALSE(cache.Contains(kSender, 0));
    EXPECT_TRUE(cache.Contains(kSender, MessageCache::kCapacity));
}

/**
 * @brief A node that resets keeps numbering its packets where it left off,
 *        so neighbours that still cache its recent packets accept new ones.
 */
TEST(MessageCacheTest, ResetDoesNotReuseRecentSequences) {
    MessageCache sender;
    MessageCache neighbour;
    for (int i = 0; i < 3; ++i) {
        neighbour.Record(kSender, sender.NextSeq());
    }

    sender.Reset();

    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(neighbour.RecordIfNew(kSender, sender.NextSeq()));
    }
}

TEST(MessageCacheTest, ResetClearsReceivedEntries) {
    MessageCache cache;
    cache.Record(kOther, 9);
    cache.Reset();
    EXPECT_FALSE(cache.Contains(kOther, 9));
}

TEST(MessageCacheTest, RestoredEntriesKeepTheirEvictionOrder) {
    MessageCache cache;
    for (size_t i = 0; i <= MessageCache::kCapacity; ++i) {
        cache.Record(kSender, static_cast<uint8_t>(i));
    }
    const auto seen = cache.GetSeenMessages();
    ASSERT_EQ(seen.size(), MessageCache::kCapacity);
    EXPECT_EQ(seen.front(), (storage::SeenMessage{kSender, 1}));
    EXPECT_EQ(seen.back(),
              (storage::SeenMessage{
                  kSender, static_cast<uint8_t>(MessageCache::kCapacity)}));

    MessageCache restored;
    ASSERT_TRUE(restored.RestoreSeenMessages(seen));
    EXPECT_EQ(restored.GetSeenMessages(), seen);
    EXPECT_FALSE(restored.Contains(kSender, 0));
    EXPECT_TRUE(restored.Contains(kSender, 1));

    // The next record evicts the oldest restored entry
    restored.Record(kOther, 1);
    EXPECT_FALSE(restored.Contains(kSender, 1));
    EXPECT_TRUE(restored.Contains(kSender, 2));
}

TEST(MessageCacheTest, PartialCacheRoundTrips) {
    MessageCache cache;
    cache.Record(kSender, 4);
    cache.Record(kOther, 9);

    MessageCache restored;
    restored.Record(kOther, 77);
    ASSERT_TRUE(restored.RestoreSeenMessages(cache.GetSeenMessages()));
    EXPECT_EQ(restored.GetSeenMessages(), cache.GetSeenMessages());
    EXPECT_FALSE(restored.Contains(kOther, 77));
}

TEST(MessageCacheTest, TooManyEntriesAreNotRestored) {
    MessageCache cache;
    cache.Record(kSender, 4);
    std::vector<storage::SeenMessage> too_many(MessageCache::kCapacity + 1,
                                               {kOther, 1});
    EXPECT_FALSE(cache.RestoreSeenMessages(too_many));
    EXPECT_TRUE(cache.Contains(kSender, 4));
}

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher
