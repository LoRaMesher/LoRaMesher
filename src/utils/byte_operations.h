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

#include "utils/compat/span.hpp"
#include "types/error_codes/result.hpp"

namespace loramesher {
namespace utils {

class ByteOperations {
   public:
    /**
      * @brief Checks if the requested number of bytes is available in the buffer
      * @param buffer The buffer to check
      * @param offset The starting position in the buffer
      * @param bytes The number of bytes to check
      *
      *  @return Result indicating if the bytes are available
      */
    static const Result CheckAvailable(std::span<const uint8_t> buffer,
                                       size_t offset, size_t bytes) {
        if (offset + bytes > buffer.size()) {
            return Result::Error(LoraMesherErrorCode::kBufferOverflow);
        }
        return Result::Success();
    }
};

/**
  * @brief Helper class for serializing data into a byte buffer
  * @details Provides methods to write different types of data into a provided buffer.
  *          Supports both std::vector<uint8_t> and raw uint8_t* buffers.
  */
class ByteSerializer {
   public:
    /**
      * @brief Constructs a ByteSerializer with a vector buffer and optional offset
      * @param buffer Reference to the buffer where data will be written
      * @param offset Starting position in the buffer (default: 0)
      */
    explicit ByteSerializer(std::vector<uint8_t>& buffer, size_t offset = 0)
        : data_(buffer.data()), capacity_(buffer.size()), offset_(offset) {}

    /**
      * @brief Constructs a ByteSerializer with a raw pointer buffer
      * @param data Pointer to the buffer where data will be written
      * @param capacity Total capacity of the buffer in bytes
      * @param offset Starting position in the buffer (default: 0)
      */
    explicit ByteSerializer(uint8_t* data, size_t capacity, size_t offset = 0)
        : data_(data), capacity_(capacity), offset_(offset) {}

    /**
      * @brief Writes a 16-bit unsigned integer to the buffer
      * @param value The value to write
      * @note Writes in little-endian format
      */
    void WriteUint16(uint16_t value) {
        if (offset_ + 2 > capacity_) {
            throw std::runtime_error("Buffer overflow during write");
        }
        data_[offset_++] = value & 0xFF;
        data_[offset_++] = (value >> 8) & 0xFF;
    }

    /**
      * @brief Writes a 32-bit unsigned integer to the buffer
      * @param value The value to write
      * @note Writes in little-endian format
      */
    void WriteUint32(uint32_t value) {
        if (offset_ + 4 > capacity_) {
            throw std::runtime_error("Buffer overflow during write");
        }
        data_[offset_++] = value & 0xFF;
        data_[offset_++] = (value >> 8) & 0xFF;
        data_[offset_++] = (value >> 16) & 0xFF;
        data_[offset_++] = (value >> 24) & 0xFF;
    }

    /**
      * @brief Writes an 8-bit unsigned integer to the buffer
      * @param value The value to write
      */
    void WriteUint8(uint8_t value) {
        if (offset_ + 1 > capacity_) {
            throw std::runtime_error("Buffer overflow during write");
        }
        data_[offset_++] = value;
    }

    /**
      * @brief Writes an array of bytes to the buffer
      * @param data Pointer to the data to write
      * @param length Number of bytes to write
      */
    void WriteBytes(const uint8_t* data, size_t length) {
        if (offset_ + length > capacity_) {
            throw std::runtime_error("Buffer overflow during write");
        }
        std::memcpy(&data_[offset_], data, length);
        offset_ += length;
    }

    /**
      * @brief Gets the current offset in the buffer
      * @return Current position in the buffer
      */
    size_t getOffset() const { return offset_; }

   private:
    uint8_t* data_;    ///< Pointer to the target buffer
    size_t capacity_;  ///< Total capacity of the buffer
    size_t offset_;    ///< Current position in the buffer
};

/**
  * @brief Helper class for deserializing data from a byte buffer
  * @details Provides methods to read different types of data from a provided buffer
  */
class ByteDeserializer {
   public:
    /**
      * @brief Constructs a ByteDeserializer with a span buffer
      * @param buffer Span to read from (implicitly constructible from vector)
      */
    explicit ByteDeserializer(std::span<const uint8_t> buffer)
        : buffer_(buffer), offset_(0) {}

    /**
      * @brief Reads a 16-bit unsigned integer from the buffer
      * @return The read value if successful, std::nullopt otherwise
      * @note Reads in little-endian format
      */
    std::optional<uint16_t> ReadUint16() {
        Result result = ByteOperations::CheckAvailable(buffer_, offset_, 2);
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
        Result result = ByteOperations::CheckAvailable(buffer_, offset_, 1);
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
        Result result = ByteOperations::CheckAvailable(buffer_, offset_, 4);
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
      * @brief Reads a sequence of bytes from the buffer into a vector
      * @param length Number of bytes to read
      * @return Vector containing the read bytes if successful, std::nullopt otherwise
      */
    std::optional<std::vector<uint8_t>> ReadBytes(size_t length) {
        Result result =
            ByteOperations::CheckAvailable(buffer_, offset_, length);

        if (!result.IsSuccess()) {
            return std::nullopt;
        }
        std::vector<uint8_t> vec_result(buffer_.begin() + offset_,
                                        buffer_.begin() + offset_ + length);
        offset_ += length;
        return vec_result;
    }

    /**
      * @brief Reads a sequence of bytes from the buffer as a span (zero-copy)
      * @param length Number of bytes to read
      * @return Span into the source buffer if successful, std::nullopt otherwise
      * @note The returned span is valid only as long as the source buffer is alive
      */
    std::optional<std::span<const uint8_t>> ReadBytesAsSpan(size_t length) {
        Result result =
            ByteOperations::CheckAvailable(buffer_, offset_, length);
        if (!result.IsSuccess()) {
            return std::nullopt;
        }
        auto result_span = buffer_.subspan(offset_, length);
        offset_ += length;
        return result_span;
    }

    /**
      * @brief Skips a number of bytes in the buffer
      * @param length Number of bytes to skip
      * @return Result indicating success or failure
      */
    Result Skip(size_t length) {
        Result result =
            ByteOperations::CheckAvailable(buffer_, offset_, length);
        if (!result.IsSuccess()) {
            return result;
        }
        offset_ += length;

        return Result::Success();
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
    std::span<const uint8_t> buffer_;  ///< View of the source buffer
    size_t offset_;                    ///< Current position in the buffer
};

}  // namespace utils
}  // namespace loramesher
