// src/types/configurations/protocol_configuration.hpp
#pragma once

#include <cstdint>
#include <string>

namespace loramesher {

class ProtocolConfig {
   public:
    // Constructor with default values
    explicit ProtocolConfig(uint32_t helloInterval = 120000,
                            uint32_t syncInterval = 300000,
                            uint32_t maxTimeouts = 10);

    // Getters
    uint32_t getHelloInterval() const { return helloInterval_; }
    uint32_t getSyncInterval() const { return syncInterval_; }
    uint32_t getMaxTimeouts() const { return maxTimeouts_; }

    // Setters with validation
    void setHelloInterval(uint32_t interval);
    void setSyncInterval(uint32_t interval);
    void setMaxTimeouts(uint32_t timeouts);

    // Factory method
    static ProtocolConfig CreateDefault();

    // Validation
    bool IsValid() const;
    std::string Validate() const;

   private:
    static constexpr uint32_t kMinHelloInterval = 1000;     // 1 second
    static constexpr uint32_t kMaxHelloInterval = 3600000;  // 1 hour

    uint32_t helloInterval_;
    uint32_t syncInterval_;
    uint32_t maxTimeouts_;
};

}  // namespace loramesher