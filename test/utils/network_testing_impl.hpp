/**
 * @file network_testing_impl.hpp
 * @brief Combined implementation of testing infrastructure for LoRaMesh protocol
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <vector>

#include "hardware/SPIMock.hpp"
#include "mock_radio.hpp"
#include "os/os_port.hpp"
#include "protocols/lora_mesh_protocol.hpp"
#include "types/error_codes/result.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Interface for radio receivers in the virtual network
 */
class IRadioReceiver {
   public:
    /**
     * @brief Virtual destructor
     */
    virtual ~IRadioReceiver() = default;

    /**
     * @brief Receive a message from the virtual network
     * 
     * @param data Message data
     * @param rssi Signal strength (-dBm)
     * @param snr Signal-to-noise ratio (dB)
     */
    virtual void ReceiveMessage(const std::vector<uint8_t>& data, int8_t rssi,
                                int8_t snr) = 0;
};

/**
 * @brief A virtual network for simulating LoRa radio communication between nodes
 */
class VirtualNetwork {
   public:
    /**
     * @brief Constructor
     */
    VirtualNetwork() : current_time_(0), packet_loss_rate_(0.0f) {
        // Initialize random number generator
        std::random_device rd;
        rng_ = std::mt19937(rd());
    }

    /**
     * @brief Register a node with the network
     * 
     * @param address Address of the node
     * @param radio Pointer to the node's radio receiver interface
     */
    void RegisterNode(uint32_t address, IRadioReceiver* radio) {
        if (nodes_.find(address) != nodes_.end()) {
            std::cerr << "Node with address " << address
                      << " already registered" << std::endl;
            return;
        }

        NodeInfo node_info;
        node_info.radio = radio;
        nodes_[address] = node_info;
    }

    /**
     * @brief Unregister a node from the network
     * 
     * @param address Address of the node to remove
     */
    void UnregisterNode(uint32_t address) { nodes_.erase(address); }

    /**
     * @brief Transmit a message from a source node to all nodes within range
     * 
     * @param source Source node address
     * @param data Message data
     * @param rssi Signal strength to simulate (-dBm)
     * @param snr Signal-to-noise ratio to simulate (dB)
     */
    void TransmitMessage(uint32_t source, const std::vector<uint8_t>& data,
                         int8_t rssi = -65, int8_t snr = 8) {
        // Check if source exists
        if (nodes_.find(source) == nodes_.end()) {
            std::cerr << "Source node " << source << " not found in network"
                      << std::endl;
            return;
        }

        // Determine which nodes should receive the message
        for (auto& node_pair : nodes_) {
            uint32_t dest_address = node_pair.first;
            NodeInfo& dest_node = node_pair.second;

            // Skip the source node
            if (dest_address == source) {
                continue;
            }

            // Check if link is active
            if (!IsLinkActive(source, dest_address)) {
                continue;
            }

            // Check for packet loss
            if (ShouldDropPacket()) {
                continue;
            }

            // Calculate delivery time
            uint32_t delay = GetLinkDelay(source, dest_address);
            uint32_t delivery_time = current_time_ + delay;

            // Queue the message for delivery
            QueueMessageDelivery(source, dest_address, data, delivery_time,
                                 rssi, snr);
        }
    }

    /**
     * @brief Control link status between two nodes
     * 
     * @param node1 First node address
     * @param node2 Second node address
     * @param active Whether the link should be active
     */
    void SetLinkStatus(uint32_t node1, uint32_t node2, bool active) {
        // Ensure bidirectional link update
        if (nodes_.find(node1) != nodes_.end()) {
            nodes_[node1].active_links[node2] = active;
        }
        if (nodes_.find(node2) != nodes_.end()) {
            nodes_[node2].active_links[node1] = active;
        }
    }

    /**
     * @brief Check if a link between two nodes is active
     * 
     * @param node1 First node address
     * @param node2 Second node address
     * @return true if link is active, false otherwise
     */
    bool IsLinkActive(uint32_t node1, uint32_t node2) const {
        auto it1 = nodes_.find(node1);
        if (it1 == nodes_.end())
            return false;

        auto& links = it1->second.active_links;
        auto it2 = links.find(node2);

        // If explicit link status not set, default to active
        if (it2 == links.end())
            return true;

        return it2->second;
    }

