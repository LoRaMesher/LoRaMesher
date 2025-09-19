/**
 * @file ping_pong_example.cpp
 * @brief Example application using LoraMesher with PingPong protocol
 */
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "loramesher.hpp"
#include "os/rtos.hpp"

using namespace loramesher;

std::unique_ptr<LoraMesher> mesher = nullptr;

#define LORA_RADIO_TYPE RadioType::kSx1276
#define LORA_ADDRESS 2  // Set 0 for auto-address

#define LORA_CS 18
#define LORA_RST 23
#define LORA_IRQ 26
#define LORA_IO1 33

#define LORA_FREQUENCY 869.900F
#define LORA_SPREADING_FACTOR 7U
#define LORA_BANDWITH 125.0
#define LORA_CODING_RATE 7U
#define LORA_POWER 6
#define LORA_SYNC_WORD 20U
#define LORA_CRC true
#define LORA_PREAMBLE_LENGTH 8U

// Event callback for ping completion
void pingCompletionCallback(AddressType address, uint32_t rtt, bool success) {
    if (success) {
        LOG_INFO("Ping response from node 0x%04X, RTT: %u ms", address, rtt);
    } else {
        LOG_ERROR("Ping to node 0x%04X timed out", address);
    }
}

// Simple data received callback (recommended approach)
void OnDataReceived(AddressType source, const std::vector<uint8_t>& data) {
    // Recommendation: Forward to separate task for processing
    std::cout << "Received data from: 0x" << std::hex << source << std::dec
              << " (" << data.size() << " bytes)" << std::endl;

    // Process data in your application...
    // For example: processDataInSeparateTask(source, data);
}

// Node application task
void appTask(void* parameters) {
    // Get RTOS instance for delays
    auto& rtos = os::RTOS::instance();

    // Our node address
    AddressType our_address = mesher->GetNodeAddress();
    LOG_INFO("Our node address: 0x%04X", our_address);

    // Get the PingPong protocol interface
    auto ping_pong = mesher->GetPingPongProtocol();
    if (!ping_pong) {
        LOG_ERROR("Failed to get PingPong protocol");
        return;
    }

    // Target nodes for pinging (adjust as needed)
    const AddressType targets[] = {0x0001, 0x0002};
    const unsigned target_count = sizeof(targets) / sizeof(targets[0]);

    // Send pings periodically
    unsigned ping_counter = 0;

    while (!rtos.ShouldStopOrPause()) {
        // Choose a target
        AddressType target = targets[ping_counter % target_count];

        // Skip if target is our own address
        if (target == our_address) {
            ping_counter++;
            rtos.delay(100);  // Small delay
            continue;
        }

        // Send a ping
        LOG_INFO("Sending ping to node 0x%04X", target);
        Result result = ping_pong->SendPing(target, our_address, 10000,
                                            pingCompletionCallback);

        if (!result) {
            LOG_ERROR("Failed to send ping: %s",
                      result.GetErrorMessage().c_str());
        }

        // Wait between pings
        rtos.delay(60000);
        ping_counter++;
    }
}

int main() {
    try {

        RadioConfig radioConfig(LORA_RADIO_TYPE, LORA_FREQUENCY,
                                LORA_SPREADING_FACTOR, LORA_BANDWITH,
                                LORA_CODING_RATE, LORA_POWER, LORA_SYNC_WORD,
                                LORA_CRC, LORA_PREAMBLE_LENGTH);

        PinConfig pinConfig(LORA_CS,   // NSS pin
                            LORA_RST,  // Reset pin
                            LORA_IRQ,  // DIO0 pin
                            LORA_IO1   // DIO1 pin
        );

        // Create LoraMesher with PingPong protocol
        mesher =
            LoraMesher::Builder()
                .withRadioConfig(radioConfig)
                .withPinConfig(pinConfig)
                .withPingPongProtocol(PingPongProtocolConfig(
                    LORA_ADDRESS, 2000, 3))  // Address, 2s timeout, 3 retries
                .Build();

        //Set up data callback
        mesher->SetDataCallback(OnDataReceived);

        // Start the network
        Result result = mesher->Start();
        if (!result) {
            LOG_ERROR("Failed to start LoraMesher: %s",
                      result.GetErrorMessage().c_str());
            return 1;
        }

        LOG_INFO("LoraMesher started successfully");

        // Create the application task
        os::TaskHandle_t app_task_handle = nullptr;
        os::RTOS::instance().CreateTask(appTask, "PingPongApp",
                                        4096,  // Stack size
                                        mesher.get(),
                                        2,  // Priority
                                        &app_task_handle);

    } catch (const std::exception& e) {
        LOG_ERROR("Exception: %s", e.what());
        return 1;
    }

    return 0;
}

#ifdef ARDUINO
void setup() {
    Serial.begin(115200);
    main();
}

void loop() {
    // Do nothing
    sleep(100);
}
#endif