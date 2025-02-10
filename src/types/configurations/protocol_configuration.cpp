#include "protocol_configuration.hpp"

#include <sstream>

namespace loramesher {

// ProtocolConfig Implementation
ProtocolConfig::ProtocolConfig(uint32_t helloInterval, uint32_t syncInterval,
                               uint32_t maxTimeouts)
    : helloInterval_(helloInterval),
      syncInterval_(syncInterval),
      maxTimeouts_(maxTimeouts) {}

void ProtocolConfig::setHelloInterval(uint32_t interval) {
    if (interval < kMinHelloInterval || interval > kMaxHelloInterval) {
        throw std::invalid_argument("Hello interval out of valid range");
    }
    helloInterval_ = interval;
}

void ProtocolConfig::setSyncInterval(uint32_t interval) {
    if (interval <= helloInterval_) {
        throw std::invalid_argument(
            "Sync interval must be greater than hello interval");
    }
    syncInterval_ = interval;
}

void ProtocolConfig::setMaxTimeouts(uint32_t timeouts) {
    if (timeouts == 0) {
        throw std::invalid_argument("Max timeouts must be greater than 0");
    }
    maxTimeouts_ = timeouts;
}

ProtocolConfig ProtocolConfig::createDefault() {
    return ProtocolConfig{};
}

bool ProtocolConfig::isValid() const {
    return helloInterval_ >= kMinHelloInterval &&
           helloInterval_ <= kMaxHelloInterval &&
           syncInterval_ > helloInterval_ && maxTimeouts_ > 0;
}

std::string ProtocolConfig::validate() const {
    std::stringstream errors;
    if (helloInterval_ < kMinHelloInterval ||
        helloInterval_ > kMaxHelloInterval) {
        errors << "Hello interval out of range. ";
    }
    if (syncInterval_ <= helloInterval_) {
        errors << "Sync interval must be greater than hello interval. ";
    }
    if (maxTimeouts_ == 0) {
        errors << "Max timeouts must be greater than 0. ";
    }
    return errors.str();
}

}  // namespace loramesher