    /**
     * @brief Set message propagation delay between nodes
     * 
     * @param node1 First node address
     * @param node2 Second node address
     * @param delay_ms Delay in milliseconds
     */
    void SetMessageDelay(uint32_t node1, uint32_t node2, uint32_t delay_ms) {
        // Ensure bidirectional delay update
        if (nodes_.find(node1) != nodes_.end()) {
            nodes_[node1].link_delays[node2] = delay_ms;
        }
        if (nodes_.find(node2) != nodes_.end()) {
            nodes_[node2].link_delays[node1] = delay_ms;
        }
    }

    /**
     * @brief Set packet loss rate for the network
     * 
     * @param rate Loss rate (0.0 = no loss, 1.0 = all packets lost)
     */
    void SetPacketLossRate(float rate) {
        packet_loss_rate_ = std::min(1.0f, std::max(0.0f, rate));
    }

    /**
     * @brief Advance the network simulation time
     * 
     * @param time_ms Time to advance in milliseconds
     */
    void AdvanceTime(uint32_t time_ms) {
        current_time_ += time_ms;
        ProcessPendingMessages();
    }

    /**
     * @brief Get current simulation time
     * 
     * @return Current time in milliseconds
     */
    uint32_t GetCurrentTime() const { return current_time_; }

   private:
    /**
     * @brief Information about a node in the network
     */
    struct NodeInfo {
        IRadioReceiver* radio;
        std::map<uint32_t, bool> active_links;
        std::map<uint32_t, uint32_t> link_delays;
    };

    /**
     * @brief Information about a pending message
     */
    struct PendingMessage {
        uint32_t source;
        uint32_t destination;
        std::vector<uint8_t> data;
        uint32_t delivery_time;
        int8_t rssi;
        int8_t snr;
    };

    std::map<uint32_t, NodeInfo> nodes_;
    std::vector<PendingMessage> pending_messages_;
    uint32_t current_time_;
    float packet_loss_rate_;
    std::mt19937 rng_;

    /**
     * @brief Get delay between two nodes
     */
    uint32_t GetLinkDelay(uint32_t node1, uint32_t node2) const {
        auto it1 = nodes_.find(node1);
        if (it1 == nodes_.end())
            return 0;

        auto& delays = it1->second.link_delays;
        auto it2 = delays.find(node2);

        // If delay not set, default to 0
        if (it2 == delays.end())
            return 0;

        return it2->second;
    }

    /**
     * @brief Check if packet should be dropped based on loss rate
     */
    bool ShouldDropPacket() {
        if (packet_loss_rate_ <= 0.0f)
            return false;
        if (packet_loss_rate_ >= 1.0f)
            return true;

        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(rng_) < packet_loss_rate_;
    }

    /**
     * @brief Queue a message for delivery
     */
    void QueueMessageDelivery(uint32_t source, uint32_t destination,
                              const std::vector<uint8_t>& data,
                              uint32_t delivery_time, int8_t rssi, int8_t snr) {
        PendingMessage msg;
        msg.source = source;
        msg.destination = destination;
        msg.data = data;
        msg.delivery_time = delivery_time;
        msg.rssi = rssi;
        msg.snr = snr;

        pending_messages_.push_back(msg);
    }

