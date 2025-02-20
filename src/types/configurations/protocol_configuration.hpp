// src/types/configurations/protocol_configuration.hpp
#pragma once

#include <cstdint>
#include <string>

namespace loramesher {

/**
 * @brief Configuration class for mesh protocol parameters
 *
 * Manages timing and retry parameters for the LoRaMesher protocol,
 * including hello messages, synchronization intervals, and timeout thresholds.
 */
class ProtocolConfig {
   public:
    /**
     * @brief Constructs a ProtocolConfig object with specified parameters
     *
     * @param helloInterval Interval between hello messages in milliseconds
     * @param syncInterval Interval between synchronization messages in milliseconds
     * @param maxTimeouts Maximum number of timeouts before considering a node unreachable
     */
    explicit ProtocolConfig(uint32_t helloInterval = 120000,
                            uint32_t syncInterval = 300000,
                            uint32_t maxTimeouts = 10);

    /**
     * @brief Gets the hello message interval
     * @return Current hello interval in milliseconds
     */
    uint32_t getHelloInterval() const { return helloInterval_; }

    /**
     * @brief Gets the synchronization message interval
     * @return Current sync interval in milliseconds
     */
    uint32_t getSyncInterval() const { return syncInterval_; }

    /**
     * @brief Gets the maximum number of allowed timeouts
     * @return Current maximum timeout threshold
     */
    uint32_t getMaxTimeouts() const { return maxTimeouts_; }

    /**
     * @brief Sets the hello message interval with validation
     * @param interval New hello interval in milliseconds
     */
    void setHelloInterval(uint32_t interval);

    /**
     * @brief Sets the synchronization message interval with validation
     * @param interval New sync interval in milliseconds
     */
    void setSyncInterval(uint32_t interval);

    /**
     * @brief Sets the maximum number of allowed timeouts with validation
     * @param timeouts New maximum timeout threshold
     */
    void setMaxTimeouts(uint32_t timeouts);

    /**
     * @brief Creates a protocol configuration with default values
     * @return Default protocol configuration object
     */
    static ProtocolConfig CreateDefault();

    /**
     * @brief Validates the protocol configuration
     * @return True if configuration is valid, false otherwise
     */
    bool IsValid() const;

    /**
     * @brief Validates the protocol configuration and provides error details
     * @return Empty string if valid, otherwise error description
     */
    std::string Validate() const;

   private:
    static constexpr uint32_t kMinHelloInterval =
        1000;  ///< Minimum hello interval (1 second)
    static constexpr uint32_t kMaxHelloInterval =
        3600000;  ///< Maximum hello interval (1 hour)

    uint32_t
        helloInterval_;  ///< Interval between hello messages in milliseconds
    uint32_t
        syncInterval_;  ///< Interval between synchronization messages in milliseconds
    uint32_t
        maxTimeouts_;  ///< Maximum number of timeouts before considering a node unreachable
};

}  // namespace loramesher