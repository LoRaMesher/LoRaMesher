// main.cpp
#include <iostream>
#include "loramesher.hpp"

using namespace loramesher;

/**
 * @brief Main function for the example, how to configure LoRaMesher with default
 */
int simple_example_main() {
    try {
        // Create a LoRaMesher instance with default configurations
        auto loraMesher = LoraMesher::Builder().build();

        // Start the LoRaMesher instance
        if (!loraMesher->start()) {
            std::cerr << "Failed to start LoRaMesher" << std::endl;
            return 1;
        }

        // Your application code here
        // For example, sending a message:
        /*
        Message msg{
            .destination = 0x1234,
            .payload = "Hello, LoRa!",
            .size = 12
        };

        if (!loraMesher->sendMessage(msg)) {
            std::cerr << "Failed to send message" << std::endl;
        }
        */

        // Keep the application running
        std::cout << "LoRaMesher is running. Press Enter to stop..."
                  << std::endl;
        std::cin.get();

        // Clean shutdown
        loraMesher->stop();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

/**
 * @brief Main function for the example, how to configure LoRaMesher with custom
 */
int full_example_main() {
    try {
        // Method 1: Using the Builder with individual setters
        auto customLoraMesher = LoraMesher::Builder()
                                    .withFrequency(868.1F)
                                    .withSpreadingFactor(7)
                                    .withDeepSleep(true)
                                    .withSleepDuration(60000)  // 60 seconds
                                    .build();

        // Method 2: Using the Builder with full custom configuration
        RadioConfig radioConfig(868.1F,  // frequency
                                7,       // spreading factor
                                125.0F,  // bandwidth
                                5,       // coding rate
                                17       // power
        );

        ProtocolConfig protocolConfig(120000,  // hello interval (2 min)
                                      300000,  // sync interval (5 min)
                                      10       // max timeouts
        );

        PinConfig pinConfig(18,  // NSS pin
                            23,  // Reset pin
                            26,  // DIO0 pin
                            33   // DIO1 pin
        );

        auto fullCustomLoraMesher = LoraMesher::Builder()
                                        .withRadioConfig(radioConfig)
                                        .withProtocolConfig(protocolConfig)
                                        .withPinConfig(pinConfig)
                                        .withSleepDuration(60000)
                                        .withDeepSleep(true)
                                        .build();

        // Start the LoRaMesher instance
        if (!fullCustomLoraMesher->start()) {
            std::cerr << "Failed to start LoRaMesher" << std::endl;
            return 1;
        }

        // Your application code here
        // For example, sending a message:
        /*
        Message msg{
            .destination = 0x1234,
            .payload = "Hello, LoRa!",
            .size = 12
        };

        if (!fullCustomLoraMesher->sendMessage(msg)) {
            std::cerr << "Failed to send message" << std::endl;
        }
        */

        // Keep the application running
        std::cout << "LoRaMesher is running. Press Enter to stop..."
                  << std::endl;
        std::cin.get();

        // Clean shutdown
        fullCustomLoraMesher->stop();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

#ifdef LORAMESHER_BUILD_ARDUINO

void setup() {
    Serial.begin(115200);
    // Initialize with builder pattern
    full_example_main();
}

void loop() {
    // Do nothing
}

#elif defined(LORAMESHER_BUILD_NATIVE)

int main() {
    return full_example_main();
}

#endif