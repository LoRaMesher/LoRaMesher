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
#include <queue>
#include <random>
#include <vector>

#include "hardware/SPIMock.hpp"
#include "mock_radio.hpp"
#include "os/os_port.hpp"
#include "protocols/lora_mesh_protocol.hpp"
#include "types/error_codes/result.hpp"
#include "types/radio/radio_state.hpp"

using ::testing::A;

namespace loramesher {
namespace test {

size_t constexpr kGetTimeOnAirOverhead =
    100;  // Overhead in ms for time on air calculations

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

    /**
     * @brief Check if the radio can currently receive messages
     * 
     * @return true if radio is in receive mode, false otherwise
     */
    virtual bool CanReceive() const = 0;

    /**
     * @brief Get current radio state for debugging
     * 
     * @return Current radio state
     */
    virtual loramesher::radio::RadioState GetRadioState() const = 0;
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
    void UnregisterNode(uint32_t address) {
        nodes_.erase(address);
        sent_messages_.erase(address);
    }

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
        // Store the sent message for testing purposes
        sent_messages_[source].push_back(data);

        // Check if source exists
        if (nodes_.find(source) == nodes_.end()) {
            std::cerr << "Source node " << source << " not found in network"
                      << std::endl;
            return;
        }

        std::string hex_data;
        if (data.size() > 0) {
            char hex_byte[4];  // Extra space for the format
            for (uint8_t byte : data) {
                snprintf(hex_byte, sizeof(hex_byte), "%02X ", byte);
                hex_data += hex_byte;
            }
        }

        LOG_DEBUG("Transmitting message from 0x%04X, hex: %s", source,
                  hex_data.c_str());

        // Determine which nodes should receive the message
        for (auto& node_pair : nodes_) {
            uint32_t dest_address = node_pair.first;
            NodeInfo& dest_node = node_pair.second;

            LOG_DEBUG("Transmitting message from 0x%04X to 0x%04X", source,
                      dest_address);

            // Skip the source node
            if (dest_address == source) {
                LOG_DEBUG("Skipping transmission to self (0x%04X)", source);
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
            uint32_t delivery_time =
                current_time_ + delay + kGetTimeOnAirOverhead;

            // Queue the message for delivery
            QueueMessageDelivery(source, dest_address, data, delivery_time,
                                 rssi, snr);
        }
    }

    /**
     * @brief Get all sent messages from a specific node
     * 
     * @param node_address Address of the node
     * @return Vector containing all messages sent by the node
     */
    std::vector<std::vector<uint8_t>> GetSentMessages(
        uint32_t node_address) const {
        auto it = sent_messages_.find(node_address);
        if (it != sent_messages_.end()) {
            return it->second;
        }
        return std::vector<std::vector<uint8_t>>();
    }

    /**
     * @brief Get the last N messages sent by a specific node
     * 
     * @param node_address Address of the node
     * @param count Number of messages to retrieve (from most recent)
     * @return Vector containing the last N messages sent by the node
     */
    std::vector<std::vector<uint8_t>> GetLastSentMessages(uint32_t node_address,
                                                          size_t count) const {
        auto it = sent_messages_.find(node_address);
        if (it == sent_messages_.end() || it->second.empty()) {
            return std::vector<std::vector<uint8_t>>();
        }

        const auto& messages = it->second;
        size_t start_index =
            (count >= messages.size()) ? 0 : messages.size() - count;

        return std::vector<std::vector<uint8_t>>(messages.begin() + start_index,
                                                 messages.end());
    }

    /**
     * @brief Get filtered sent messages from a specific node
     * 
     * @param node_address Address of the node
     * @param filter Predicate function to filter messages
     * @return Vector containing messages that match the filter criteria
     */
    std::vector<std::vector<uint8_t>> GetFilteredSentMessages(
        uint32_t node_address,
        std::function<bool(const std::vector<uint8_t>&)> filter) const {

        auto it = sent_messages_.find(node_address);
        if (it == sent_messages_.end()) {
            return std::vector<std::vector<uint8_t>>();
        }

        std::vector<std::vector<uint8_t>> filtered_messages;
        for (const auto& message : it->second) {
            if (filter(message)) {
                filtered_messages.push_back(message);
            }
        }

        return filtered_messages;
    }

