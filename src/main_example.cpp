// main.cpp
#include "loramesher.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

extern "C" void app_main() {
    // Initialize with builder pattern
    std::unique_ptr<LoraMesher> mesher = LoraMesher::Builder()
                                             .setFrequency(868E6)
                                             .setSpreadingFactor(7)
                                             .setSleepEnabled(true)
                                             .build();

    if (!mesher->start()) {
        // ESP_LOGE(TAG, "Failed to start LoRaMesher");
        return;
    }

    // // Application code
    // Message msg{
    //     .destination = 0x1234,
    //     .payload = "Hello World",
    //     .length = 11
    // };

    // mesher->sendMessage(msg);
}

#elif defined(LORAMESHER_BUILD_NATIVE)

int main() {
    // Initialize with builder pattern
    std::unique_ptr<LoraMesher> mesher = LoraMesher::Builder()
                                             .setFrequency(868E6)
                                             .setSpreadingFactor(7)
                                             .setSleepEnabled(true)
                                             .build();

    if (!mesher->start()) {
        // ESP_LOGE(TAG, "Failed to start LoRaMesher");
        return 1;
    }

    // // Application code
    // Message msg{
    //     .destination = 0x1234,
    //     .payload = "Hello World",
    //     .length = 11
    // };

    // mesher->sendMessage(msg);

    return 0;
}

#endif