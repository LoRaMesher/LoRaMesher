// include/loramesher/core/loramesher.hpp
#pragma once

#include "build_options.hpp"

// Standard includes
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

static const char* LM_TAG = "LoraMesher";

#include "hal/arduino/arduino_hal.hpp"
#include "hal/native/native_hal.hpp"
// #include "loramesher/hal/mock_radio.hpp"

// Project includes
// #include "loramesher/core/radio_manager.hpp"
// #include "loramesher/core/mesh_protocol.hpp"
// #include "loramesher/core/sleep_manager.hpp"
// #include "loramesher/core/task_manager.hpp"
// #include "loramesher/types/message.hpp"
// #include "loramesher/types/config.hpp"

// Main class
class LoraMesher {
   private:
    /**
     * @brief Configuration for the LoraMesher instance
     */
    struct PinConfig {
        int8_t nss;
        int8_t reset;
        int8_t dio0;
        int8_t dio1;

        static PinConfig getDefaultPins() {
            // TTGO T-Beam default pins
            return PinConfig{
                .nss = 18,
                .reset = 23,
                .dio0 = 26,
                .dio1 = 33,
            };
        }
    };

    // Core components
    // std::unique_ptr<Module> module;
    // std::unique_ptr<RadioManager> radioManager;
    // std::unique_ptr<MeshProtocol> meshProtocol;
    // std::unique_ptr<SleepManager> sleepManager;
    // std::unique_ptr<TaskManager> taskManager;

    /**
     * @brief Configuration for the LoraMesher instance
     */
    struct Config {
        PinConfig pins;

        // Radio configuration
        float frequency;
        uint8_t spreadingFactor;
        float bandwidth;
        uint8_t codingRate;
        uint8_t power;

        // Protocol configuration
        uint32_t helloInterval;
        uint32_t syncInterval;
        uint32_t maxTimeouts;

        // Power management
        uint32_t sleepDuration;
        bool enableDeepSleep;

        static Config getDefaultConfig() {
            return Config{.pins = PinConfig::getDefaultPins(),
                          .frequency = 869.900F,
                          .spreadingFactor = 7,
                          .bandwidth = 125.0,
                          .codingRate = 5,
                          .power = 17,
                          .helloInterval = 120000,
                          .syncInterval = 300000,
                          .maxTimeouts = 10,
                          .sleepDuration = 60000,
                          .enableDeepSleep = true};
        }
    };

   public:
    // Builder pattern for initialization
    class Builder {
       private:
        Config config;

       public:
        Builder() : config(Config::getDefaultConfig()) {}

        Builder& setFrequency(uint32_t freq) {
            config.frequency = freq;
            return *this;
        }

        Builder& setSpreadingFactor(uint8_t sf) {
            config.spreadingFactor = sf;
            return *this;
        }

        Builder& setBandwidth(float bw) {
            config.bandwidth = bw;
            return *this;
        }

        Builder& setCodingRate(uint8_t cr) {
            config.codingRate = cr;
            return *this;
        }

        Builder& setPower(uint8_t pwr) {
            config.power = pwr;
            return *this;
        }

        Builder& setSleepDuration(uint32_t duration) {
            config.sleepDuration = duration;
            return *this;
        }

        Builder& setSleepEnabled(bool enabled) {
            config.enableDeepSleep = enabled;
            return *this;
        }

        std::unique_ptr<LoraMesher> build() {
            return std::make_unique<LoraMesher>(config);
        }
    };

    explicit LoraMesher(const Config& config);
    ~LoraMesher();

    bool start() { return true; }
    void stop();

    // bool sendMessage(const Message& msg);
    // bool sendReliableMessage(const Message& msg);

   private:
    std::unique_ptr<loramesher::hal::LoraMesherHal> m_hal = nullptr;
    /**
     * @brief Initialize the LoraMesher instance
     */
    void init();
};