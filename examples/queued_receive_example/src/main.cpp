/**
 * @file main.cpp
 * @brief Queued receive example for LoRaMesher
 *
 * Demonstrates running LoRaMesher as a pure background task with safe
 * message passing to an application receive task via queue.
 *
 * Pattern:
 *   SetDataCallback  →  enqueue message pointer (non-blocking)
 *   ReceiveTask      →  dequeue and process (blocking wait)
 *
 * This is the recommended pattern when your application owns the main
 * loop and wants to handle LoRa messages on its own task/stack.
 * Uses os::RTOS::instance() for cross-platform compatibility.
 */

#include <iostream>
#include <vector>

#include "loramesher.hpp"
#include "os/rtos.hpp"

using namespace loramesher;

// =============================================================================
// Hardware Pin Configuration
// =============================================================================
// Configure these pins for your board. Common configurations:
//   TTGO T-Beam v1.x:    CS=18, RST=23, IRQ=26, IO1=33  (SX1276)
//   TTGO LoRa32 v1:      CS=18, RST=14, IRQ=26, IO1=33  (SX1278)
//   LILYGO T3 S3 v1.x:   CS=7,  RST=8,  IRQ=9,  IO1=33  (SX1278)
//   Heltec WiFi LoRa:    CS=18, RST=14, IRQ=26, IO1=35  (SX1276)
//   Heltec WiFi LoRa V3: CS=8,  RST=12, IRQ=14, IO1=13  (SX1262)
//     SPI pins: SCK=9, MISO=11, MOSI=10
//     Requires: radioConfig.setTcxoVoltage(1.8F)

#define LORA_CS 18   // SPI Chip Select (NSS)
#define LORA_RST 23  // Radio Reset pin
#define LORA_IRQ 26  // DIO0 - Primary interrupt
#define LORA_IO1 33  // DIO1 - Secondary interrupt

// =============================================================================
// Radio Configuration
// =============================================================================

#define LORA_RADIO_TYPE RadioType::kSx1276
#define LORA_FREQUENCY 869.900F   // MHz - EU868 band
#define LORA_SPREADING_FACTOR 7U  // SF7-SF12: higher = more range, slower
#define LORA_BANDWIDTH 125.0      // kHz
#define LORA_CODING_RATE 7U       // 5-8: higher = more error correction
#define LORA_POWER 6              // dBm - transmit power
#define LORA_SYNC_WORD 20U        // Network identifier (0-255)
#define LORA_CRC true             // Enable CRC checking
#define LORA_PREAMBLE_LENGTH 8U   // Preamble symbols

// =============================================================================
// Application message struct stored in the queue (pointer-based)
// =============================================================================

struct AppMessage {
    AddressType source;
    std::vector<uint8_t> data;
};

// =============================================================================
// Global state
// =============================================================================

std::unique_ptr<LoraMesher> mesher = nullptr;
os::QueueHandle_t receive_queue = nullptr;
os::TaskHandle_t receive_task_handle = nullptr;

// =============================================================================
// Receive Task — runs on its own stack, processes messages from queue
// =============================================================================

/**
 * @brief Application task that processes incoming LoRa messages
 *
 * Blocks on the queue until a message arrives, then processes it on
 * this task's own stack — decoupled from the mesher's internal tasks.
 */
void ReceiveTask(void* /*pvParameters*/) {
    AppMessage* msg = nullptr;
    for (;;) {
        auto result = os::RTOS::instance().ReceiveFromQueue(receive_queue, &msg,
                                                            MAX_DELAY);

        if (result == os::QueueResult::kOk && msg != nullptr) {
            std::cout << "App received " << msg->data.size() << " bytes from 0x"
                      << std::hex << msg->source << std::dec << std::endl;

            // --- Your application logic here ---
            // e.g. parse message, update state, trigger display refresh

            delete msg;
        }
    }
}

// =============================================================================
// Initialization
// =============================================================================

void ConfigureAndStart() {
    // Step 1: Create the receive queue (stores AppMessage pointers)
    // Must be created before SetDataCallback so the callback can use it.
    receive_queue = os::RTOS::instance().CreateQueue(10, sizeof(AppMessage*));

    // Step 2: Create the application receive task
    os::RTOS::instance().CreateTask(
        ReceiveTask, "App_LoRa_Recv",
        4096,  // Stack size in bytes
        nullptr,
        2,  // Priority — adjust relative to your other tasks
        &receive_task_handle);

    // Step 3: Configure hardware and radio
    PinConfig pinConfig(LORA_CS, LORA_RST, LORA_IRQ, LORA_IO1);
    RadioConfig radioConfig(LORA_RADIO_TYPE, LORA_FREQUENCY,
                            LORA_SPREADING_FACTOR, LORA_BANDWIDTH,
                            LORA_CODING_RATE, LORA_POWER, LORA_SYNC_WORD,
                            LORA_CRC, LORA_PREAMBLE_LENGTH);

    // --- Heltec WiFi LoRa V3 configuration ---
    // PinConfig pinConfig(8, 12, 14, 13, 9, 11, 10);  // CS, RST, IRQ, IO1, SCK, MISO, MOSI
    // RadioConfig radioConfig(RadioType::kSx1262, 868.0F, 7U, 125.0, 7U, 14, 20U, true, 8U);
    // radioConfig.setTcxoVoltage(1.8F);
    LoRaMeshProtocolConfig mesh_config;

    mesher = LoraMesher::Builder()
                 .withRadioConfig(radioConfig)
                 .withPinConfig(pinConfig)
                 .withLoRaMeshProtocol(mesh_config)
                 .Build();

    // Step 4: Register data callback — only enqueues, never processes
    //
    // The callback runs on LoRaMesher's internal task. Keep it minimal:
    // allocate, copy, enqueue. All real work happens in ReceiveTask.
    mesher->SetDataCallback(
        [](AddressType source, const std::vector<uint8_t>& data) {
            auto* msg = new AppMessage{source, data};
            auto result = os::RTOS::instance().SendToQueue(
                receive_queue, &msg, 0 /* non-blocking */);

            if (result != os::QueueResult::kOk) {
                // Queue full — drop and free rather than block the mesher task
                delete msg;
                std::cerr << "Receive queue full, dropping packet" << std::endl;
            }
        });

    // Step 5: Start LoRaMesher — creates its own background tasks internally
    Result init_result = mesher->Start();
    if (!init_result) {
        std::cerr << "Failed to start LoraMesher: "
                  << init_result.GetErrorMessage() << std::endl;
        return;
    }

    std::cout << "LoRaMesher started. Node address: 0x" << std::hex
              << mesher->GetNodeAddress() << std::dec << std::endl;
}

// =============================================================================
// Arduino Entry Points
// =============================================================================

#ifdef ARDUINO
void setup() {
    Serial.begin(115200);
    ConfigureAndStart();
}

void loop() {
    // Application main loop — freely runs here while LoRaMesher
    // handles the radio in background and ReceiveTask handles messages.

    auto routes = mesher->GetRoutingTable();
    if (!routes.empty()) {
        AddressType dest = routes[0].destination;
        if (dest != mesher->GetNodeAddress()) {
            std::string hello = "Hello!";
            mesher->Send(dest,
                         std::vector<uint8_t>(hello.begin(), hello.end()));
        }
    }

    // Use GetTimeUntilNextDataSlot to avoid sending before the TX window
    uint32_t wait_ms = mesher->GetTimeUntilNextDataSlot();
    delay(wait_ms > 0 ? wait_ms : 10000);
}
#endif
