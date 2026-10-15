/**
 * @file main.cpp
 * @brief Group (multicast) messaging with LoRaMesher
 *
 * This example shows how a node:
 * - Joins a group with JoinGroup()
 * - Sends one message to every member with SendGroup(), asking the members to
 *   acknowledge it
 * - Follows the acknowledgements with SetDeliveryCallback()
 * - Tells group messages from unicast messages in SetDataCallbackEx()
 *
 * Flash it on two or more boards. Every node joins kGroup and sends group
 * messages one at a time: the next one goes out only after the previous
 * acknowledgement window has closed. The other members receive and
 * acknowledge each message.
 */

#include <atomic>
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

// The node with this address is the Network Manager; all others only join
#define NODE_MANAGER_ADDRESS 0x3ADF

// =============================================================================
// Group Configuration
// =============================================================================
// Group addresses range from kGroupAddressMin (0x8000) to kGroupAddressMax
// (0xFFFE). A node can belong to up to 8 groups. Sending to a group does not
// require being a member of it.

constexpr AddressType kGroup = 0x8001;

// =============================================================================
// Send Pacing
// =============================================================================
// A node's TX data slots (GetDataSlotsPerSuperframe() per superframe) carry
// its own messages, the messages it forwards for other nodes and the ACKs it
// returns. A group message is flooded, so it costs about one transmission per
// node, plus one ACK per member travelling back over its hop distance. Rather
// than a fixed delay, the loop sends the next message only when the previous
// acknowledgement window has closed and the TX queue is empty, so the rate
// follows the network size, the topology and the load.

constexpr uint32_t kMinSendIntervalMs = 20000;  // Lower bound between sends
constexpr uint32_t kPollIntervalMs = 1000;      // How often loop() re-checks

// =============================================================================
// Global Variables
// =============================================================================

std::unique_ptr<LoraMesher> mesher = nullptr;
uint32_t message_number = 0;  // Included in the payload text
uint32_t last_send_ms = 0;    // millis() of the last send attempt

/// True from SendGroup() until its acknowledgement window closes (set by the
/// loop, cleared on the protocol task).
std::atomic<bool> awaiting_window{false};

// =============================================================================
// Callbacks
// =============================================================================
// Both callbacks run on the protocol task. Keep them short; for heavier work,
// hand the data to your own task (see examples/queued_receive_example).

/**
 * @brief Called with the progress of every acknowledged SendGroup()
 *
 * A group send reports Delivered once per member that acknowledges, then
 * GroupWindowClosed when its acknowledgement window ends. GroupWindowClosed and
 * Failed end the send and let the loop send the next message.
 *
 * @param result Message id, outcome, acknowledging member and member count
 */
void OnDeliveryResult(const LoraMesher::DeliveryResult& result) {
    using protocols::reliability::Outcome;

    switch (result.outcome) {
        case Outcome::Delivered:
            std::cout << "Group 0x" << std::hex << result.id.dest << std::dec
                      << " seq=" << static_cast<int>(result.id.seq)
                      << ": member 0x" << std::hex << result.by << std::dec
                      << " acknowledged (RTT " << result.rtt_ms << " ms)"
                      << std::endl;
            return;
        case Outcome::GroupWindowClosed:
            std::cout << "Group 0x" << std::hex << result.id.dest << std::dec
                      << " seq=" << static_cast<int>(result.id.seq) << ": "
                      << static_cast<int>(result.ack_count)
                      << " member(s) acknowledged" << std::endl;
            break;
        case Outcome::Failed:
            std::cout << "Group 0x" << std::hex << result.id.dest << std::dec
                      << " seq=" << static_cast<int>(result.id.seq)
                      << ": send failed" << std::endl;
            break;
    }
    awaiting_window = false;
}

/**
 * @brief Called when a message for this node or one of its groups arrives
 *
 * @param message Source, destination, sequence, hops and payload. The payload
 *                is only valid during this call.
 */
void OnDataReceived(const ReceivedData& message) {
    std::string text(message.payload.begin(), message.payload.end());

    if (IsGroupAddress(message.dest)) {
        std::cout << "Group 0x" << std::hex << message.dest
                  << " message from 0x" << message.source << std::dec
                  << " (hops=" << static_cast<int>(message.hops)
                  << "): " << text << std::endl;
    } else {
        std::cout << "Unicast message from 0x" << std::hex << message.source
                  << std::dec << ": " << text << std::endl;
    }
}

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Sends an acknowledged message to every member of kGroup
 * @return true if the message was accepted for sending
 */
bool sendGroupMessage() {
    std::string text = "Group hello #" + std::to_string(++message_number);
    std::vector<uint8_t> payload(text.begin(), text.end());

    GroupSendOptions options;
    options.request_acks = true;  // Members acknowledge; see OnDeliveryResult
    options.window_ms = 8000;     // How long to collect acknowledgements
    options.max_retries = 1;      // Re-floods inside the window

    // Set before sending: a send that fails at once reports its outcome
    // before SendGroup() returns.
    awaiting_window = true;
    LoraMesher::MessageId id = mesher->SendGroup(kGroup, payload, options);
    if (id.source == 0) {
        awaiting_window = false;
        // Rejected, e.g. the node has not joined a network yet.
        std::cout << "Group send not accepted yet" << std::endl;
        return false;
    }

    std::cout << "Sent \"" << text << "\" to group 0x" << std::hex << kGroup
              << std::dec << " (seq=" << static_cast<int>(id.seq) << ")"
              << std::endl;
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
    if (LoraMesher::GenerateAddressFromHardware() == NODE_MANAGER_ADDRESS) {
        mesh_config.setNodeRole(NodeRole::NETWORK_MANAGER);
    } else {
        mesh_config.setNodeRole(NodeRole::NODE_ONLY);
    }

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
        return;
    }

    // Membership is kept across network re-joins.
    Result join_result = mesher->JoinGroup(kGroup);
    if (!join_result) {
        std::cerr << "Failed to join group: " << join_result.GetErrorMessage()
                  << std::endl;
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
    const bool interval_elapsed = millis() - last_send_ms >= kMinSendIntervalMs;
    if (interval_elapsed && !awaiting_window && mesher->GetTxQueueSize() == 0) {
        // Transmit in this node's next TX data slot.
        delay(mesher->GetTimeUntilNextDataSlot());
        sendGroupMessage();
        last_send_ms = millis();
    }
    delay(kPollIntervalMs);
}
#endif
