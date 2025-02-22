// src/hardware/arduino/radio_lib_modules/LoraMesherSX1276.hpp
#pragma once

#include <memory>

#include "RadioLib.h"

#include "types/radio/radio.hpp"

namespace loramesher {
namespace radio {

/**
 * @brief Implementation of IRadio interface for LoraMesher SX1276 using RadioLib
 * 
 * This class provides a concrete implementation of the IRadio interface
 * specifically for the LoraMesher SX1276 radio module using the RadioLib library.
 * It manages all low-level communication with the hardware and provides
 * a high-level interface for radio operations.
 * 
 * @note This implementation is designed for use with RadioLib and requires
 * proper SPI configuration.
 * 
 * @see IRadio
 * @see RadioLib::SX1276
 */
class LoraMesherSX1276 : public IRadio {
   public:
    /**
     * @brief Construct a new LoraMesher SX1276 radio instance
     * 
     * Initializes a new LoraMesher SX1276 radio module with the specified pins.
     * The pins must be configured for proper SPI communication.
     * 
     * @param cs_pin SPI Chip Select pin number
     * @param irq_pin Interrupt Request pin number
     * @param reset_pin Reset pin number
     * @param busy_pin Busy state pin number
     * 
     * @note All pin numbers should be valid for your hardware configuration
     */
    LoraMesherSX1276(int8_t cs_pin, int8_t irq_pin, int8_t reset_pin,
                     int8_t busy_pin)
        : cs_pin_(cs_pin),
          irq_pin_(irq_pin),
          reset_pin_(reset_pin),
          busy_pin_(busy_pin) {}

    /**
     * @brief Deleted copy constructor to prevent duplicated hardware access
     */
    LoraMesherSX1276(const LoraMesherSX1276&) = delete;

    /**
     * @brief Deleted copy assignment operator to prevent duplicated hardware access
     */
    LoraMesherSX1276& operator=(const LoraMesherSX1276&) = delete;

    /**
     * @brief Move constructor for transfer of ownership
     */
    LoraMesherSX1276(LoraMesherSX1276&&) = default;

    /**
     * @brief Move assignment operator for transfer of ownership
     */
    LoraMesherSX1276& operator=(LoraMesherSX1276&&) = default;

    /**
     * @brief Destructor ensuring proper cleanup of hardware resources
     */
    ~LoraMesherSX1276() override { Sleep(); }

    /**
     * @brief Initialize and begin radio operations
     * @param config Initial configuration parameters
     * @return Result Success if initialization was successful
     */
    Result Begin(const RadioConfig& config) override;

    /**
     * @brief Transmit data over the radio
     * @param data Pointer to data buffer to transmit
     * @param len Length of data in bytes
     * @return Result Success if transmission started successfully
     */
    Result Send(const uint8_t* data, size_t len) override;

    /**
     * @brief Start continuous receive mode
     * @return Result Success if receive mode was started successfully
     */
    Result StartReceive() override;

    /**
     * @brief Enter sleep mode to conserve power
     * @return Result Success if sleep mode was entered successfully
     */
    Result Sleep() override;

    /**
     * @brief Set the radio frequency
     * @param frequency Target frequency in MHz
     * @return Result Success if frequency was set successfully
     */
    Result setFrequency(float frequency) override;

    /**
     * @brief Set the spreading factor for LoRa modulation
     * @param sf Spreading factor (6-12)
     * @return Result Success if spreading factor was set successfully
     */
    Result setSpreadingFactor(uint8_t sf) override;

    /**
     * @brief Set the signal bandwidth
     * @param bandwidth Bandwidth in kHz
     * @return Result Success if bandwidth was set successfully
     */
    Result setBandwidth(float bandwidth) override;

    /**
     * @brief Set the coding rate for error correction
     * @param coding_rate Coding rate (5-8, representing 4/5 to 4/8)
     * @return Result Success if coding rate was set successfully
     */
    Result setCodingRate(uint8_t coding_rate) override;

    /**
     * @brief Set the transmission power
     * @param power Output power in dBm
     * @return Result Success if power was set successfully
     */
    Result setPower(uint8_t power) override;

    /**
     * @brief Set the sync word for packet recognition
     * @param sync_word Sync word value
     * @return Result Success if sync word was set successfully
     */
    Result setSyncWord(uint8_t sync_word) override;

    /**
     * @brief Enable or disable CRC checking
     * @param enable True to enable CRC, false to disable
     * @return Result Success if CRC setting was changed successfully
     */
    Result setCRC(bool enable) override;

    /**
     * @brief Set the preamble length
     * @param length Preamble length in symbols
     * @return Result Success if preamble length was set successfully
     */
    Result setPreambleLength(uint16_t length) override;

    /**
     * @brief Sets the callback function for packet reception
     * 
     * @param callback Function pointer to the callback that will be executed when a packet is received
     * @return true If the callback was set successfully
     * @return false If there was an error setting the callback
     */
    Result setActionReceive(void (*callback)(void)) override;

    /**
     * @brief Clear the action receive. At this moment receiving will be disabled
     * 
     * @return Result Success if clearing action receive is successfully
     */
    Result ClearActionReceive() override;

