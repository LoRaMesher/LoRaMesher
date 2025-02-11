// src/types/configurations/radio_configuration.hpp
#pragma once

#include <cstdint>
#include <string>

namespace loramesher {

/**
 * @brief Enumeration of supported radio hardware types
 */
enum class RadioType {
    kSx1276,  ///< Semtech SX1276 radio module
    kSx1278   ///< Semtech SX1278 radio module
    // TODO: Implement these types
    // kRfm95,   ///< HopeRF RFM95 radio module
    // kRfm96    ///< HopeRF RFM96 radio module
};

/**
 * @brief Configuration class for LoRa radio parameters
 * 
 * This class encapsulates all configurable parameters for LoRa radio operation
 * including frequency, spreading factor, bandwidth, coding rate, and power settings.
 */
class RadioConfig {
   public:
    /**
   * @brief Construct a new Radio Config object
   * 
   * @param type Radio hardware type (default: SX1276)
   * @param frequency Operating frequency in MHz (default: 869.900MHz)
   * @param spreadingFactor LoRa spreading factor (default: SF7)
   * @param bandwidth Signal bandwidth in kHz (default: 125kHz)
   * @param codingRate Error coding rate (default: 4/5)
   * @param power Transmission power in dBm (default: 17dBm)
   */
    explicit RadioConfig(RadioType type = RadioType::kSx1276,
                         float frequency = 869.900F,
                         uint8_t spreadingFactor = 7, float bandwidth = 125.0,
                         uint8_t codingRate = 5, uint8_t power = 17);

    /**
   * @brief Get the configured radio type
   * @return RadioType Current radio hardware type
   */
    RadioType getRadioType() const { return radio_type_; }

    /**
   * @brief Get the configured frequency
   * @return float Frequency in MHz
   */
    float getFrequency() const { return frequency_; }

    /**
   * @brief Get the configured spreading factor
   * @return uint8_t Spreading factor value
   */
    uint8_t getSpreadingFactor() const { return spreadingFactor_; }

    /**
   * @brief Get the configured bandwidth
   * @return float Bandwidth in kHz
   */
    float getBandwidth() const { return bandwidth_; }

    /**
   * @brief Get the configured coding rate
   * @return uint8_t Coding rate value
   */
    uint8_t getCodingRate() const { return codingRate_; }

    /**
   * @brief Get the configured transmission power
   * @return uint8_t Power in dBm
   */
    uint8_t getPower() const { return power_; }

    /**
   * @brief Set the radio hardware type
   * @param type New radio type to use
   */
    void setRadioType(RadioType type) { radio_type_ = type; }

    /**
   * @brief Set the operating frequency
   * 
   * @param frequency New frequency in MHz
   * @throw std::invalid_argument if frequency is outside valid range
   */
    void setFrequency(float frequency);

    /**
   * @brief Set the spreading factor
   * 
   * @param sf New spreading factor value (6-12)
   * @throw std::invalid_argument if spreading factor is outside valid range
   */
    void setSpreadingFactor(uint8_t sf);

    /**
   * @brief Set the signal bandwidth
   * 
   * @param bandwidth New bandwidth in kHz
   * @throw std::invalid_argument if bandwidth is invalid
   */
    void setBandwidth(float bandwidth);

    /**
   * @brief Set the coding rate
   * 
   * @param codingRate New coding rate value
   * @throw std::invalid_argument if coding rate is invalid
   */
    void setCodingRate(uint8_t codingRate);

    /**
   * @brief Set the transmission power
   * 
   * @param power New power value in dBm
   * @throw std::invalid_argument if power is outside valid range
   */
    void setPower(uint8_t power);

    /**
   * @brief Create default configuration for SX1276
   * @return RadioConfig Optimized configuration for SX1276
   */
    static RadioConfig createDefaultSx1276();

    /**
   * @brief Create default configuration for SX1278
   * @return RadioConfig Optimized configuration for SX1278
   */
    static RadioConfig createDefaultSx1278();

    /**
   * @brief Check if the configuration is valid
   * @return bool True if all parameters are within valid ranges
   */
    bool isValid() const;

    /**
   * @brief Get detailed validation messages
   * @return std::string Description of any validation errors
   */
    std::string validate() const;

   private:
    static constexpr float kMinFrequency = 137.0F;  ///< Minimum valid frequency
    static constexpr float kMaxFrequency =
        1020.0F;  ///< Maximum valid frequency
    static constexpr uint8_t kMinSpreadingFactor =
        6;  ///< Minimum spreading factor
    static constexpr uint8_t kMaxSpreadingFactor =
        12;  ///< Maximum spreading factor

    RadioType radio_type_;     ///< Radio hardware type
    float frequency_;          ///< Operating frequency in MHz
    uint8_t spreadingFactor_;  ///< LoRa spreading factor
    float bandwidth_;          ///< Signal bandwidth in kHz
    uint8_t codingRate_;       ///< Error coding rate
    uint8_t power_;            ///< Transmission power in dBm
};

}  // namespace loramesher