    /**
     * @brief Process any pending messages that are due for delivery
     */
    void ProcessPendingMessages() {
        auto it = pending_messages_.begin();
        while (it != pending_messages_.end()) {
            if (it->delivery_time <= current_time_) {
                DeliverMessage(*it);
                it = pending_messages_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * @brief Deliver a message to its destination
     */
    void DeliverMessage(const PendingMessage& msg) {
        // Check if destination node exists
        auto it = nodes_.find(msg.destination);
        if (it == nodes_.end()) {
            return;
        }

        // Get the destination radio
        auto* radio = it->second.radio;
        if (!radio) {
            return;
        }

        // Deliver the message to the radio
        radio->ReceiveMessage(msg.data, msg.rssi, msg.snr);
    }
};

/**
 * @brief Controller for virtual time in tests
 */
class VirtualTimeController {
   public:
    /**
     * @brief Constructor
     * 
     * @param network Reference to the virtual network
     */
    VirtualTimeController(VirtualNetwork& network)
        : network_(network), current_time_(0) {
        // Register this instance as the global singleton
        instance_ = this;

#ifdef LORAMESHER_BUILD_NATIVE
        os::RTOSMock* rtos_mock = dynamic_cast<os::RTOSMock*>(&GetRTOS());

        if (rtos_mock) {
            rtos_mock->setTimeMode(os::RTOSMock::TimeMode::kVirtualTime);
        } else {
            throw std::runtime_error("RTOS is not an RTOSMock instance");
        }
#endif  // LORAMESHER_BUILD_NATIVE
    }

    /**
     * @brief Destructor
     */
    ~VirtualTimeController() {
        if (instance_ == this) {
            instance_ = nullptr;
        }
    }

    /**
     * @brief Get current virtual time
     * 
     * @return Current time in milliseconds
     */
    static uint32_t GetCurrentTime() {
        if (!instance_) {
            throw std::runtime_error("VirtualTimeController not initialized");
        }
        return instance_->current_time_;
    }

    /**
     * @brief Advance time by a specific amount
     * 
     * @param time_ms Time to advance in milliseconds
     */
    void AdvanceTime(uint32_t time_ms) {
        LOG_DEBUG("Advancing time by %u ms", time_ms);
        current_time_ += time_ms;
        network_.AdvanceTime(time_ms);
        ProcessTimeDependentEvents();

#ifdef LORAMESHER_BUILD_NATIVE
        os::RTOSMock* rtos_mock = dynamic_cast<os::RTOSMock*>(&GetRTOS());

        if (rtos_mock) {
            rtos_mock->advanceTime(time_ms);
        } else {
            throw std::runtime_error("RTOS is not an RTOSMock instance");
        }
#endif  // LORAMESHER_BUILD_NATIVE
    }

    /**
     * @brief Schedule a function to be called at a specific time
     * 
     * @param time Absolute time to trigger the callback
     * @param callback Function to call
     */
    void ScheduleAt(uint32_t time, std::function<void()> callback) {
        scheduled_events_.push_back({time, std::move(callback)});
    }

    /**
     * @brief Schedule a function to be called after a delay
     * 
     * @param delay_ms Delay in milliseconds
     * @param callback Function to call
     */
    void ScheduleAfter(uint32_t delay_ms, std::function<void()> callback) {
        scheduled_events_.push_back(
            {current_time_ + delay_ms, std::move(callback)});
    }

    /**
     * @brief Get time provider function for injection
     * 
     * @return Function that returns the current virtual time
     */
    std::function<uint32_t()> GetTimeProvider() {
        return []() {
            return GetCurrentTime();
        };
    }

   private:
    // Singleton instance for static access
    static VirtualTimeController* instance_;

    VirtualNetwork& network_;
    uint32_t current_time_;

    /**
     * @brief Scheduled event structure
     */
    struct ScheduledEvent {
        uint32_t trigger_time;
        std::function<void()> callback;
    };

    std::vector<ScheduledEvent> scheduled_events_;

    /**
     * @brief Process any events that are due to run
     */
    void ProcessTimeDependentEvents() {
        auto it = scheduled_events_.begin();
        while (it != scheduled_events_.end()) {
            if (it->trigger_time <= current_time_) {
                it->callback();
                it = scheduled_events_.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// Initialize static member
VirtualTimeController* VirtualTimeController::instance_ = nullptr;

/**
 * @brief Network-connected mock radio for testing LoRaMesh protocol
 * 
 * This class extends your existing MockRadio to connect it to the virtual network
 */
class NetworkConnectedMockRadio : public radio::test::MockRadio,
                                  public IRadioReceiver {
   public:
    /**
     * @brief Constructor
     * 
     * @param network Reference to the virtual network
     * @param address Address of this node
     */
    NetworkConnectedMockRadio(VirtualNetwork& network, uint32_t address)
        : radio::test::MockRadio(), network_(network), address_(address) {
        // Register with the virtual network
        network_.RegisterNode(address_, this);
    }

    /**
     * @brief Destructor
     */
    ~NetworkConnectedMockRadio() override { network_.UnregisterNode(address_); }

    /**
     * @brief Override Send to route through the virtual network
     * 
     * @param data Data to send
     * @param len Length of data
     * @return Result indicating success or failure
     */
    Result Send(const uint8_t* data, size_t len) override {
        // First call the parent's Send to maintain any behavior or expectations
        auto result = radio::test::MockRadio::Send(data, len);
        if (!result) {
            return result;
        }

        // Convert data to vector for convenience
        std::vector<uint8_t> packet(data, data + len);

        // Transmit via the virtual network
        network_.TransmitMessage(address_, packet, rssi_, snr_);

        return Result::Success();
    }

    /**
     * @brief Implementation of IRadioReceiver interface
     * 
     * @param data Received data
     * @param rssi Simulated RSSI value
     * @param snr Simulated SNR value
     */
    void ReceiveMessage(const std::vector<uint8_t>& data, int8_t rssi,
                        int8_t snr) override {
        // Store parameters for future calls to getRSSI, getSNR, etc.
        received_data_ = data;
        rssi_ = rssi;
        snr_ = snr;

        // Set packet length for getPacketLength
        packet_length_ = data.size();

        // Call the parent's callback if set
        if (receive_callback_) {
            receive_callback_();
        }
    }

    /**
     * @brief Get the received RSSI
     * 
     * @return RSSI value
     */
    int8_t getRSSI() override { return rssi_; }

    /**
     * @brief Get the received SNR
     * 
     * @return SNR value
     */
    int8_t getSNR() override { return snr_; }

    /**
     * @brief Get the packet length
     * 
     * @return Packet length
     */
    uint8_t getPacketLength() override { return packet_length_; }

    /**
     * @brief Read received data
     * 
     * @param data Buffer to read data into
     * @param len Length of buffer
     * @return Result indicating success or failure
     */
    Result readData(uint8_t* data, size_t len) override {
        if (received_data_.empty()) {
            return Result(LoraMesherErrorCode::kHardwareError,
                          "No data received");
        }

        if (len < received_data_.size()) {
            return Result(LoraMesherErrorCode::kBufferOverflow,
                          "Buffer too small");
        }

        std::copy(received_data_.begin(), received_data_.end(), data);
        return Result::Success();
    }

    /**
     * @brief Set the action receive callback
     * 
     * @param callback Function to call when data is received
     * @return Result indicating success or failure
     */
    Result setActionReceive(void (*callback)(void)) override {
        receive_callback_ = callback;
        return Result::Success();
    }

    /**
     * @brief Clear the action receive callback
     * 
     * @return Result indicating success or failure
     */
    Result ClearActionReceive() override {
        receive_callback_ = nullptr;
        return Result::Success();
    }

   private:
    VirtualNetwork& network_;
    uint32_t address_;
    std::vector<uint8_t> received_data_;
    size_t packet_length_{0};
    int8_t rssi_{-65};
    int8_t snr_{8};
    void (*receive_callback_)(void) = nullptr;
};

/**
 * @brief Factory for creating network-connected mock radios
 * 
 * This class allows us to override the default radio creation
 * with our network-connected mock radios for testing.
 */
class TestRadioFactory {
   public:
    /**
     * @brief Initialize the test radio factory
     * 
     * @param network Reference to the virtual network
     */
    static void Initialize(VirtualNetwork& network) {
        instance().network_ = &network;
        instance().initialized_ = true;
    }

    /**
     * @brief Register a node address for a specific set of pins
     * 
     * This maps pin configurations to node addresses for testing.
     * 
     * @param nss NSS pin
     * @param dio0 DIO0 pin
     * @param reset Reset pin
     * @param busy Busy pin
     * @param address Node address to use
     */
    static void RegisterNodeAddress(int nss, int dio0, int reset, int busy,
                                    uint32_t address) {
        NodePins pins{nss, dio0, reset, busy};
        instance().pin_to_address_map_[pins] = address;
    }

    /**
     * @brief Reset the factory to its initial state
     */
    static void Reset() {
        instance().initialized_ = false;
        instance().network_ = nullptr;
        instance().pin_to_address_map_.clear();
        instance().original_create_radio_ = nullptr;
    }

    /**
     * @brief Create a radio for testing
     * 
     * @param nss NSS pin
     * @param dio0 DIO0 pin
     * @param reset Reset pin
     * @param busy Busy pin
     * @param spi SPI interface
     * @return std::unique_ptr<radio::IRadio> Network-connected mock radio
     */
    static std::unique_ptr<radio::IRadio> CreateTestRadio(int nss, int dio0,
                                                          int reset, int busy,
                                                          SPIClass& spi) {
        if (!instance().initialized_) {
            throw std::runtime_error("TestRadioFactory not initialized");
        }

        // Find node address based on pins
        NodePins pins{nss, dio0, reset, busy};
        auto it = instance().pin_to_address_map_.find(pins);
        if (it == instance().pin_to_address_map_.end()) {
            throw std::runtime_error(
                "Node address not registered for these pins");
        }

        uint32_t address = it->second;

        // Create network-connected mock radio
        return std::make_unique<NetworkConnectedMockRadio>(*instance().network_,
                                                           address);
    }

    /**
     * @brief Install the test radio factory
     * 
     * This replaces the original CreateRadio function with our test version.
     * 
     * @param original_func Pointer to store the original function
     */
    static void Install(std::function<std::unique_ptr<radio::IRadio>(
                            int, int, int, int, SPIClass&)>* original_func) {
        // Store the original function
        if (original_func) {
            instance().original_create_radio_ = *original_func;
            // Replace with our test function
            *original_func = CreateTestRadio;
        }
    }

    /**
     * @brief Uninstall the test radio factory
     * 
     * This restores the original CreateRadio function.
     * 
     * @param func_ptr Pointer to restore the original function
     */
    static void Uninstall(std::function<std::unique_ptr<radio::IRadio>(
                              int, int, int, int, SPIClass&)>* func_ptr) {
        if (func_ptr && instance().original_create_radio_) {
            // Restore the original function
            *func_ptr = instance().original_create_radio_;
        }
    }

   private:
    /**
     * @brief Structure to represent a set of pins
     */
    struct NodePins {
        int nss;
        int dio0;
        int reset;
        int busy;

        bool operator<(const NodePins& other) const {
            if (nss != other.nss)
                return nss < other.nss;
            if (dio0 != other.dio0)
                return dio0 < other.dio0;
            if (reset != other.reset)
                return reset < other.reset;
            return busy < other.busy;
        }
    };

    // Singleton pattern
    static TestRadioFactory& instance() {
        static TestRadioFactory instance;
        return instance;
    }

    TestRadioFactory() = default;

    bool initialized_{false};
    VirtualNetwork* network_{nullptr};
    std::map<NodePins, uint32_t> pin_to_address_map_;
    std::function<std::unique_ptr<radio::IRadio>(int, int, int, int, SPIClass&)>
        original_create_radio_;
};

/**
 * @brief Helper function to inject time function into LoRaMeshProtocol
 * 
 * This function allows tests to override the protocol's internal time
 * mechanism with a controlled virtual time.
 * 
 * @param protocol Protocol instance to inject time function into
 * @param time_function Time function to inject
 */
void InjectTimeFunction(protocols::LoRaMeshProtocol& protocol,
                        std::function<uint32_t()> time_function) {
#ifdef DEBUG
    protocol.SetTimeFunction(time_function);
#endif
}

}  // namespace test
}  // namespace loramesher