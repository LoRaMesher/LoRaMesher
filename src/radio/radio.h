// src/radio/radio.h
#pragma once

#include <functional>
#include <vector>

namespace loramesher {
namespace radio {

// Interface for radio implementations
class IRadio {
   public:
    virtual ~IRadio() = default;

    // Core radio operations
    virtual Result configure(const RadioConfig& config) = 0;
    virtual Result send(const uint8_t* data, size_t len) = 0;
    virtual Result startReceive() = 0;
    virtual Result sleep() = 0;

    // Radio parameters
    virtual int8_t getRSSI() = 0;
    virtual int8_t getSNR() = 0;

    // Event registration
    virtual void setReceiveCallback(
        std::function<void(RadioEvent&)> callback) = 0;
};

}  // namespace radio
}  // namespace loramesher