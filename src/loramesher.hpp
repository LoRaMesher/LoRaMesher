// include/loramesher/core/loramesher.hpp
#pragma once

#include "build_options.hpp"

// Standard includes
// #include <functional>
// #include <memory>
// #include <stdexcept>
// #include <string>
// #include <vector>

static const char* LM_TAG = "LoraMesher";

#include <memory>
#include "hal/arduino/arduino_hal.hpp"
#include "hal/native/native_hal.hpp"
#include "types/configurations/loramesher_configuration.hpp"
#include "types/messages/message.hpp"
// #include "power/sleep_manager.h"
// #include "protocol/mesh_protocol.h"
// #include "radio/radio_manager.h"
// #include "task/task_manager.h"

namespace loramesher {

class LoraMesher {
   public:
    class Builder;  // Forward declaration of Builder

   private:
    friend class Builder;  // Allow Builder to access private constructor
    explicit LoraMesher(const Config& config);  // Private constructor

   public:
    // Non-copyable
    LoraMesher(const LoraMesher&) = delete;
    LoraMesher& operator=(const LoraMesher&) = delete;

    // Moveable
    LoraMesher(LoraMesher&&) noexcept = default;
    LoraMesher& operator=(LoraMesher&&) noexcept = default;

    ~LoraMesher();

    // Core functionality
    [[nodiscard]] bool start();
    void stop();

    // Message sending
    // [[nodiscard]] bool sendMessage(const Message& msg);
    // [[nodiscard]] bool sendReliableMessage(const Message& msg);

    // Getters for internal components (if needed)
    // const RadioManager& getRadioManager() const { return *radioManager_; }
    // const MeshProtocol& getMeshProtocol() const { return *meshProtocol_; }

   private:
    // Initialization
    [[nodiscard]] bool initialize();

    // Core components
    // std::unique_ptr<RadioManager> radioManager_;
    // std::unique_ptr<MeshProtocol> meshProtocol_;
    // std::unique_ptr<SleepManager> sleepManager_;
    // std::unique_ptr<TaskManager> taskManager_;
    std::unique_ptr<loramesher::hal::ILoraMesherHal> hal_;

    // Configuration
    Config config_;

    // State
    bool isInitialized_ = false;
    bool isRunning_ = false;
};

// Builder class implementation
class LoraMesher::Builder {
   public:
    Builder() : config_(Config::createDefault()) {}

    // Radio configuration
    Builder& withRadioConfig(const RadioConfig& config) {
        config_.setRadioConfig(config);
        return *this;
    }

    // Protocol configuration
    Builder& withProtocolConfig(const ProtocolConfig& config) {
        config_.setProtocolConfig(config);
        return *this;
    }

    // Pin configuration
    Builder& withPinConfig(const PinConfig& config) {
        config_.setPinConfig(config);
        return *this;
    }

    // Individual setters for common configurations
    Builder& withFrequency(float freq) {
        auto radio = config_.getRadioConfig();
        radio.setFrequency(freq);
        config_.setRadioConfig(radio);
        return *this;
    }

    Builder& withSpreadingFactor(uint8_t sf) {
        auto radio = config_.getRadioConfig();
        radio.setSpreadingFactor(sf);
        config_.setRadioConfig(radio);
        return *this;
    }

    Builder& withSleepDuration(uint32_t duration) {
        config_.setSleepDuration(duration);
        return *this;
    }

    Builder& withDeepSleep(bool enabled) {
        config_.setDeepSleepEnabled(enabled);
        return *this;
    }

    // Build method with validation
    [[nodiscard]] std::unique_ptr<LoraMesher> build() {
        if (!config_.isValid()) {
            throw std::invalid_argument("Invalid configuration: " +
                                        config_.validate());
        }
        return std::unique_ptr<LoraMesher>(new LoraMesher(config_));
    }

   private:
    Config config_;
};

}  // namespace loramesher