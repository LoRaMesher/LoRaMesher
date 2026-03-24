// src/hardware/radiolib/radiolib_modules/sx1262.hpp
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include <memory>

#include "RadioLib.h"

#include "types/radio/radio.hpp"

namespace loramesher {
namespace radio {

/**
 * @brief Implementation of IRadio interface for SX1262 using RadioLib
 *
 * This class provides a concrete implementation of the IRadio interface
 * specifically for the SX1262 radio module using the RadioLib library.
 * The SX1262 is part of the SX126x family featuring lower power consumption
 * and up to +22 dBm TX power.
 *
 * @note This implementation is designed for use with RadioLib and requires
 * proper SPI configuration. The SX1262 requires a BUSY pin in addition to
 * CS, IRQ (DIO1), and RESET pins.
 *
 * @see IRadio
 * @see RadioLib::SX1262
 */
class LoraMesherSX1262 : public IRadio {
   public:
    /**
     * @brief Construct a new LoraMesher SX1262 radio instance
     *
     * @param cs_pin SPI Chip Select pin number
     * @param irq_pin Interrupt Request pin number (DIO1)
     * @param reset_pin Reset pin number
     * @param busy_pin Busy state pin number (required for SX1262)
     * @param spi SPI bus instance to use for communication
     */
    LoraMesherSX1262(int8_t cs_pin, int8_t irq_pin, int8_t reset_pin,
                     int8_t busy_pin, SPIClass& spi)
        : cs_pin_(cs_pin),
          irq_pin_(irq_pin),
          reset_pin_(reset_pin),
          busy_pin_(busy_pin),
          spi_(spi) {}

    LoraMesherSX1262(const LoraMesherSX1262&) = delete;
    LoraMesherSX1262& operator=(const LoraMesherSX1262&) = delete;
    LoraMesherSX1262(LoraMesherSX1262&&) = default;
    LoraMesherSX1262& operator=(LoraMesherSX1262&&) = delete;
    ~LoraMesherSX1262() override = default;

    Result Begin(const RadioConfig& config) override;
    Result Send(const uint8_t* data, size_t len) override;
    Result StartReceive() override;
    Result Sleep() override;
    Result setFrequency(float frequency) override;
    Result setSpreadingFactor(uint8_t sf) override;
    Result setBandwidth(float bandwidth) override;
    Result setCodingRate(uint8_t coding_rate) override;
    Result setPower(int8_t power) override;
    Result setSyncWord(uint8_t sync_word) override;
    Result setCRC(bool enable) override;
    Result setPreambleLength(uint16_t length) override;
    Result setActionReceive(void (*callback)(void)) override;
    Result ClearActionReceive() override;
    int8_t getRSSI() override;
    int8_t getSNR() override;
    uint8_t getPacketLength() override;
    uint32_t getTimeOnAir(uint8_t length) override;
    Result readData(uint8_t* data, size_t len) override;
    Result setCurrentLimit(float current_limit_ma) override;

    // Not supported functions

    Result Configure(const RadioConfig& config) override {
        (void)config;
        throw std::runtime_error("Configure not supported in LoraMesherSX1262");
    }

    Result setActionReceive(
        std::function<void(std::unique_ptr<RadioEvent>)> callback) {
        (void)callback;
        throw std::runtime_error(
            "setActionReceive not supported in LoraMesherSX1262");
    }

    int8_t getLastPacketRSSI() override {
        throw std::runtime_error(
            "getLastPacketRSSI not supported in LoraMesherSX1262");
    }

    int8_t getLastPacketSNR() override {
        throw std::runtime_error(
            "getLastPacketSNR not supported in LoraMesherSX1262");
    }

    bool IsTransmitting() override {
        throw std::runtime_error(
            "IsTransmitting not supported in LoraMesherSX1262");
    }

    float getFrequency() override {
        throw std::runtime_error(
            "getFrequency not supported in LoraMesherSX1262");
    }

    uint8_t getSpreadingFactor() override {
        throw std::runtime_error(
            "getSpreadingFactor not supported in LoraMesherSX1262");
    }

    float getBandwidth() override {
        throw std::runtime_error(
            "getBandwidth not supported in LoraMesherSX1262");
    }

    uint8_t getCodingRate() override {
        throw std::runtime_error(
            "getCodingRate not supported in LoraMesherSX1262");
    }

    uint8_t getPower() override {
        throw std::runtime_error("getPower not supported in LoraMesherSX1262");
    }

    Result setState(RadioState state) override {
        (void)state;
        throw std::runtime_error("setState not supported in LoraMesherSX1262");
    }

    RadioState getState() override {
        throw std::runtime_error("getState not supported in LoraMesherSX1262");
    }

   private:
    Result InitializeHardware();

    const int8_t cs_pin_;
    const int8_t irq_pin_;
    const int8_t reset_pin_;
    const int8_t busy_pin_;
    SPIClass& spi_;  ///< SPI bus instance

    std::unique_ptr<Module> hal_module_;
    std::unique_ptr<SX1262> radio_module_;

    bool initialized_ = false;
    bool auto_current_limit_ = true;
};

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO
