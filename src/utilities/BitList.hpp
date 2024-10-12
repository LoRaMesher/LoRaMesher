#ifndef BIT_LIST_H
#define BIT_LIST_H

#include <cstdint>
#include <cstdlib>
#include <stdexcept>

class BitList {
public:
    BitList(size_t bitCount): size((bitCount + 7) / 8), currentIndex(0) {
        bits = static_cast<uint8_t*>(malloc(size));
        if (!bits) {
            throw std::bad_alloc();
        }
        std::fill(bits, bits + size, 0);
    }

    ~BitList() {
        free(bits);
    }

    void addBit(bool bit) {
        if (bit) {
            bits[currentIndex / 8] |= (1 << (currentIndex % 8));
        }
        else {
            bits[currentIndex / 8] &= ~(1 << (currentIndex % 8));
        }
        if (currentIndex == size * 8) {
            currentIndex = 0;
            return;
        }

        currentIndex = (currentIndex + 1) % (size * 8);
    }

    bool getBit(size_t index) const {
        if (index >= size * 8) {
            throw std::out_of_range("Index out of range");
        }
        return bits[index / 8] & (1 << (index % 8));
    }

    size_t getSize() const {
        return size * 8;
    }

    size_t countBits() const {
        size_t count = 0;
        for (size_t i = 0; i < size; ++i) {
            count += __builtin_popcount(bits[i]);
        }
        return count;
    }

    void printBits() const {
        printf("Bits: ");
        for (size_t i = 0; i < size; ++i) {
            for (size_t j = 0; j < 8; ++j) {
                printf("%d", (bits[i] & (1 << j)) != 0);
            }
        }
        printf("\n");
    }

    void clear() {
        std::fill(bits, bits + size, 0);
        currentIndex = 0;
    }

private:
    size_t size;
    uint8_t* bits;
    size_t currentIndex;
};

#endif // BIT_LIST_H