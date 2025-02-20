// src/types/configurations/pin_configuration.hpp
#pragma once

#include <cstdint>
#include <string>

namespace loramesher {

/**
 * @brief Configuration class for hardware pin assignments
 *
 * Manages the pin configuration for LoRa radio modules, including
 * chip select (NSS), reset, and interrupt pins (DIO0, DIO1).
 */
class PinConfig {
   public:
    // TODO: Add RADIOLIB_NC for unused pins mapping
    /**
     * @brief Constructs a PinConfig object with specified pin assignments
     *
     * @param nss Chip select pin number
     * @param reset Reset pin number 
     * @param dio0 Interrupt pin DIO0 number
     * @param dio1 Interrupt pin DIO1 number
     */
    explicit PinConfig(int8_t nss = 18, int8_t reset = 23, int8_t dio0 = 26,
                       int8_t dio1 = 33);

    /**
     * @brief Gets the chip select pin number
     * @return Current NSS pin assignment
     */
    int8_t getNss() const { return nss_; }

    /**
     * @brief Gets the reset pin number
     * @return Current reset pin assignment
     */
    int8_t getReset() const { return reset_; }

    /**
     * @brief Gets the DIO0 interrupt pin number
     * @return Current DIO0 pin assignment
     */
    int8_t getDio0() const { return dio0_; }

    /**
     * @brief Gets the DIO1 interrupt pin number
     * @return Current DIO1 pin assignment
     */
    int8_t getDio1() const { return dio1_; }

    /**
     * @brief Sets the chip select pin with validation
     * @param nss New NSS pin number to assign
     */
    void setNss(int8_t nss);

    /**
     * @brief Sets the reset pin with validation
     * @param reset New reset pin number to assign
     */
    void setReset(int8_t reset);

    /**
     * @brief Sets the DIO0 interrupt pin with validation
     * @param dio0 New DIO0 pin number to assign
     */
    void setDio0(int8_t dio0);

    /**
     * @brief Sets the DIO1 interrupt pin with validation 
     * @param dio1 New DIO1 pin number to assign
     */
    void setDio1(int8_t dio1);

    /**
     * @brief Creates a pin configuration with default values
     * @return Default pin configuration object
     */
    static PinConfig CreateDefault();

    /**
     * @brief Validates the pin configuration
     * @return True if configuration is valid, false otherwise
     */
    bool IsValid() const;

    /**
     * @brief Validates the pin configuration and provides error details
     * @return Empty string if valid, otherwise error description
     */
    std::string Validate() const;

   private:
    int8_t nss_;    ///< Chip select pin number
    int8_t reset_;  ///< Reset pin number
    int8_t dio0_;   ///<  Interrupt pin DIO0 number
    int8_t dio1_;   ///< Interrupt pin DIO1 number
};

}  // namespace loramesher