    /**
     * @brief Get the current RSSI value
     * @return int8_t Current RSSI in dBm
     */
    int8_t getRSSI() override;

    /**
     * @brief Get the current SNR value
     * @return int8_t Current SNR in dB
     */
    int8_t getSNR() override;

    /**
     * @brief Get the length of the received packet
     * @return size_t Length of the received packet in bytes
     */
    uint8_t getPacketLength() override;

    /**
     * @brief Read the received data from the radio
     * 
     * @param data Buffer to store received data
     * @param len Length of data to read
     * @return Result Success if data was read successfully
     */
    Result readData(uint8_t* data, size_t len) override;

    // Not supported functions

    /**
     * @brief Configure the radio with the given configuration
     * @param config The radio configuration to apply
     * @return Result Never returns - throws exception
     * @throws std::runtime_error This operation is not supported
     */
    Result Configure(const RadioConfig& config) override {
        throw std::runtime_error("Configure not supported in LoraMesherSX1276");
    }

    /**
     * @brief Set the action to take when data is received
     * @param callback Function to call when data is received
     * @result Result Success if action was set successfully
     */
    Result setActionReceive(
        std::function<void(std::unique_ptr<RadioEvent>)> callback) {
        throw std::runtime_error(
            "setActionReceive not supported in LoraMesherSX1276");
    }

    /**
     * @brief Get the RSSI (Received Signal Strength Indicator) of the last received packet
     * @return int8_t Never returns - throws exception
     * @throws std::runtime_error This operation is not supported
     */
    int8_t getLastPacketRSSI() override {
        throw std::runtime_error(
            "getLastPacketRSSI not supported in LoraMesherSX1276");
    }

    /**
     * @brief Get the SNR (Signal-to-Noise Ratio) of the last received packet
     * @return int8_t Never returns - throws exception  
     * @throws std::runtime_error This operation is not supported
     */
    int8_t getLastPacketSNR() override {
        throw std::runtime_error(
            "getLastPacketSNR not supported in LoraMesherSX1276");
    }

    /**
     * @brief Check if the radio is currently transmitting
     * @return bool Never returns - throws exception
     * @throws std::runtime_error This operation is not supported
     */
    bool IsTransmitting() override {
        throw std::runtime_error(
            "IsTransmitting not supported in LoraMesherSX1276");
    }

    /**
     * @brief Get the current frequency setting
     * @return float Never returns - throws exception
     * @throws std::runtime_error This operation is not supported
     */
    float getFrequency() override {
        throw std::runtime_error(
            "getFrequency not supported in LoraMesherSX1276");
    }

    /**
     * @brief Get the current spreading factor setting
     * @return uint8_t Never returns - throws exception
     * @throws std::runtime_error This operation is not supported
     */
    uint8_t getSpreadingFactor() override {
        throw std::runtime_error(
            "getSpreadingFactor not supported in LoraMesherSX1276");
    }

    /**
     * @brief Get the current bandwidth setting
     * @return float Never returns - throws exception
     * @throws std::runtime_error This operation is not supported
     */
    float getBandwidth() override {
        throw std::runtime_error(
            "getBandwidth not supported in LoraMesherSX1276");
    }

    /**
     * @brief Get the current coding rate setting
     * @return uint8_t Never returns - throws exception
     * @throws std::runtime_error This operation is not supported
     */
    uint8_t getCodingRate() override {
        throw std::runtime_error(
            "getCodingRate not supported in LoraMesherSX1276");
    }

    /**
     * @brief Get the current transmission power setting
     * @return uint8_t Never returns - throws exception
     * @throws std::runtime_error This operation is not supported
     */
    uint8_t getPower() override {
        throw std::runtime_error("getPower not supported in LoraMesherSX1276");
    }

    /**
 * @brief Set the radio state (TX, RX, standby, etc.)
 * @param state The state to set the radio to
 * @return Result Never returns - throws exception
 * @throws std::runtime_error This operation is not supported
 */
    Result setState(RadioState state) override {
        throw std::runtime_error("setState not supported in LoraMesherSX1276");
    }

    /**
 * @brief Get the current radio state
 * @return RadioState Never returns - throws exception
 * @throws std::runtime_error This operation is not supported
 */
    RadioState getState() override {
        throw std::runtime_error("getState not supported in LoraMesherSX1276");
    }

   private:
    /**
     * @brief Initialize hardware pins and SPI interface
     * 
     * Sets up the SPI communication and configures all necessary pins
     * for communication with the radio module.
     * 
     * @return Result Success if initialization was successful
     */
    Result InitializeHardware();

    const int8_t cs_pin_;     ///< SPI Chip Select pin number
    const int8_t irq_pin_;    ///< Interrupt Request pin number
    const int8_t reset_pin_;  ///< Reset pin number
    const int8_t busy_pin_;   ///< Busy state pin number

    std::unique_ptr<Module>
        hal_module_;  ///< RadioLib hardware abstraction layer
    std::unique_ptr<SX1276>
        radio_module_;  ///< RadioLib LoraMesherSX1276 instance

    bool initialized_ = false;  ///< Boolean when module initialized
};

}  // namespace radio
}  // namespace loramesher