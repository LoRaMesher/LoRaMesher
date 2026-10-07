/**
 * @file main.cpp
 * @brief Reliable (acknowledged) unicast with LoRaMesher
 *
 * This example shows how a node:
 * - Sends acknowledged messages to its peers with SendReliable()
 * - Learns whether each message arrived with SetDeliveryCallback()
 * - Receives messages together with their metadata (hops travelled, sender
 *   sequence number) with SetDataCallbackEx()
 *
 * Flash it on two or more boards. Every node sends a reliable message to the
 * next peer in its routing table every 15 seconds.
 */

#include <iostream>
#include <string>

#include "loramesher.hpp"

using namespace loramesher;

// =============================================================================
// Hardware Pin Configuration
// =============================================================================
// Configure these pins for your board (see examples/simple_example for more
// boards):
//   TTGO T-Beam v1.x:    CS=18, RST=23, IRQ=26, IO1=33  (SX1276)
//   TTGO LoRa32 v1:      CS=18, RST=14, IRQ=26, IO1=33  (SX1278)

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
#define LORA_BANDWITH 125.0       // kHz - 125/250/500
#define LORA_CODING_RATE 7U       // 5-8: higher = more error correction
#define LORA_POWER 6              // dBm - transmit power
#define LORA_SYNC_WORD 20U        // Network identifier (0-255)
#define LORA_CRC true             // Enable CRC checking
#define LORA_PREAMBLE_LENGTH 8U   // Preamble symbols

// =============================================================================
// Global Variables
// =============================================================================

std::unique_ptr<LoraMesher> mesher = nullptr;
uint8_t peer_index = 0;       // Cycles through routing table destinations
uint32_t message_number = 0;  // Included in the payload text

// =============================================================================
// Callbacks
// =============================================================================
// Both callbacks run on the protocol task. Keep them short; for heavier work,
// hand the data to your own task (see examples/queued_receive_example).

/**
 * @brief Called with the outcome of every SendReliable()
 *
 * @param result Message id, outcome, acknowledging node and round-trip time
 */
void OnDeliveryResult(const LoraMesher::DeliveryResult& result) {
    using protocols::reliability::Outcome;

    switch (result.outcome) {
        case Outcome::Delivered:
            std::cout << "Message seq=" << static_cast<int>(result.id.seq)
                      << " to 0x" << std::hex << result.id.dest
                      << " delivered (acknowledged by 0x" << result.by
                      << std::dec << ", RTT " << result.rtt_ms << " ms)"
                      << std::endl;
            break;
        case Outcome::Failed:
            std::cout << "Message seq=" << static_cast<int>(result.id.seq)
                      << " to 0x" << std::hex << result.id.dest << std::dec
                      << " failed: no acknowledgement after all retries"
                      << std::endl;
            break;
        case Outcome::GroupWindowClosed:
            // Reported only for group sends.
            break;
    }
}

/**
 * @brief Called when a message addressed to this node arrives
 *
 * @param message Source, destination, sequence, hops and payload. The payload
 *                is only valid during this call.
 */
void OnDataReceived(const ReceivedData& message) {
    std::string text(message.payload.begin(), message.payload.end());
    std::cout << "Received from 0x" << std::hex << message.source << std::dec
              << " (seq=" << static_cast<int>(message.seq)
              << ", hops=" << static_cast<int>(message.hops) << "): " << text
              << std::endl;
}

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Sends a reliable message to the next peer in the routing table
 * @return true if the message was accepted for sending
 */
bool sendReliableMessage() {
    auto routes = mesher->GetRoutingTable();
    if (routes.empty()) {
        std::cout << "No peers known yet" << std::endl;
        return false;
    }

    AddressType dest = routes[peer_index % routes.size()].destination;
    peer_index++;
    if (dest == mesher->GetNodeAddress()) {
        return false;
    }

    Result ready = mesher->IsReadyToSend(dest);
    if (!ready) {
        std::cout << "Not ready to send to 0x" << std::hex << dest << std::dec
                  << ": " << ready.GetErrorMessage() << std::endl;
        return false;
    }

    std::string text = "Reliable hello #" + std::to_string(++message_number);
    std::vector<uint8_t> payload(text.begin(), text.end());

    ReliableOptions options;
    options.max_retries = 3;  // Retransmissions after the first attempt
    options.timeout_ms = 0;   // 0 = derive from hop count and superframe

    LoraMesher::MessageId id = mesher->SendReliable(dest, payload, options);
    if (id.source == 0) {
        std::cerr << "SendReliable to 0x" << std::hex << dest << std::dec
                  << " was rejected" << std::endl;
        return false;
    }

    // The outcome arrives later through OnDeliveryResult with this id.
    std::cout << "Sent \"" << text << "\" to 0x" << std::hex << dest << std::dec
              << " (seq=" << static_cast<int>(id.seq) << ")" << std::endl;
    return true;
}

// =============================================================================
// Initialization
// =============================================================================

void ConfigureAndUseLoraMesher() {
    PinConfig pinConfig(LORA_CS, LORA_RST, LORA_IRQ, LORA_IO1);

    RadioConfig radioConfig(LORA_RADIO_TYPE, LORA_FREQUENCY,
                            LORA_SPREADING_FACTOR, LORA_BANDWITH,
                            LORA_CODING_RATE, LORA_POWER, LORA_SYNC_WORD,
                            LORA_CRC, LORA_PREAMBLE_LENGTH);

    LoRaMeshProtocolConfig mesh_config;

    mesher = LoraMesher::Builder()
                 .withRadioConfig(radioConfig)
                 .withPinConfig(pinConfig)
                 .withLoRaMeshProtocol(mesh_config)
                 .Build();

    mesher->SetDataCallbackEx(OnDataReceived);
    mesher->SetDeliveryCallback(OnDeliveryResult);

    Result start_result = mesher->Start();
    if (!start_result) {
        std::cerr << "Failed to start LoraMesher: "
                  << start_result.GetErrorMessage() << std::endl;
    }
}

// =============================================================================
// Arduino Entry Points
// =============================================================================

#ifdef ARDUINO
void setup() {
    Serial.begin(115200);
    ConfigureAndUseLoraMesher();
}

void loop() {
    sendReliableMessage();
    delay(15000);
}
#endif
