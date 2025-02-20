// src/types/radio/radio.hpp
#pragma once

#include <functional>
#include <vector>

#include "radio_event.hpp"
#include "radio_state.hpp"
#include "types/configurations/radio_configuration.hpp"
#include "types/error_codes/result.hpp"

namespace loramesher {
namespace radio {

/**
 * @brief Interface for radio implementations
 * 
 * This interface defines the standard operations that any radio implementation
 * must support. It provides methods for configuration, transmission, reception,
 * and status monitoring of radio modules.
 */
class IRadio {
   public:
    /** @brief Virtual destructor */
    virtual ~IRadio() = default;

    //////////////////
    // Core Operations
    //////////////////

    /**
     * @brief Configure the radio with all parameters at once
     * 
     * @param config Complete radio configuration
     * @return Result Success if configuration was successful
     */
    virtual Result Configure(const RadioConfig& config) = 0;

    /**
   * @brief Initialize the radio module
   * @param config Initial configuration parameters
   * @return true if initialization was successful, false otherwise
   */
    virtual Result Begin(const RadioConfig& config) = 0;

    /**
     * @brief Send data over the radio
     * 
     * @param data Pointer to data buffer to transmit
     * @param len Length of data in bytes
     * @return Result Success if transmission started successfully
     */
    virtual Result Send(const uint8_t* data, size_t len) = 0;

    /**
     * @brief Start the radio in receive mode
     * 
     * @return Result Success if receive mode was started successfully
     */
    virtual Result StartReceive() = 0;

    /**
     * @brief Put the radio into sleep mode
     * 
     * @return Result Success if sleep mode was entered successfully
     */
    virtual Result Sleep() = 0;

    ///////////////////////
    // Parameter Configuration
    ///////////////////////

    /**
     * @brief Set the radio frequency
     * 
     * @param frequency Frequency in MHz
     * @return Result Success if frequency was set successfully
     */
    virtual Result setFrequency(float frequency) = 0;

    /**
     * @brief Set the spreading factor
     * 
     * @param sf Spreading factor (6-12)
     * @return Result Success if spreading factor was set successfully
     */
    virtual Result setSpreadingFactor(uint8_t sf) = 0;

    /**
     * @brief Set the signal bandwidth
     * 
     * @param bandwidth Bandwidth in kHz
     * @return Result Success if bandwidth was set successfully
     */
    virtual Result setBandwidth(float bandwidth) = 0;

    /**
     * @brief Set the coding rate
     * 
     * @param codingRate Coding rate (5-8, representing 4/5 to 4/8)
     * @return Result Success if coding rate was set successfully
     */
    virtual Result setCodingRate(uint8_t codingRate) = 0;

    /**
     * @brief Set the transmission power
     * 
     * @param power Output power in dBm
     * @return Result Success if power was set successfully
     */
    virtual Result setPower(uint8_t power) = 0;

    /**
     * @brief Set the sync word
     * 
     * @param syncWord Sync word value
     * @return Result Success if sync word was set successfully
     */
    virtual Result setSyncWord(uint8_t syncWord) = 0;

    /**
     * @brief Enable or disable CRC checking
     * 
     * @param enable True to enable CRC, false to disable
     * @return Result Success if CRC setting was changed successfully
     */
    virtual Result setCRC(bool enable) = 0;

    /**
     * @brief Set the preamble length
     * 
     * @param length Preamble length in symbols
     * @return Result Success if preamble length was set successfully
     */
    virtual Result setPreambleLength(uint16_t length) = 0;

    /**
   * @brief Sets the callback function for packet reception
   * 
   * @param callback Function pointer to the callback that will be executed when a packet is received.
   * @warning Only used by the low level library. Do not use it in upper layers.
   * @return true If the callback was set successfully
   * @return false If there was an error setting the callback
   */
    virtual Result setActionReceive(void (*callback)(void)) = 0;

    /**
     * @brief Set the action to receive data
     * 
     * @param callback Function to call when data is received
     * @return Result Success if action was set successfully
     */
    virtual Result setActionReceive(
        std::function<void(std::unique_ptr<RadioEvent>)> callback) = 0;

    ///////////////////////
    // Radio Status
    ///////////////////////

    /**
     * @brief Get the current RSSI value
     * 
     * @return int8_t RSSI in dBm
     */
    virtual int8_t getRSSI() = 0;

    /**
     * @brief Get the current SNR value
     * 
     * @return int8_t SNR in dB
     */
    virtual int8_t getSNR() = 0;

    /**
     * @brief Get the last packet's RSSI
     * 
     * @return int8_t Last packet RSSI in dBm
     */
    virtual int8_t getLastPacketRSSI() = 0;

    /**
     * @brief Get the last packet's SNR
     * 
     * @return int8_t Last packet SNR in dB
     */
    virtual int8_t getLastPacketSNR() = 0;

    /**
     * @brief Check if the radio is currently transmitting
     * 
     * @return bool True if radio is transmitting
     */
    virtual bool IsTransmitting() = 0;

    /**
     * @brief Get the current radio frequency
     * 
     * @return float Current frequency in MHz
     */
    virtual float getFrequency() = 0;

    /**
     * @brief Get the current spreading factor
     * 
     * @return uint8_t Current spreading factor
     */
    virtual uint8_t getSpreadingFactor() = 0;

    /**
     * @brief Get the current signal bandwidth
     * 
     * @return float Current bandwidth in kHz
     */
    virtual float getBandwidth() = 0;

    /**
     * @brief Get the current coding rate
     * 
     * @return uint8_t Current coding rate
     */
    virtual uint8_t getCodingRate() = 0;

    /**
     * @brief Get the current transmission power
     * 
     * @return uint8_t Current output power in dBm
     */
    virtual uint8_t getPower() = 0;

    /**
     * @brief Get the length of the received packet
     * 
     * @return uint8_t Length of the received packet
     */
    virtual uint8_t getPacketLength() = 0;

    /**
     * @brief Read the received data from the radio
     * 
     * @param data Buffer to store received data
     * @param len Length of data to read
     * @return Result Success if data was read successfully
     */
    virtual Result readData(uint8_t* data, size_t len) = 0;

    ///////////////////////
    // Event Handling
    ///////////////////////

    /**
     * @brief Set the current radio state
     * 
     * @param state Desired radio state
     * @return Result Success if state was changed successfully
     */
    virtual Result setState(RadioState state) = 0;

    /**
     * @brief Get the current radio state
     * 
     * @return RadioState Current state of the radio
     */
    virtual RadioState getState() = 0;
};

}  // namespace radio
}  // namespace loramesher