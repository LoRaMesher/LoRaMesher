#include "radio_configuration.hpp"

#include <sstream>

namespace loramesher {

// RadioConfig Implementation
RadioConfig::RadioConfig(RadioType type, float frequency,
                         uint8_t spreadingFactor, float bandwidth,
                         uint8_t codingRate, int8_t power, uint8_t sync_word,
                         bool crc, uint16_t preamble_length)
    : radio_type_(type),
      frequency_(frequency),
      spreadingFactor_(spreadingFactor),
      bandwidth_(bandwidth),
      codingRate_(codingRate),
      power_(power),
      sync_word_(sync_word),
      crc_(crc),
      preamble_length_(preamble_length),
      tcxo_voltage_(0.0F) {
    if (!IsValid()) {
        throw std::invalid_argument("Invalid radio configuration:" +
                                    Validate());
    }
}

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

void RadioConfig::setPower(int8_t power) {
    if (power > 20) {  // Typical LoRa limit
        throw std::invalid_argument("Power exceeds maximum allowed value");
    }
    power_ = power;
}

RadioConfig RadioConfig::CreateDefaultSx1276() {
    return RadioConfig{};
}

RadioConfig RadioConfig::CreateDefaultSx1278() {
    return RadioConfig{RadioType::kSx1278, 433.0, 7, 125.0, 5, 20};
}

RadioConfig RadioConfig::CreateDefaultSx1262() {
    return RadioConfig{RadioType::kSx1262};
}

std::string RadioConfig::getRadioTypeString() const {
    switch (radio_type_) {
        case RadioType::kSx1276:
            return "SX1276";
        case RadioType::kSx1278:
            return "SX1278";
        case RadioType::kSx1262:
            return "SX1262";
        case RadioType::kMockRadio:
            return "MockRadio";
        default:
            return "Unknown";
    }
}

Result RadioConfig::setSyncWord(uint8_t sync_word) {
    // TODO: Validation
    sync_word_ = sync_word;
    return Result::Success();
}

Result RadioConfig::setCRC(bool enabled) {
    crc_ = enabled;
    return Result::Success();
}

Result RadioConfig::setPreambleLength(uint16_t preamble_length) {
    // TODO: Validation
    preamble_length_ = preamble_length;
    return Result::Success();
}

void RadioConfig::setCurrentLimit(float current_limit_ma) {
    if (current_limit_ma == kAutoCurrentLimit) {
        current_limit_ma_ = kAutoCurrentLimit;
        return;
    }
    if (current_limit_ma < 45.0F || current_limit_ma > 240.0F) {
        throw std::invalid_argument(
            "Current limit must be 0 (auto) or between 45 and 240 mA");
    }
    current_limit_ma_ = current_limit_ma;
}

float RadioConfig::RecommendedCurrentLimit(RadioType type, int8_t power) {
    switch (type) {
        case RadioType::kSx1276:
        case RadioType::kSx1278:
            if (power <= 7)
                return 45.0F;
            if (power <= 14)
                return 60.0F;
            if (power <= 17)
                return 100.0F;
            return 150.0F;

        case RadioType::kSx1262:
            if (power <= 14)
                return 60.0F;
            if (power <= 17)
                return 100.0F;
            return 140.0F;

        default:
            return 60.0F;
    }
}

void RadioConfig::setTcxoVoltage(float voltage) {
    if (voltage < 0.0F || voltage > 3.3F) {
        throw std::invalid_argument("TCXO voltage must be between 0.0 and 3.3");
    }
    tcxo_voltage_ = voltage;
}

bool RadioConfig::IsValid() const {
    return frequency_ >= kMinFrequency && frequency_ <= kMaxFrequency &&
           spreadingFactor_ >= kMinSpreadingFactor &&
           spreadingFactor_ <= kMaxSpreadingFactor && bandwidth_ > 0 &&
           codingRate_ >= 5 && codingRate_ <= 8 && power_ <= 20;
}

std::string RadioConfig::Validate() const {
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
