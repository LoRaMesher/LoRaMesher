#include "radio_configuration.hpp"

#include <sstream>

namespace loramesher {

// RadioConfig Implementation
RadioConfig::RadioConfig(float frequency, uint8_t spreadingFactor,
                         float bandwidth, uint8_t codingRate, uint8_t power)
    : frequency_(frequency),
      spreadingFactor_(spreadingFactor),
      bandwidth_(bandwidth),
      codingRate_(codingRate),
      power_(power) {}

void RadioConfig::setFrequency(float frequency) {
    if (frequency < kMinFrequency || frequency > kMaxFrequency) {
        throw std::invalid_argument("Frequency out of valid range");
    }
    frequency_ = frequency;
}

void RadioConfig::setSpreadingFactor(uint8_t sf) {
    if (sf < kMinSpreadingFactor || sf > kMaxSpreadingFactor) {
        throw std::invalid_argument("Invalid spreading factor");
    }
    spreadingFactor_ = sf;
}

void RadioConfig::setBandwidth(float bandwidth) {
    if (bandwidth <= 0) {
        throw std::invalid_argument("Bandwidth must be positive");
    }
    bandwidth_ = bandwidth;
}

void RadioConfig::setCodingRate(uint8_t codingRate) {
    if (codingRate < 5 || codingRate > 8) {
        throw std::invalid_argument("Coding rate must be between 5 and 8");
    }
    codingRate_ = codingRate;
}

void RadioConfig::setPower(uint8_t power) {
    if (power > 20) {  // Typical LoRa limit
        throw std::invalid_argument("Power exceeds maximum allowed value");
    }
    power_ = power;
}

RadioConfig RadioConfig::createDefault() {
    return RadioConfig{};
}

bool RadioConfig::isValid() const {
    return frequency_ >= kMinFrequency && frequency_ <= kMaxFrequency &&
           spreadingFactor_ >= kMinSpreadingFactor &&
           spreadingFactor_ <= kMaxSpreadingFactor && bandwidth_ > 0 &&
           codingRate_ >= 5 && codingRate_ <= 8 && power_ <= 20;
}

std::string RadioConfig::validate() const {
    std::stringstream errors;
    if (frequency_ < kMinFrequency || frequency_ > kMaxFrequency) {
        errors << "Frequency out of range. ";
    }
    if (spreadingFactor_ < kMinSpreadingFactor ||
        spreadingFactor_ > kMaxSpreadingFactor) {
        errors << "Invalid spreading factor. ";
    }
    if (bandwidth_ <= 0) {
        errors << "Invalid bandwidth. ";
    }
    if (codingRate_ < 5 || codingRate_ > 8) {
        errors << "Invalid coding rate. ";
    }
    if (power_ > 20) {
        errors << "Power exceeds maximum. ";
    }
    return errors.str();
}

}  // namespace loramesher
