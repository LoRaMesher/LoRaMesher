// src/types/configurations/radio_configuration.hpp
#pragma once

#include <cstdint>
#include <string>

namespace loramesher {

class RadioConfig {
   public:
    // Constructor with default values
    explicit RadioConfig(float frequency = 869.900F,
                         uint8_t spreadingFactor = 7, float bandwidth = 125.0,
                         uint8_t codingRate = 5, uint8_t power = 17);

    // Getters
    float getFrequency() const { return frequency_; }
    uint8_t getSpreadingFactor() const { return spreadingFactor_; }
    float getBandwidth() const { return bandwidth_; }
    uint8_t getCodingRate() const { return codingRate_; }
    uint8_t getPower() const { return power_; }

    // Setters with validation
    void setFrequency(float frequency);
    void setSpreadingFactor(uint8_t sf);
    void setBandwidth(float bandwidth);
    void setCodingRate(uint8_t codingRate);
    void setPower(uint8_t power);

    // Factory method
    static RadioConfig createDefault();

    // Validation
    bool isValid() const;
    std::string validate() const;

   private:
    static constexpr float kMinFrequency = 137.0F;
    static constexpr float kMaxFrequency = 1020.0F;
    static constexpr uint8_t kMinSpreadingFactor = 6;
    static constexpr uint8_t kMaxSpreadingFactor = 12;

    float frequency_;
    uint8_t spreadingFactor_;
    float bandwidth_;
    uint8_t codingRate_;
    uint8_t power_;
};

}  // namespace loramesher