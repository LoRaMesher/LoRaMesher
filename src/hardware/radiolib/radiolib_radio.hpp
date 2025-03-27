#pragma once

#include <memory>
#include <mutex>
#include <queue>

#include "config/system_config.hpp"

#include "hardware/SPIMock.hpp"
#include "os/rtos.hpp"
#include "types/configurations/radio_configuration.hpp"
#include "types/messages/message.hpp"
#include "types/radio/radio.hpp"

#ifdef DEBUG
#include "../test/mocks/mock_radio.hpp"
#endif  // DEBUG

namespace loramesher {
namespace radio {

/**
 * @brief Implementation of IRadio interface using RadioLib
 * 
 * This class provides a concrete implementation of the IRadio interface using
 * the RadioLib library. It supports multiple radio modules and implements
 * non-blocking message reception using an internal queue.
 */
class RadioLibRadio : public IRadio {
   public:
    /**
     * @brief Construct a new RadioLib Radio object
     * 
     * @param cs_pin SPI Chip Select pin
     * @param di0_pin DIO0/IRQ pin for interrupts
     * @param rst_pin Reset pin
     * @param busy_pin Busy state pin
     * @param spi Reference to SPI interface
     */
    RadioLibRadio(int cs_pin, int di0_pin, int rst_pin, int busy_pin,
                  SPIClass& spi);

    /**
     * @brief Destroy the RadioLib Radio object
     */
    ~RadioLibRadio() override;

    // Prevent copying
    RadioLibRadio(const RadioLibRadio&) = delete;
    RadioLibRadio& operator=(const RadioLibRadio&) = delete;

    // Allow moving
    RadioLibRadio(RadioLibRadio&&) = default;
    RadioLibRadio& operator=(RadioLibRadio&&) = default;

    /**
     * @brief Configure the radio with specified parameters
     * 
     * @param config Configuration parameters
     * @return Result Success if configuration was successful
     */
    Result Configure(const RadioConfig& config) override;

    /**
     * @brief Begin radio operation
     * 
     * @param config Configuration parameters
     * @return Result Success if radio was started successfully
     * @note This function must be called after Configure
     * @see Configure
     * 
     */
    Result Begin(const RadioConfig& config) override;

    /**
     * @brief Send data over the radio
     * 
     * @param data Pointer to data buffer
     * @param len Length of data in bytes
     * @return Result Success if transmission started successfully
     */
    Result Send(const uint8_t* data, size_t len) override;

    /**
     * @brief Start the radio in receive mode
     * 
     * @return Result Success if receive mode was started successfully
     */
    Result StartReceive() override;

    /**
     * @brief Put the radio into sleep mode
     * 
     * @return Result Success if sleep mode was entered successfully
     */
    Result Sleep() override;

    /**
     * @brief Get the current radio state
     * @return RadioState Current state of the radio
     */
    RadioState getState() override;

    /**
     * @brief Get the current RSSI value
     * 
     * @return int8_t RSSI in dBm, or -255 if radio not initialized
     */
    int8_t getRSSI() override;

    /**
     * @brief Get the current SNR value
     * 
     * @return int8_t SNR in dB, or -128 if radio not initialized
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
    bool IsTransmitting() override;

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
    Result setPower(int8_t power) override;

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
     * @brief Set callback for received messages
     * 
     * The callback will be invoked for each received message. Messages are
     * queued internally to prevent blocking the radio during processing.
     * 
     * @param callback Function to call for each received message
     */
    Result setActionReceive(
        std::function<void(std::unique_ptr<RadioEvent>)> callback) override;

    /**
     * @brief Set the current radio state
     * @param state Desired radio state
     * @return Result Success if state was changed successfully
     */
    Result setState(RadioState state) override;

    /* Unused functions */

    /**
     * @brief Sets the callback function for packet reception
     * 
     * @param callback Function pointer to the callback that will be executed when a packet is received
     * @return true If the callback was set successfully
     * @return false If there was an error setting the callback
     */
    Result setActionReceive(void (*callback)(void)) {
        // Prevent unused parameter warnings
        (void)callback;
        throw std::runtime_error(
            "setActionReceive not supported in RadioLibRadio");
    }

