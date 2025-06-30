// src/types/configurations/loramesher_configuration.hpp
#pragma once

#include <cstdint>

#include "pin_configuration.hpp"
#include "protocol_configuration.hpp"
#include "radio_configuration.hpp"

namespace loramesher {

/**
 * @brief Configuration class for LoRaMesher communication system
 *
 * This class encapsulates all configuration parameters for the LoRaMesher system,
 * including pin configurations, radio settings, protocol parameters, and power management.
 */
class Config {
   public:
    /**
     * @brief Constructs a Config object with specified parameters
     *
     * @param pins Pin configuration for the hardware interface
     * @param radio Radio parameters for the LoRa transceiver
     * @param protocol Protocol-specific configuration settings
     * @param sleepDuration Duration of sleep period in milliseconds
     * @param enableDeepSleep Flag to enable/disable deep sleep functionality
     */
    explicit Config(
        const PinConfig& pins = PinConfig::CreateDefault(),
        const RadioConfig& radio = RadioConfig::CreateDefaultSx1276(),
        const ProtocolConfig& protocol = ProtocolConfig::CreateDefault(),
        uint32_t sleepDuration = 60000, bool enableDeepSleep = true);

    /**
     * @brief Gets the pin configuration
     * @return Reference to the current pin configuration
     */
    const PinConfig& getPinConfig() const { return pinConfig_; }

    /**
     * @brief Gets the radio configuration
     * @return Reference to the current radio configuration
     */
    const RadioConfig& getRadioConfig() const { return radioConfig_; }

    /**
     * @brief Gets the protocol configuration
     * @return Reference to the current protocol configuration
     */
    const ProtocolConfig& getProtocolConfig() const { return protocolConfig_; }

    /**
     * @brief Gets the sleep duration in milliseconds
     * @return Current sleep duration value
     */
    uint32_t getSleepDuration() const { return sleepDuration_; }

    /**
     * @brief Gets the deep sleep enable status
     * @return True if deep sleep is enabled, false otherwise
     */
    bool getDeepSleepEnabled() const { return enableDeepSleep_; }

    /**
     * @brief Sets the pin configuration
     * @param config New pin configuration to apply
     */
    void setPinConfig(const PinConfig& config);

    /**
     * @brief Sets the radio configuration
     * @param config New radio configuration to apply
     */
    void setRadioConfig(const RadioConfig& config);

    /**
     * @brief Sets the protocol configuration
     * @param config New protocol configuration to apply
     */
    void setProtocolConfig(const ProtocolConfig& config);

    /**
     * @brief Sets the sleep duration
     * @param duration Sleep duration in milliseconds
     */
    void setSleepDuration(uint32_t duration);

    /**
     * @brief Enables or disables deep sleep functionality
     * @param enable True to enable deep sleep, false to disable
     */
    void setDeepSleepEnabled(bool enable);

    /**
     * @brief Creates a configuration with default values
     * @return Default configuration object
     */
    static Config CreateDefault();

    /**
     * @brief Validates the configuration
     * @return True if configuration is valid, false otherwise
     */
    bool IsValid() const;

    /**
     * @brief Validates the configuration and provides error details
     * @return Empty string if valid, otherwise error description
     */
    std::string Validate() const;

   private:
    PinConfig pinConfig_;            ///< Pin configuration settings
    RadioConfig radioConfig_;        ///< Radio hardware configuration
    ProtocolConfig protocolConfig_;  ///< Protocol-specific settings

    uint32_t sleepDuration_;  ///< Sleep duration in milliseconds
    bool enableDeepSleep_;    ///< Flag controlling deep sleep functionality
};

}  // namespace loramesher