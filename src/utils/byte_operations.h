// src/utilities/byte_operations.h
#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace loramesher {
namespace utils {

// Helper class for serialization
class ByteSerializer {
   public:
    explicit ByteSerializer(std::vector<uint8_t>& buffer, size_t offset = 0)
        : buffer_(buffer), offset_(offset) {}

    // Write 16-bit value
    void writeUint16(uint16_t value) {
        buffer_[offset_++] = value & 0xFF;
        buffer_[offset_++] = (value >> 8) & 0xFF;
    }

    // Write 8-bit value
    void writeUint8(uint8_t value) { buffer_[offset_++] = value; }

    // Write array of bytes
    void writeBytes(const uint8_t* data, size_t length) {
        std::memcpy(&buffer_[offset_], data, length);
        offset_ += length;
    }

    size_t getOffset() const { return offset_; }

   private:
    std::vector<uint8_t>& buffer_;
    size_t offset_;
};

class ByteDeserializer {
   public:
    explicit ByteDeserializer(const std::vector<uint8_t>& buffer)
        : buffer_(buffer), offset_(0) {}

    uint16_t readUint16() {
        checkAvailable(2);
        uint16_t value = static_cast<uint16_t>(buffer_[offset_]) |
                         (static_cast<uint16_t>(buffer_[offset_ + 1]) << 8);
        offset_ += 2;
        return value;
    }

    uint8_t readUint8() {
        checkAvailable(1);
        return buffer_[offset_++];
    }

    std::vector<uint8_t> readBytes(size_t length) {
        checkAvailable(length);
        std::vector<uint8_t> result(buffer_.begin() + offset_,
                                    buffer_.begin() + offset_ + length);
        offset_ += length;
        return result;
    }

    void skip(size_t bytes) {
        checkAvailable(bytes);
        offset_ += bytes;
    }

    size_t getBytesLeft() const { return buffer_.size() - offset_; }

    size_t getOffset() const { return offset_; }

    bool hasMore() const { return offset_ < buffer_.size(); }

   private:
    void checkAvailable(size_t bytes) const {
        if (offset_ + bytes > buffer_.size()) {
            throw std::out_of_range(
                "Not enough bytes available to read: " + std::to_string(bytes) +
                " requested, " + std::to_string(getBytesLeft()) + " available");
        }
    }

    const std::vector<uint8_t>& buffer_;
    size_t offset_;
};

}  // namespace utils
}  // namespace loramesher
