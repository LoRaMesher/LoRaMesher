#include "pin_configuration.hpp"

#include <sstream>

namespace loramesher {

// PinConfig Implementation
PinConfig::PinConfig(int8_t nss, int8_t reset, int8_t dio0, int8_t dio1)
    : nss_(nss), reset_(reset), dio0_(dio0), dio1_(dio1) {}

void PinConfig::setNss(int8_t nss) {
    if (nss < 0) {
        throw std::invalid_argument("NSS pin must be non-negative");
    }
    nss_ = nss;
}

void PinConfig::setReset(int8_t reset) {
    if (reset < 0) {
        throw std::invalid_argument("Reset pin must be non-negative");
    }
    reset_ = reset;
}

void PinConfig::setDio0(int8_t dio0) {
    if (dio0 < 0) {
        throw std::invalid_argument("DIO0 pin must be non-negative");
    }
    dio0_ = dio0;
}

void PinConfig::setDio1(int8_t dio1) {
    if (dio1 < 0) {
        throw std::invalid_argument("DIO1 pin must be non-negative");
    }
    dio1_ = dio1;
}

PinConfig PinConfig::createDefault() {
    return PinConfig{};  // Uses default constructor values
}

bool PinConfig::isValid() const {
    return nss_ >= 0 && reset_ >= 0 && dio0_ >= 0 && dio1_ >= 0;
}

std::string PinConfig::validate() const {
    std::stringstream errors;
    if (nss_ < 0)
        errors << "Invalid NSS pin. ";
    if (reset_ < 0)
        errors << "Invalid Reset pin. ";
    if (dio0_ < 0)
        errors << "Invalid DIO0 pin. ";
    if (dio1_ < 0)
        errors << "Invalid DIO1 pin. ";
    return errors.str();
}

}  // namespace loramesher