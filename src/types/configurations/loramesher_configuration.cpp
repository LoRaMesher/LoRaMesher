#include "loramesher_configuration.hpp"

#include <sstream>

namespace loramesher {

// Config Implementation
Config::Config(const PinConfig& pins, const RadioConfig& radio,
               const ProtocolConfig& protocol, uint32_t sleepDuration,
               bool enableDeepSleep)
    : pinConfig_(pins),
      radioConfig_(radio),
      protocolConfig_(protocol),
      sleepDuration_(sleepDuration),
      enableDeepSleep_(enableDeepSleep) {}

void Config::setPinConfig(const PinConfig& config) {
    if (!config.IsValid()) {
        throw std::invalid_argument("Invalid pin configuration: " +
                                    config.Validate());
    }
    pinConfig_ = config;
}

void Config::setRadioConfig(const RadioConfig& config) {
    if (!config.IsValid()) {
        throw std::invalid_argument("Invalid radio configuration: " +
                                    config.Validate());
    }
    radioConfig_ = config;
}

void Config::setProtocolConfig(const ProtocolConfig& config) {
    if (!config.IsValid()) {
        throw std::invalid_argument("Invalid protocol configuration: " +
                                    config.Validate());
    }
    protocolConfig_ = config;
}

void Config::setSleepDuration(uint32_t duration) {
    if (duration == 0) {
        throw std::invalid_argument("Sleep duration must be greater than 0");
    }
    sleepDuration_ = duration;
}

void Config::setDeepSleepEnabled(bool enable) {
    enableDeepSleep_ = enable;
}

Config Config::CreateDefault() {
    return Config{};
}

bool Config::IsValid() const {
    return pinConfig_.IsValid() && radioConfig_.IsValid() &&
           protocolConfig_.IsValid() && sleepDuration_ > 0;
}

std::string Config::Validate() const {
    std::stringstream errors;
    if (!pinConfig_.IsValid()) {
        errors << "Pin config errors: " << pinConfig_.Validate();
    }
    if (!radioConfig_.IsValid()) {
        errors << "Radio config errors: " << radioConfig_.Validate();
    }
    if (!protocolConfig_.IsValid()) {
        errors << "Protocol config errors: " << protocolConfig_.Validate();
    }
    if (sleepDuration_ == 0) {
        errors << "Invalid sleep duration. ";
    }
    return errors.str();
}

}  // namespace loramesher