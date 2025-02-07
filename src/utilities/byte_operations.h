// src/utilities/byte_operations.h
#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace loramesher {

// Helper class for serialization
class ByteSerializer {
   public:
    explicit ByteSerializer(std::vector<uint8_t>& buffer, size_t offset = 0)
        : m_buffer(buffer), m_offset(offset) {}

    // Write 16-bit value
    void writeUint16(uint16_t value) {
        m_buffer[m_offset++] = value & 0xFF;
        m_buffer[m_offset++] = (value >> 8) & 0xFF;
    }

    // Write 8-bit value
    void writeUint8(uint8_t value) { m_buffer[m_offset++] = value; }

    // Write array of bytes
    void writeBytes(const uint8_t* data, size_t length) {
        std::memcpy(&m_buffer[m_offset], data, length);
        m_offset += length;
    }

    size_t getOffset() const { return m_offset; }

   private:
    std::vector<uint8_t>& m_buffer;
    size_t m_offset;
};

class ByteDeserializer {
   public:
    explicit ByteDeserializer(const std::vector<uint8_t>& buffer)
        : m_buffer(buffer), m_offset(0) {}

    uint16_t readUint16() {
        checkAvailable(2);
        uint16_t value = static_cast<uint16_t>(m_buffer[m_offset]) |
                         (static_cast<uint16_t>(m_buffer[m_offset + 1]) << 8);
        m_offset += 2;
        return value;
    }

    uint8_t readUint8() {
        checkAvailable(1);
        return m_buffer[m_offset++];
    }

    std::vector<uint8_t> readBytes(size_t length) {
        checkAvailable(length);
        std::vector<uint8_t> result(m_buffer.begin() + m_offset,
                                    m_buffer.begin() + m_offset + length);
        m_offset += length;
        return result;
    }

    void skip(size_t bytes) {
        checkAvailable(bytes);
        m_offset += bytes;
    }

    size_t getBytesLeft() const { return m_buffer.size() - m_offset; }

    size_t getOffset() const { return m_offset; }

    bool hasMore() const { return m_offset < m_buffer.size(); }

   private:
    void checkAvailable(size_t bytes) const {
        if (m_offset + bytes > m_buffer.size()) {
            throw std::out_of_range(
                "Not enough bytes available to read: " + std::to_string(bytes) +
                " requested, " + std::to_string(getBytesLeft()) + " available");
        }
    }

    const std::vector<uint8_t>& m_buffer;
    size_t m_offset;
};

}  // namespace loramesher
