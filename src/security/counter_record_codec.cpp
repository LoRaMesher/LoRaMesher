/**
 * @file counter_record_codec.cpp
 * @brief Persisted record of the transmit frame counters and its encoding
 */

#include "security/counter_record_codec.hpp"

#include "types/storage/snapshot_codec.hpp"

namespace loramesher {
namespace security {

namespace {

constexpr uint8_t kMagic[4] = {'L', 'M', 'C', '1'};
constexpr uint8_t kFormatVersion = 1;
constexpr size_t kEpochOffset = 5;
constexpr size_t kFieldsOffset = 8;
constexpr size_t kCrcOffset = kCounterRecordSize - 4;

void WriteUint32(uint8_t* out, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) {
        out[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

uint32_t ReadUint32(const uint8_t* in) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(in[i]) << (8 * i);
    }
    return value;
}

}  // namespace

bool IsValidCounterRecord(const CounterRecord& record) {
    if (record.tx_epoch > kMaxEpoch) {
        return false;
    }
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        if (record.ceiling[i] > kCounterSpaceNone[i] ||
            record.limit[i] > record.ceiling[i]) {
            return false;
        }
    }
    return true;
}

std::array<uint8_t, kCounterRecordSize> EncodeCounterRecord(
    const CounterRecord& record) {
    std::array<uint8_t, kCounterRecordSize> bytes{};
    for (size_t i = 0; i < sizeof(kMagic); ++i) {
        bytes[i] = kMagic[i];
    }
    bytes[4] = kFormatVersion;
    bytes[kEpochOffset] = record.tx_epoch;
    uint8_t* field = &bytes[kFieldsOffset];
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        WriteUint32(field, record.limit[i]);
        WriteUint32(field + 4, record.ceiling[i]);
        field += 8;
    }
    WriteUint32(field, record.limit[kTrafficSpaceCount]);
    WriteUint32(&bytes[kCrcOffset],
                storage::SnapshotCodec::Crc32(
                    std::span<const uint8_t>(bytes.data(), kCrcOffset)));
    return bytes;
}

std::optional<CounterRecord> DecodeCounterRecord(
    std::span<const uint8_t> data) {
    if (data.size() != kCounterRecordSize) {
        return std::nullopt;
    }
    for (size_t i = 0; i < sizeof(kMagic); ++i) {
        if (data[i] != kMagic[i]) {
            return std::nullopt;
        }
    }
    if (data[4] != kFormatVersion || data[6] != 0 || data[7] != 0) {
        return std::nullopt;
    }
    if (ReadUint32(&data[kCrcOffset]) !=
        storage::SnapshotCodec::Crc32(data.first(kCrcOffset))) {
        return std::nullopt;
    }

    CounterRecord record;
    record.tx_epoch = data[kEpochOffset];
    const uint8_t* field = &data[kFieldsOffset];
    for (size_t i = 0; i < kTrafficSpaceCount; ++i) {
        record.limit[i] = ReadUint32(field);
        record.ceiling[i] = ReadUint32(field + 4);
        field += 8;
    }
    record.limit[kTrafficSpaceCount] = ReadUint32(field);
    if (!IsValidCounterRecord(record)) {
        return std::nullopt;
    }
    return record;
}

}  // namespace security
}  // namespace loramesher
