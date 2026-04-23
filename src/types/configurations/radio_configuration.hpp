// src/types/configurations/radio_configuration.hpp
#pragma once

#include <cstdint>
#include <string>

#include "types/error_codes/result.hpp"

namespace loramesher {

/**
 * @brief Enumeration of supported radio hardware types
 */
enum class RadioType {
    kSx1276,    ///< Semtech SX1276 radio module
    kSx1278,    ///< Semtech SX1278 radio module
    kSx1262,    ///< Semtech SX1262 radio module (SX126x family)
    kMockRadio  ///< Mock radio module for testing
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
     * @param sync_word Syncronization word (default: 20U)
     * @param crc CRC enabled (default: true)
     * @param preamble_length Length of LoRa transmission preamble in symbols. 
     * The actual preamble length is 4.25 symbols longer than the set number. (default: 8U)
     */
    explicit RadioConfig(RadioType type = RadioType::kSx1276,
                         float frequency = 869.900F,
                         uint8_t spreadingFactor = 7, float bandwidth = 125.0,
                         uint8_t codingRate = 5, int8_t power = 17,
                         uint8_t sync_word = 20U, bool crc = true,
                         uint16_t preamble_length = 8U);

    /**
     * @brief Create default configuration for SX1276
     * @return RadioConfig Optimized configuration for SX1276
     */
    static RadioConfig CreateDefaultSx1276();

    /**
     * @brief Create default configuration for SX1278
     * @return RadioConfig Optimized configuration for SX1278
     */
    static RadioConfig CreateDefaultSx1278();

    /**
     * @brief Create default configuration for SX1262
     * @return RadioConfig Optimized configuration for SX1262
     */
    static RadioConfig CreateDefaultSx1262();

    /**
     * @brief Get the configured radio type
     * @return RadioType Current radio hardware type
     */
    RadioType getRadioType() const { return radio_type_; }

    /**
     * @brief Converts RadioType enum to its string representation
     * 
     * @return std::string String representation of the RadioType
     */
    std::string getRadioTypeString() const;

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
     * @brief Get the configured sync word
     * @return uint8_t Sync word value
     */
    uint8_t getSyncWord() const { return sync_word_; }

    /**
     * @brief Get the configured transmission power
     * @return int8_t Power in dBm
     */
    int8_t getPower() const { return power_; }

    /**
     * @brief Get the configured preamble length
     * @return uint16_t Preamble length
     */
    uint16_t getPreambleLength() const { return preamble_length_; }

    /**
     * @brief Get the configured CRC
     * @return bool CRC
     */
    bool getCRC() const { return crc_; }

    /**
     * @brief Get the configured TCXO reference voltage
     * @return float TCXO voltage (0.0 = no TCXO / external crystal)
     */
    float getTcxoVoltage() const { return tcxo_voltage_; }

    /**
     * @brief Get the configured OCP current limit
     * @return float Current limit in mA (0.0 = auto mode)
     */
    float getCurrentLimit() const { return current_limit_ma_; }

    /**
     * @brief Check if the current limit is in auto mode
     * @return bool True if OCP will be auto-set based on TX power
     */
    bool IsCurrentLimitAuto() const {
        return current_limit_ma_ == kAutoCurrentLimit;
    }

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
    void setPower(int8_t power);

    /**
     * @brief Set syncronyzation word
     * 
     * @param sync_word New Sync word
     * @return Result Success if correctly set
     */
    Result setSyncWord(uint8_t sync_word);

    /**
     * @brief Set CRC
     * 
     * @param enabled True if enabled
     * @return Result Success if correctly setted
     */
    Result setCRC(bool enabled);

    /**
     * @brief Set preamble length
     *
     * @param preamble_length Length of LoRa transmission preamble in symbols.
     * The actual preamble length is 4.25 symbols longer than the set number.
     * @return Result Success if correctly setted
     */
    Result setPreambleLength(uint16_t preamble_length);

    /**
     * @brief Set the OCP current limit manually
     *
     * Set to 0.0 for auto mode (recommended). In auto mode, the library
     * sets the appropriate OCP limit based on the TX power and radio type.
     *
     * @param current_limit_ma Current limit in mA (0.0 for auto, or 45-240)
     * @throw std::invalid_argument if value is outside valid range
     */
    void setCurrentLimit(float current_limit_ma);

    /**
     * @brief Get the recommended OCP current limit for a given radio and power
     *
     * @param type Radio hardware type
     * @param power TX power in dBm
     * @return float Recommended current limit in mA
     */
    static float RecommendedCurrentLimit(RadioType type, int8_t power);

    /**
     * @brief Recommended max_packet_size for the given spreading factor and bandwidth
     *
     * Derived from the LoRaWAN EU868 regional parameters at BW125 kHz and
     * scaled proportionally for higher bandwidths (BW250 -> x2, BW500 -> x4),
     * clamped to the 255-byte LoRa PHY ceiling. Callers use this to size
     * TDMA slot durations so that time-on-air stays within practical bounds
     * at high SF where symbol time is long.
     *
     * Table at BW125 kHz: SF7/SF8 = 242, SF9 = 115, SF10/SF11/SF12 = 51.
     *
     * @param sf Spreading factor (6–12)
     * @param bw_khz Bandwidth in kHz (e.g. 125.0, 250.0, 500.0)
     * @return uint8_t Recommended max_packet_size in bytes (range [1, 255])
     */
    static uint8_t GetMaxPacketSizeForSf(uint8_t sf, float bw_khz);

    /**
     * @brief Set TCXO reference voltage for SX1262 modules
     *
     * Boards using an SX1262 with a TCXO instead of an external crystal
     * (e.g. Heltec WiFi LoRa V3) require a non-zero voltage, typically 1.8V.
     * Set to 0.0 for boards with an external crystal.
     *
     * @param voltage TCXO voltage in volts (0.0–3.3)
     * @throw std::invalid_argument if voltage is outside valid range
     */
    void setTcxoVoltage(float voltage);

    /**
     * @brief Check if the configuration is valid
     * @return bool True if all parameters are within valid ranges
     */
    bool IsValid() const;

    /**
     * @brief Get detailed validation messages
     * @return std::string Description of any validation errors
     */
    std::string Validate() const;

    /// Sentinel value: auto-set OCP based on TX power
    static constexpr float kAutoCurrentLimit = 0.0F;

   private:
    static constexpr float kMinFrequency = 137.0F;  ///< Minimum valid frequency
    static constexpr float kMaxFrequency =
        1020.0F;  ///< Maximum valid frequency
    static constexpr uint8_t kMinSpreadingFactor =
        6;  ///< Minimum spreading factor
    static constexpr uint8_t kMaxSpreadingFactor =
        12;  ///< Maximum spreading factor

    RadioType radio_type_;      ///< Radio hardware type
    float frequency_;           ///< Operating frequency in MHz
    uint8_t spreadingFactor_;   ///< LoRa spreading factor
    float bandwidth_;           ///< Signal bandwidth in kHz
    uint8_t codingRate_;        ///< Error coding rate
    int8_t power_;              ///< Transmission power in dBm
    uint8_t sync_word_;         ///< Syncronyzation word
    bool crc_;                  ///< CRC enabled
    uint16_t preamble_length_;  ///< Preamble length
    float tcxo_voltage_;        ///< TCXO reference voltage (0 = no TCXO)
    float current_limit_ma_{kAutoCurrentLimit};  ///< OCP limit (0 = auto)
};

}  // namespace loramesher