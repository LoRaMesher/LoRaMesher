// src/types/configurations/loramesher_configuration.hpp
#pragma once

#include <cstdint>

#include "pin_configuration.hpp"
#include "protocol_configuration.hpp"
#include "radio_configuration.hpp"

namespace loramesher {

class Config {
   public:
    // Constructor
    explicit Config(
        const PinConfig& pins = PinConfig::createDefault(),
        const RadioConfig& radio = RadioConfig::createDefaultSx1276(),
        const ProtocolConfig& protocol = ProtocolConfig::createDefault(),
        uint32_t sleepDuration = 60000, bool enableDeepSleep = true);

    // Getters
    const PinConfig& getPinConfig() const { return pinConfig_; }
    const RadioConfig& getRadioConfig() const { return radioConfig_; }
    const ProtocolConfig& getProtocolConfig() const { return protocolConfig_; }
    uint32_t getSleepDuration() const { return sleepDuration_; }
    bool isDeepSleepEnabled() const { return enableDeepSleep_; }

    // Setters
    void setPinConfig(const PinConfig& config);
    void setRadioConfig(const RadioConfig& config);
    void setProtocolConfig(const ProtocolConfig& config);
    void setSleepDuration(uint32_t duration);
    void setDeepSleepEnabled(bool enable);

    // Factory method
    static Config createDefault();

    // Validation
    bool isValid() const;
    std::string validate() const;

   private:
    PinConfig pinConfig_;
    RadioConfig radioConfig_;
    ProtocolConfig protocolConfig_;
    uint32_t sleepDuration_;
    bool enableDeepSleep_;
};

}  // namespace loramesher