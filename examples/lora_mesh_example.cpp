/**
 * @file loramesher_example.cpp
 * @brief Example showing how to use LoraMesher with the LoRaMesh protocol
 *
 * This example demonstrates the application of the LoraMesher library
 * to set up a LoRa mesh network using the LoRaMesh protocol. It includes
 * configuration of hardware, radio, and protocol settings, as well as
 * sending and receiving messages.
 */

#include <iostream>

#include "loramesher.hpp"

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

// Simple data received callback (recommended approach)
void OnDataReceived(AddressType source, const std::vector<uint8_t>& data) {
    // Recommendation: Forward to separate task for processing
    std::cout << "Received data from: 0x" << std::hex << source << std::dec
              << " (" << data.size() << " bytes)" << std::endl;

    // Process data in your application...
    // For example: processDataInSeparateTask(source, data);
}

// Legacy message received callback (still available for advanced users)
void OnMessageReceived(const BaseMessage& msg) {
    // Process received message
    std::cout << "Received message from: " << msg.GetHeader().GetSource()
              << std::endl;

    // Access message payload
    auto payload = msg.GetPayload();
    // Process payload...
}

void ConfigureAndUseLoraMesher() {
    // Step 1: Set up hardware configuration
    PinConfig pinConfig(LORA_CS,   // NSS pin
                        LORA_RST,  // Reset pin
                        LORA_IRQ,  // DIO0 pin
                        LORA_IO1   // DIO1 pin
    );

    // Step 2: Set up radio configuration
    RadioConfig radioConfig(LORA_RADIO_TYPE, LORA_FREQUENCY,
                            LORA_SPREADING_FACTOR, LORA_BANDWITH,
                            LORA_CODING_RATE, LORA_POWER, LORA_SYNC_WORD,
                            LORA_CRC, LORA_PREAMBLE_LENGTH);

    // Step 3: Set up LoRaMesh protocol configuration
    LoRaMeshProtocolConfig mesh_config(0,       // Auto-assign node address
                                       60000,   // Hello interval: 60 seconds
                                       180000,  // Route timeout: 180 seconds
                                       10);     // Max hops: 10

    // Step 4: Create protocol configuration
    ProtocolConfig protocol_config;
    protocol_config.setLoRaMeshConfig(mesh_config);

    // Step 5: Create LoraMesher configuration
    mesher = LoraMesher::Builder()
                 .withRadioConfig(radioConfig)
                 .withPinConfig(pinConfig)
                 .withLoRaMeshProtocol(mesh_config)  // Use LoRaMesh protocol
                 .withAutoAddressFromHardware(
                     true)  // Enable hardware-based addressing (default)
                 // Alternatively, you can set a specific address:
                 // .withNodeAddress(0x1234)
                 .Build();

    std::cout << "Node address: 0x" << std::hex << mesher->GetNodeAddress()
              << std::dec << std::endl;

    // Step 6: Set up data callback
    mesher->SetDataCallback(OnDataReceived);

    // Step 7: Start LoraMesher
    Result init_result = mesher->Start();
    if (!init_result) {
        std::cerr << "Failed to start LoraMesher: "
                  << init_result.GetErrorMessage() << std::endl;
        return;
    }

    // Step 9: Access the LoRaMesh protocol for protocol-specific operations
    auto mesh_protocol = mesher->GetLoRaMeshProtocol();
    if (mesh_protocol) {
        // Set up route update callback
        mesh_protocol->SetRouteUpdateCallback(
            [](bool route_updated, AddressType destination,
               AddressType next_hop, uint8_t hop_count) {
                if (route_updated) {
                    std::cout << "Route updated - Destination: " << destination
                              << ", Next hop: " << next_hop
                              << ", Hops: " << static_cast<int>(hop_count)
                              << std::endl;
                } else {
                    std::cout
                        << "Route removed for destination: " << destination
                        << std::endl;
                }
            });
    }

    // Step 10: Send data (simplified approach)
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    Result send_result = mesher->Send(2, data);  // Send to node address 2
    if (!send_result) {
        std::cerr << "Failed to send data: " << send_result.GetErrorMessage()
                  << std::endl;
    }

    // Step 11: Access network information
    auto routes = mesher->GetRoutingTable();
    std::cout << "Routing table has " << routes.size()
              << " entries:" << std::endl;
    for (const auto& route : routes) {
        std::cout << "  Destination: 0x" << std::hex << route.destination
                  << ", Next hop: 0x" << route.next_hop
                  << ", Hops: " << std::dec << static_cast<int>(route.hop_count)
                  << ", Valid: " << (route.is_valid ? "yes" : "no")
                  << std::endl;
    }

    auto status = mesher->GetNetworkStatus();
    std::cout << "Network status: State="
              << static_cast<int>(status.current_state) << ", Manager=0x"
              << std::hex << status.network_manager << ", Slot=" << std::dec
              << status.current_slot << ", Nodes=" << status.connected_nodes
              << std::endl;

    // Application main loop...

    // When done, stop LoraMesher
    mesher->Stop();
}