    /**
     * @brief Clear all sent messages for a specific node
     * 
     * @param node_address Address of the node
     */
    void ClearSentMessages(uint32_t node_address) {
        auto it = sent_messages_.find(node_address);
        if (it != sent_messages_.end()) {
            it->second.clear();
        }
    }

    /**
     * @brief Clear all sent messages for all nodes
     */
    void ClearAllSentMessages() { sent_messages_.clear(); }

    /**
     * @brief Get the number of messages sent by a specific node
     * 
     * @param node_address Address of the node
     * @return Number of messages sent by the node
     */
    size_t GetSentMessageCount(uint32_t node_address) const {
        auto it = sent_messages_.find(node_address);
        if (it != sent_messages_.end()) {
            return it->second.size();
        }
        return 0;
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
    void SetMessageDelay(uint32_t node1, uint32_t node2,
                         uint32_t delay_ms = 50) {
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
    std::map<uint32_t, std::vector<std::vector<uint8_t>>>
        sent_messages_;  ///< Store sent messages per node
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

        LOG_DEBUG(
            "[%u ms] - Queued message from 0x%04X to 0x%04X for delivery at %u "
            "ms",
            current_time_, source, destination, delivery_time);
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
            LOG_ERROR(
                "Message delivery failed - Node 0x%04X not found in network",
                msg.destination);
            return;
        }

        // Get the destination radio
        auto* radio = it->second.radio;
        if (!radio) {
            LOG_ERROR("Message delivery failed - Node 0x%04X radio not found",
                      msg.destination);
            return;
        }

        // Double-check that the radio can still receive when delivery time arrives
        if (!radio->CanReceive()) {
            radio::RadioState radio_state = radio->GetRadioState();
            LOG_ERROR(
                "[%u ms] - Message delivery cancelled - Node 0x%04X cannot "
                "receive "
                "(state: %d)",
                GetCurrentTime(), msg.destination,
                static_cast<int>(radio_state));
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
            LOG_DEBUG("Setting RTOSMock to virtual time mode");
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
        // LOG_DEBUG("Advancing time by %u ms", time_ms);
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
 * @brief Adapter class to connect MockRadio to VirtualNetwork
 */
class RadioToNetworkAdapter : public IRadioReceiver {
   public:
    RadioToNetworkAdapter(radio::test::MockRadio* radio,
                          VirtualNetwork& network, AddressType address)
        : radio_(radio), network_(network), address_(address) {
        // Set up original callback saving
        EXPECT_CALL(*radio_, setActionReceive(A<void (*)(void)>()))
            .WillRepeatedly(
                testing::DoAll(testing::SaveArg<0>(&original_callback_),
                               testing::Return(Result::Success())));

        // Set up packet data storage
        EXPECT_CALL(*radio_, getPacketLength())
            .WillRepeatedly(testing::Invoke([this]() -> size_t {
                if (message_queue_.empty()) {
                    return 0;
                }
                return message_queue_.front().data.size();
            }));

        EXPECT_CALL(*radio_, getRSSI())
            .WillRepeatedly(testing::Invoke([this]() -> int8_t {
                if (message_queue_.empty()) {
                    return -100;  // Default RSSI value when no message
                }
                return message_queue_.front().rssi;
            }));

        EXPECT_CALL(*radio_, getSNR())
            .WillRepeatedly(testing::Invoke([this]() -> int8_t {
                if (message_queue_.empty()) {
                    return 0;  // Default SNR value when no message
                }
                return message_queue_.front().snr;
            }));

        EXPECT_CALL(*radio_, readData(testing::_, testing::_))
            .WillRepeatedly(
                testing::Invoke([this](uint8_t* data, size_t len) -> Result {
                    if (message_queue_.empty()) {
                        return Result(LoraMesherErrorCode::kHardwareError,
                                      "No data received");
                    }

                    auto current_message = message_queue_.front();
                    message_queue_.pop();

                    if (len < current_message.data.size()) {
                        return Result(LoraMesherErrorCode::kBufferOverflow,
                                      "Buffer too small");
                    }

                    std::copy(current_message.data.begin(),
                              current_message.data.end(), data);
                    return Result::Success();
                }));
        EXPECT_CALL(*radio_, getTimeOnAir(testing::_))
            .WillRepeatedly(testing::Invoke([this](uint8_t length) -> uint32_t {
                return length * 10;  // TODO: Mock time on air
            }));                     // Default value
        EXPECT_CALL(*radio_, ClearActionReceive())
            .WillRepeatedly(testing::Return(Result::Success()));
        EXPECT_CALL(*radio_, Sleep())
            .WillRepeatedly(
                testing::DoAll(testing::Invoke([this]() {
                                   current_radio_state_ =
                                       loramesher::radio::RadioState::kSleep;
                               }),
                               testing::Return(Result::Success())));

        EXPECT_CALL(*radio_, StartReceive())
            .WillRepeatedly(
                testing::DoAll(testing::Invoke([this]() {
                                   current_radio_state_ =
                                       loramesher::radio::RadioState::kReceive;
                               }),
                               testing::Return(Result::Success())));
        EXPECT_CALL(*radio_, Begin(testing::_))
            .WillRepeatedly(testing::DoAll(
                testing::Invoke([this](const RadioConfig&) {
                    current_radio_state_ = loramesher::radio::RadioState::kIdle;
                }),
                testing::Return(Result::Success())));

        EXPECT_CALL(*radio_, getState())
            .WillRepeatedly(
                testing::Invoke([this]() -> loramesher::radio::RadioState {
                    return current_radio_state_;
                }));

        // Set up expectations for the mock radio
        EXPECT_CALL(*radio_, Send(testing::_, testing::_))
            .WillRepeatedly(testing::Invoke(
                [this, address](const uint8_t* data, size_t len) -> Result {
                    current_radio_state_ =
                        loramesher::radio::RadioState::kTransmit;

                    // Convert data to vector for convenience
                    std::vector<uint8_t> packet(data, data + len);

                    // Transmit via the virtual network
                    network_.TransmitMessage(address, packet);

                    // After sending, reset the radio state
                    // to receive mode
                    current_radio_state_ =
                        loramesher::radio::RadioState::kReceive;

                    return Result::Success();
                }));
    }

    ~RadioToNetworkAdapter() override {
        // Clear callback to prevent calls during destruction
        original_callback_ = nullptr;

        // Clear message queue to prevent access after destruction
        while (!message_queue_.empty()) {
            message_queue_.pop();
        }

        // Unregister from network
        network_.UnregisterNode(address_);
    }

    void ReceiveMessage(const std::vector<uint8_t>& data, int8_t rssi,
                        int8_t snr) override {
        // Queue the message to prevent race conditions with multiple simultaneous messages
        QueuedMessage msg;
        msg.data = data;
        msg.rssi = rssi;
        msg.snr = snr;
        message_queue_.push(msg);

        // Call the original callback if set
        if (original_callback_) {
            original_callback_();
        } else {
            LOG_ERROR("No original callback set for received message");
        }
    }

    /**
         * @brief Check if the radio can currently receive messages
         * 
         * @return true if radio is in receive mode, false otherwise
         */
    bool CanReceive() const override {
        return current_radio_state_ == loramesher::radio::RadioState::kReceive;
    }

    /**
         * @brief Get current radio state for debugging
         * 
         * @return Current radio state
         */
    loramesher::radio::RadioState GetRadioState() const override {
        return current_radio_state_;
    }

   private:
    struct QueuedMessage {
        std::vector<uint8_t> data;
        int8_t rssi;
        int8_t snr;
    };

    radio::test::MockRadio* radio_;
    VirtualNetwork& network_;
    AddressType address_;
    std::queue<QueuedMessage> message_queue_;
    void (*original_callback_)() = nullptr;
    loramesher::radio::RadioState current_radio_state_{
        loramesher::radio::RadioState::kIdle};  ///< Track current radio state
};

}  // namespace test
}  // namespace loramesher