    uint8_t getPacketLength() override {
        throw std::runtime_error(
            "getPacketLength not supported in RadioLibRadio");
    }

    Result readData(uint8_t* data, size_t len) override {
        // Prevent unused parameter warnings
        (void)data;
        (void)len;

        throw std::runtime_error("readData not supported in RadioLibRadio");
    }

    Result ClearActionReceive() override {
        throw std::runtime_error(
            "ClearActionReceive not supported in RadioLibRadio");
    }

#ifdef DEBUG
    /**
     * @brief Get the mock radio from RadioLibRadio for testing purposes
     * 
     * This function allows tests to access the mock radio inside RadioLibRadio
     * to set expectations.
     * 
     * @param radio The RadioLibRadio instance
     * @return test::MockRadio& Reference to the mock radio for setting expectations
     * @throws std::runtime_error if the current module is not a MockRadio
     */
    friend test::MockRadio& GetRadioLibMockForTesting(RadioLibRadio& radio);
#endif  // DEBUG

   protected:
    /**
     * @brief Create appropriate radio module based on type
     * 
     * @param type Type of radio module to create
     * @return bool True if module was created successfully
     */
    bool CreateRadioModule(RadioType type);

    /**
     * @brief Static ISR callback for radio events
     * 
     * Static wrapper for the interrupt handler. Required because attachInterrupt
     * only accepts static functions or plain C-style function pointers.
     */
    static ISR_ATTR HandleInterruptStatic();

    /**
     * @brief Handle interrupt from radio module
     * 
     * This method is called when data is received. It reads the data,
     * creates a RadioEvent, and adds it to the receive queue.
     */
    void HandleInterrupt();

    /**
     * @brief Process radio events in a separate task
     * 
     * Task function to process radio events in a separate task to avoid blocking
     * the main loop. This function reads events from the event queue and calls the
     * receive callback for each event.
     * 
     * @param parameters Pointer to the RadioLibRadio instance
     */
    static void ProcessEvents(void* parameters);

    /**
     * @brief Pointer to the current instance for interrupt handling
     * 
     * Used by the static interrupt handler to call the member function
     * of the correct instance.
     */
    static RadioLibRadio* instance_;

    // Hardware configuration
    const int cs_pin_;    ///< SPI Chip Select pin
    const int di0_pin_;   ///< DIO0/IRQ pin
    const int rst_pin_;   ///< Reset pin
    const int busy_pin_;  ///< Busy pin
    SPIClass& spi_;       ///< SPI interface reference

    // Radio module and state
    std::unique_ptr<IRadio> current_module_;  ///< Current radio module
    std::function<void(std::unique_ptr<RadioEvent>)>
        receive_callback_;  ///< Callback for received data, this callback
                            ///< should move the event to a queue and
                            ///< process it in other tasks

    RadioState current_state_{RadioState::kIdle};  ///< Current radio state
    RadioConfig current_config_;  ///< Current radio configuration

    int8_t last_packet_rssi_{0};  ///< RSSI of last received packet
    int8_t last_packet_snr_{0};   ///< SNR of last received packet

    // Message queue for received data
    os::QueueHandle_t receive_queue_{nullptr};  // Queue for radio events
    os::TaskHandle_t processing_task_{
        nullptr};  // Handle to the event processing task

    // Mutex for thread safety
    std::mutex radio_mutex_;
};

/**
 * @brief Create a new RadioLib radio instance
 * 
 * Factory function to create a new RadioLibRadio instance.
 * 
 * @param cs_pin SPI Chip Select pin
 * @param di0_pin DIO0/IRQ pin
 * @param rst_pin Reset pin
 * @param busy_pin Busy pin
 * @param spi Reference to SPI interface
 * @return std::unique_ptr<IRadio> New radio instance
 */
inline std::unique_ptr<IRadio> CreateRadio(int cs_pin, int di0_pin, int rst_pin,
                                           int busy_pin, SPIClass& spi) {
    return std::make_unique<RadioLibRadio>(cs_pin, di0_pin, rst_pin, busy_pin,
                                           spi);
}

}  // namespace radio
}  // namespace loramesher