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
    if (!config.isValid()) {
        throw std::invalid_argument("Invalid pin configuration: " +
                                    config.validate());
    }
    pinConfig_ = config;
}

void Config::setRadioConfig(const RadioConfig& config) {
    if (!config.isValid()) {
        throw std::invalid_argument("Invalid radio configuration: " +
                                    config.validate());
    }
    radioConfig_ = config;
}

void Config::setProtocolConfig(const ProtocolConfig& config) {
    if (!config.isValid()) {
        throw std::invalid_argument("Invalid protocol configuration: " +
                                    config.validate());
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

Config Config::createDefault() {
    return Config{};
}

bool Config::isValid() const {
    return pinConfig_.isValid() && radioConfig_.isValid() &&
           protocolConfig_.isValid() && sleepDuration_ > 0;
}

std::string Config::validate() const {
    std::stringstream errors;
    if (!pinConfig_.isValid()) {
        errors << "Pin config errors: " << pinConfig_.validate();
    }
    if (!radioConfig_.isValid()) {
        errors << "Radio config errors: " << radioConfig_.validate();
    }
    if (!protocolConfig_.isValid()) {
        errors << "Protocol config errors: " << protocolConfig_.validate();
    }
    if (sleepDuration_ == 0) {
        errors << "Invalid sleep duration. ";
    }
    return errors.str();
}

}  // namespace loramesher