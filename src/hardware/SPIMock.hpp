// src/hardware/SPIMock.hpp
#pragma once

#include <stdio.h>
#include <cstdint>

#ifdef ARDUINO
// Use actual ESP32 SPI implementation
#include <SPI.h>
#else
// Mock implementation for native builds

/**
 * @brief Mock implementation of SPIClass for native platform development
 * 
 * This class provides a compatible interface with ESP32's SPIClass
 * to allow for compilation and testing on native platforms.
 */
class SPIClass {
   public:
    /**
   * @brief Construct a new SPIClass object
   * 
   * @param spi_bus The SPI bus to use (ignored in mock)
   */
    SPIClass(uint8_t spi_bus = 0) : _spi_bus(spi_bus) {}

    /**
   * @brief Initialize the SPI bus
   */
    void begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1,
               int8_t ss = -1) {
        _initialized = true;

        // Prevent unused parameter warnings
        (void)sck;
        (void)miso;
        (void)mosi;
        (void)ss;

        // Log mock initialization for debugging
        printf("MOCK: SPI%d initialized\n", _spi_bus);
    }

    /**
   * @brief End the SPI connection
   */
    void end() {
        _initialized = false;
        printf("MOCK: SPI%d ended\n", _spi_bus);
    }

    /**
   * @brief Set the bit order
   * 
   * @param bitOrder MSBFIRST or LSBFIRST
   */
    void setBitOrder(uint8_t bitOrder) { _bit_order = bitOrder; }

    /**
   * @brief Set the SPI clock divider
   * 
   * @param clockDiv Clock divider
   */
    void setClockDivider(uint32_t clockDiv) { _clock_div = clockDiv; }

    /**
   * @brief Set the SPI data mode
   * 
   * @param dataMode SPI data mode (0-3)
   */
    void setDataMode(uint8_t dataMode) { _data_mode = dataMode; }

    /**
   * @brief Set the SPI frequency
   * 
   * @param freq Frequency in Hz
   */
    void setFrequency(uint32_t freq) {
        _frequency = freq;
        printf("MOCK: SPI%d frequency set to %u Hz\n", _spi_bus, _frequency);
    }

    /**
   * @brief Transfer a byte over SPI
   * 
   * @param data Byte to send
   * @return uint8_t Received byte (mocked)
   */
    uint8_t transfer(uint8_t data) {
        if (!_initialized) {
            printf("MOCK WARNING: SPI transfer called before initialization\n");
            return 0xFF;
        }

        // Prevent unused parameter warnings
        (void)data;

        // In mock implementation, just return some dummy data
        // Could be extended to simulate specific device responses
        return 0xA5;  // Mock response
    }

    /**
   * @brief Transfer multiple bytes over SPI
   * 
   * @param data Buffer containing data to send
   * @param size Number of bytes to transfer
   */
    void transfer(void* data, size_t size) {
        if (!_initialized) {
            printf("MOCK WARNING: SPI transfer called before initialization\n");
            return;
        }

        // Prevent unused parameter warnings
        (void)data;
        (void)size;

        // In real implementation, this would read/write from the buffer
        // For mock, we just show that the function was called
        printf("MOCK: SPI%d transferred %zu bytes\n", _spi_bus, size);
    }

    /**
   * @brief Transfer multiple bytes over SPI with separate buffers
   * 
   * @param txbuf Buffer containing data to send
   * @param rxbuf Buffer to store received data
   * @param size Number of bytes to transfer
   */
    void transferBytes(const uint8_t* txbuf, uint8_t* rxbuf, size_t size) {
        if (!_initialized) {
            printf(
                "MOCK WARNING: SPI transferBytes called before "
                "initialization\n");
            return;
        }

        if (rxbuf && txbuf) {
            // Copy txbuf to rxbuf with some modification to simulate a response
            for (size_t i = 0; i < size; i++) {
                rxbuf[i] = txbuf[i] ^ 0xFF;  // Mock response: invert the data
            }
        }

        printf("MOCK: SPI%d transferred %zu bytes with separate buffers\n",
               _spi_bus, size);
    }

   private:
    uint8_t _spi_bus;
    bool _initialized = false;
    uint8_t _bit_order = 0;  // MSBFIRST
    uint32_t _clock_div = 0;
    uint8_t _data_mode = 0;
    uint32_t _frequency = 1000000;  // Default: 1MHz
};

// Define constants that would normally be defined by Arduino.h
#ifndef MSBFIRST
#define MSBFIRST 0
#endif
#ifndef LSBFIRST
#define LSBFIRST 1
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 0
#endif
#ifndef SPI_MODE1
#define SPI_MODE1 1
#endif
#ifndef SPI_MODE2
#define SPI_MODE2 2
#endif
#ifndef SPI_MODE3
#define SPI_MODE3 3
#endif

// Create global SPI instances to match ESP32
extern SPIClass SPI;
extern SPIClass SPI1;
extern SPIClass SPI2;

#endif  // ARDUINO