// src/types/configurations/pin_configuration.hpp
#pragma once

#include <cstdint>
#include <string>

namespace loramesher {

class PinConfig {
   public:
    // TODO: Add RADIOLIB_NC for unused pins mapping
    // Constructor with default values
    explicit PinConfig(int8_t nss = 18, int8_t reset = 23, int8_t dio0 = 26,
                       int8_t dio1 = 33);

    // Getters
    int8_t getNss() const { return nss_; }
    int8_t getReset() const { return reset_; }
    int8_t getDio0() const { return dio0_; }
    int8_t getDio1() const { return dio1_; }

    // Setters with validation
    void setNss(int8_t nss);
    void setReset(int8_t reset);
    void setDio0(int8_t dio0);
    void setDio1(int8_t dio1);

    // Factory method
    static PinConfig CreateDefault();

    // Validation
    bool IsValid() const;
    std::string Validate() const;

   private:
    int8_t nss_;
    int8_t reset_;
    int8_t dio0_;
    int8_t dio1_;
};

}  // namespace loramesher