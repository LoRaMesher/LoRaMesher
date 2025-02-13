#pragma once

#ifdef LORAMESHER_BUILD_ARDUINO

#include <memory>
#include <mutex>
#include <queue>

#include "config/system_config.hpp"
#include "types/configurations/radio_configuration.hpp"
#include "types/messages/message.hpp"
#include "types/radio/radio.hpp"

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
     * @param spi Reference to SPI interface
     */
    RadioLibRadio(int cs_pin, int di0_pin, int rst_pin, SPIClass& spi);

    /**
     * @brief Destroy the RadioLib Radio object
     */
    ~RadioLibRadio() override = default;

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
     * @brief Set callback for received messages
     * 
     * The callback will be invoked for each received message. Messages are
     * queued internally to prevent blocking the radio during processing.
     * 
     * @param callback Function to call for each received message
     */
    void setReceiveCallback(std::function<void(RadioEvent&)> callback) override;

   private:
    /**
     * @brief Create appropriate radio module based on type
     * 
     * @param type Type of radio module to create
     * @return bool True if module was created successfully
     */
    bool CreateRadioModule(RadioType type);

    /**
     * @brief Handle interrupt from radio module
     * 
     * This method is called when data is received. It reads the data,
     * creates a RadioEvent, and adds it to the receive queue.
     * 
     * @param context Context pointer (unused)
     */
    void HandleInterrupt(void* context);

    /**
     * @brief Process messages in the receive queue
     * 
     * Calls the receive callback for each queued message.
     */
    void ProcessQueuedMessages();

    // Hardware configuration
    const int cs_pin_;   ///< SPI Chip Select pin
    const int di0_pin_;  ///< DIO0/IRQ pin
    const int rst_pin_;  ///< Reset pin
    SPIClass& spi_;      ///< SPI interface reference

    // Radio module and state
    std::unique_ptr<IRadio> current_module_;  ///< Current radio module
    std::function<void(RadioEvent&)> receive_callback_;  ///< Receive callback

    // Message queue for received data
    std::queue<std::unique_ptr<RadioEvent>> receive_queue_;

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
 * @param spi Reference to SPI interface
 * @return std::unique_ptr<IRadio> New radio instance
 */
inline std::unique_ptr<IRadio> CreateRadio(int cs_pin, int di0_pin, int rst_pin,
                                           SPIClass& spi) {
    return std::make_unique<RadioLibRadio>(cs_pin, di0_pin, rst_pin, spi);
}

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO