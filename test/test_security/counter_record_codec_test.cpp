/**
 * @file counter_record_codec_test.cpp
 * @brief Tests of the 40-byte counter record encoding
 */

#include <gtest/gtest.h>

#include <vector>

#include "crypto_test_vectors.hpp"
#include "security/counter_record_codec.hpp"

namespace loramesher {
namespace security {
namespace {

using test_vectors::FromHex;

CounterRecord MakeSampleRecord() {
    CounterRecord record;
    record.tx_epoch = 3;
    record.limit[0] = 0x000123;
    record.limit[1] = 0x00000457;
    record.limit[2] = 0x00000029;
    record.limit[3] = 7;
    return record;
}

TEST(CounterRecordCodecTest, DefaultRecordHasTheSpaceCeilings) {
    const CounterRecord record;
    EXPECT_EQ(0u, record.tx_epoch);
    EXPECT_EQ(0x00FFFFFFu, record.ceiling[0]);
    EXPECT_EQ(0xFFFFFFFFu, record.ceiling[1]);
    EXPECT_EQ(0x3FFFFFFFu, record.ceiling[2]);
    EXPECT_TRUE(IsValidCounterRecord(record));
}

TEST(CounterRecordCodecTest, EncodesTheDocumentedLayout) {
    // Reference bytes computed with Python struct and zlib.crc32
    const auto bytes = EncodeCounterRecord(MakeSampleRecord());
    EXPECT_EQ(FromHex("4c4d433101030000"
                      "23010000ffffff00"
                      "57040000ffffffff"
                      "29000000ffffff3f"
                      "07000000d3f06313"),
              std::vector<uint8_t>(bytes.begin(), bytes.end()));

    const auto empty = EncodeCounterRecord(CounterRecord{});
    EXPECT_EQ(FromHex("4c4d433101000000"
                      "00000000ffffff00"
                      "00000000ffffffff"
                      "00000000ffffff3f"
                      "00000000f40ffb9f"),
              std::vector<uint8_t>(empty.begin(), empty.end()));
}

TEST(CounterRecordCodecTest, RoundTrips) {
    CounterRecord record = MakeSampleRecord();
    record.tx_epoch = 254;
    record.ceiling[0] = 0x00400000u;
    record.ceiling[2] = 0x20000000u;
    record.limit[3] = 0xFFFFFFFFu;
    const auto bytes = EncodeCounterRecord(record);
    const auto decoded = DecodeCounterRecord(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(*decoded == record);
}

TEST(CounterRecordCodecTest, RejectsEveryFlippedBit) {
    const auto bytes = EncodeCounterRecord(MakeSampleRecord());
    for (size_t bit = 0; bit < 8 * bytes.size(); ++bit) {
        auto damaged = bytes;
        damaged[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));
        EXPECT_FALSE(DecodeCounterRecord(damaged).has_value()) << bit;
    }
}

TEST(CounterRecordCodecTest, RejectsWrongSizes) {
    const auto bytes = EncodeCounterRecord(MakeSampleRecord());
    std::vector<uint8_t> data(bytes.begin(), bytes.end());
    data.pop_back();
    EXPECT_FALSE(DecodeCounterRecord(data).has_value());
    data.resize(kCounterRecordSize + 1);
    EXPECT_FALSE(DecodeCounterRecord(data).has_value());
    EXPECT_FALSE(DecodeCounterRecord({}).has_value());
}

TEST(CounterRecordCodecTest, RejectsFieldValuesOutsideTheirSpace) {
    // Each record carries a valid CRC, so only the field check can refuse
    CounterRecord beacon_ceiling;
    beacon_ceiling.ceiling[0] = 0x01000000u;
    CounterRecord data_ceiling;
    data_ceiling.ceiling[2] = 0x40000000u;
    CounterRecord limit_above_ceiling;
    limit_above_ceiling.ceiling[1] = 100;
    limit_above_ceiling.limit[1] = 101;
    CounterRecord epoch;
    epoch.tx_epoch = 255;
    for (const CounterRecord& record :
         {beacon_ceiling, data_ceiling, limit_above_ceiling, epoch}) {
        EXPECT_FALSE(IsValidCounterRecord(record));
        EXPECT_FALSE(
            DecodeCounterRecord(EncodeCounterRecord(record)).has_value());
    }
}

}  // namespace
}  // namespace security
}  // namespace loramesher
