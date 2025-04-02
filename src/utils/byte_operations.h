/**
 * @file byte_operations.h
 * @brief Helper classes for binary serialization and deserialization operations
 * @details Provides ByteSerializer and ByteDeserializer classes for handling binary data
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "types/error_codes/result.hpp"

namespace loramesher {
namespace utils {

/**
  * @brief Helper class for serializing data into a byte buffer
  * @details Provides methods to write different types of data into a provided buffer
  */
class ByteSerializer {
   public:
    /**
      * @brief Constructs a ByteSerializer with a buffer and optional offset
      * @param buffer Reference to the buffer where data will be written
      * @param offset Starting position in the buffer (default: 0)
      */
    explicit ByteSerializer(std::vector<uint8_t>& buffer, size_t offset = 0)
        : buffer_(buffer), offset_(offset) {}

    /**
      * @brief Writes a 16-bit unsigned integer to the buffer
      * @param value The value to write
      * @note Writes in little-endian format
      */
    void WriteUint16(uint16_t value) {
        buffer_[offset_++] = value & 0xFF;
        buffer_[offset_++] = (value >> 8) & 0xFF;
    }

    /**
      * @brief Writes a 32-bit unsigned integer to the buffer
      * @param value The value to write
      * @note Writes in little-endian format
      */
    void WriteUint32(uint32_t value) {
        buffer_[offset_++] = value & 0xFF;
        buffer_[offset_++] = (value >> 8) & 0xFF;
        buffer_[offset_++] = (value >> 16) & 0xFF;
        buffer_[offset_++] = (value >> 24) & 0xFF;
    }

    /**
      * @brief Writes an 8-bit unsigned integer to the buffer
      * @param value The value to write
      */
    void WriteUint8(uint8_t value) { buffer_[offset_++] = value; }

    /**
      * @brief Writes an array of bytes to the buffer
      * @param data Pointer to the data to write
      * @param length Number of bytes to write
      */
    void WriteBytes(const uint8_t* data, size_t length) {
        std::memcpy(&buffer_[offset_], data, length);
        offset_ += length;
    }

    /**
      * @brief Gets the current offset in the buffer
      * @return Current position in the buffer
      */
    size_t getOffset() const { return offset_; }

   private:
    std::vector<uint8_t>& buffer_;  ///< Reference to the target buffer
    size_t offset_;                 ///< Current position in the buffer
};

/**
  * @brief Helper class for deserializing data from a byte buffer
  * @details Provides methods to read different types of data from a provided buffer
  */
class ByteDeserializer {
   public:
    /**
      * @brief Constructs a ByteDeserializer with a buffer
      * @param buffer Reference to the buffer to read from
      */
    explicit ByteDeserializer(const std::vector<uint8_t>& buffer)
        : buffer_(buffer), offset_(0) {}

    /**
      * @brief Reads a 16-bit unsigned integer from the buffer
      * @return The read value if successful, std::nullopt otherwise
      * @note Reads in little-endian format
      */
    std::optional<uint16_t> ReadUint16() {
        Result result = CheckAvailable(2);
        if (!result.IsSuccess()) {
            return std::nullopt;
        }
        uint16_t value = static_cast<uint16_t>(buffer_[offset_]) |
                         (static_cast<uint16_t>(buffer_[offset_ + 1]) << 8);
        offset_ += 2;
        return value;
    }

    /**
      * @brief Reads an 8-bit unsigned integer from the buffer
      * @return The read value if successful, std::nullopt otherwise
      */
    std::optional<uint8_t> ReadUint8() {
        Result result = CheckAvailable(1);
        if (!result.IsSuccess()) {
            return std::nullopt;
        }
        return buffer_[offset_++];
    }

    /**
      * @brief Reads a 32-bit unsigned integer from the buffer
      * @return The read value if successful, std::nullopt otherwise
      * @note Reads in little-endian format
      */
    std::optional<uint32_t> ReadUint32() {
        Result result = CheckAvailable(4);
        if (!result.IsSuccess()) {
            return std::nullopt;
        }
        uint32_t value = static_cast<uint32_t>(buffer_[offset_]) |
                         (static_cast<uint32_t>(buffer_[offset_ + 1]) << 8) |
                         (static_cast<uint32_t>(buffer_[offset_ + 2]) << 16) |
                         (static_cast<uint32_t>(buffer_[offset_ + 3]) << 24);
        offset_ += 4;
        return value;
    }

    /**
      * @brief Reads a sequence of bytes from the buffer
      * @param length Number of bytes to read
      * @return Vector containing the read bytes if successful, std::nullopt otherwise
      */
    std::optional<std::vector<uint8_t>> ReadBytes(size_t length) {
        Result result = CheckAvailable(length);
        if (!result.IsSuccess()) {
            return std::nullopt;
        }
        std::vector<uint8_t> vec_result(buffer_.begin() + offset_,
                                        buffer_.begin() + offset_ + length);
        offset_ += length;
        return vec_result;
    }

    /**
      * @brief Skips a number of bytes in the buffer
      * @param length Number of bytes to skip
      * @return Result indicating success or failure
      */
    Result Skip(size_t length) {
        Result result = CheckAvailable(length);
        if (!result.IsSuccess()) {
            return result;
        }
        offset_ += length;
    }

    /**
      * @brief Gets the number of unread bytes in the buffer
      * @return Number of remaining bytes
      */
    size_t getBytesLeft() const { return buffer_.size() - offset_; }

    /**
      * @brief Gets the current offset in the buffer
      * @return Current position in the buffer
      */
    size_t getOffset() const { return offset_; }

    /**
      * @brief Checks if there are more bytes to read
      * @return true if there are unread bytes, false otherwise
      */
    bool hasMore() const { return offset_ < buffer_.size(); }

   private:
    /**
      * @brief Checks if the requested number of bytes is available
      * @param bytes Number of bytes to check
      * @return Result indicating if the bytes are available
      */
    Result CheckAvailable(size_t bytes) const {
        if (offset_ + bytes > buffer_.size()) {
            return Result::Error(LoraMesherErrorCode::kBufferOverflow);
        }
        return Result::Success();
    }

    const std::vector<uint8_t>& buffer_;  ///< Reference to the source buffer
    size_t offset_;                       ///< Current position in the buffer
};

}  // namespace utils
}  // namespace loramesher