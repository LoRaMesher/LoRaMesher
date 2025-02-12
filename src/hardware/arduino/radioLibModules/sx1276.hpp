// src/hardware/arduino/radioLibModules/LoraMesherSX1276.hpp
#pragma once

#include "build_options.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include <memory>

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
                     int8_t busy_pin);

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
    ~LoraMesherSX1276() override;

    // IRadio interface implementation
    /**
     * @brief Configure the radio with the specified parameters
     * @param config Complete radio configuration parameters
     * @return Result Success if configuration was successful
     */
    Result configure(const RadioConfig& config) override;

    /**
     * @brief Initialize and begin radio operations
     * @param config Initial configuration parameters
     * @return Result Success if initialization was successful
     */
    Result begin(const RadioConfig& config) override;

    /**
     * @brief Transmit data over the radio
     * @param data Pointer to data buffer to transmit
     * @param len Length of data in bytes
     * @return Result Success if transmission started successfully
     */
    Result send(const uint8_t* data, size_t len) override;

    /**
     * @brief Start continuous receive mode
     * @return Result Success if receive mode was started successfully
     */
    Result startReceive() override;

    /**
     * @brief Enter sleep mode to conserve power
     * @return Result Success if sleep mode was entered successfully
     */
    Result sleep() override;

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
     * @brief Get the RSSI of the last received packet
     * @return int8_t Last packet RSSI in dBm
     */
    int8_t getLastPacketRSSI() override;

    /**
     * @brief Get the SNR of the last received packet
     * @return int8_t Last packet SNR in dB
     */
    int8_t getLastPacketSNR() override;

    /**
     * @brief Check if the radio is currently transmitting
     * @return bool True if radio is transmitting
     */
    bool isTransmitting() override;

    /**
     * @brief Get the current radio frequency
     * @return float Current frequency in MHz
     */
    float getFrequency() override;

    /**
     * @brief Get the current spreading factor
     * @return uint8_t Current spreading factor
     */
    uint8_t getSpreadingFactor() override;

    /**
     * @brief Get the current signal bandwidth
     * @return float Current bandwidth in kHz
     */
    float getBandwidth() override;

    /**
     * @brief Get the current coding rate
     * @return uint8_t Current coding rate
     */
    uint8_t getCodingRate() override;

    /**
     * @brief Get the current transmission power
     * @return uint8_t Current output power in dBm
     */
    uint8_t getPower() override;

    /**
     * @brief Set callback for received messages
     * @param callback Function to call when data is received
     */
    void setReceiveCallback(std::function<void(RadioEvent&)> callback) override;

    /**
     * @brief Set the current radio state
     * @param state Desired radio state
     * @return Result Success if state was changed successfully
     */
    Result setState(RadioState state) override;

    /**
     * @brief Get the current radio state
     * @return RadioState Current state of the radio
     */
    RadioState getState() override;

   private:
    /**
     * @brief Initialize hardware pins and SPI interface
     * 
     * Sets up the SPI communication and configures all necessary pins
     * for communication with the radio module.
     * 
     * @return Result Success if initialization was successful
     */
    Result initializeHardware();

    /**
     * @brief ISR callback for radio events
     * 
     * Handles interrupt events from the radio module, including
     * received data, transmission complete, and other events.
     */
    void handleInterrupt();

    /**
     * @brief Static ISR callback for radio events
     * 
     * Static wrapper for the interrupt handler. Required because attachInterrupt
     * only accepts static functions or plain C-style function pointers.
     */
    static void ICACHE_RAM_ATTR handleInterruptStatic();

    /**
     * @brief Process radio events in a separate task
     * 
     * Task function to process radio events in a separate task to avoid blocking
     * the main loop. This function reads events from the event queue and calls the
     * receive callback for each event.
     * 
     * @param parameters Task parameters (unused)
     */
    static void processEvents(void* parameters);

    /**
     * @brief Pointer to the current instance for interrupt handling
     * 
     * Used by the static interrupt handler to call the member function
     * of the correct instance.
     */
    static LoraMesherSX1276* instance_;

    const int8_t cs_pin_;     ///< SPI Chip Select pin number
    const int8_t irq_pin_;    ///< Interrupt Request pin number
    const int8_t reset_pin_;  ///< Reset pin number
    const int8_t busy_pin_;   ///< Busy state pin number

    std::unique_ptr<Module>
        hal_module_;  ///< RadioLib hardware abstraction layer
    std::unique_ptr<SX1276>
        radio_module_;  ///< RadioLib LoraMesherSX1276 instance

    RadioState current_state_{RadioState::kIdle};  ///< Current radio state
    RadioConfig current_config_;  ///< Current radio configuration

    std::function<void(RadioEvent&)>
        receive_callback_;  ///< Callback for received data, this callback
                            ///< should move the event to a queue and
                            ///< process it in other tasks

    QueueHandle_t event_queue_;     // Queue for radio events
    TaskHandle_t processing_task_;  // Handle to the event processing task

    int8_t last_packet_rssi_{0};  ///< RSSI of last received packet
    int8_t last_packet_snr_{0};   ///< SNR of last received packet
};

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO