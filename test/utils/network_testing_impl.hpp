/**
 * @file network_testing_impl.hpp
 * @brief Combined implementation of testing infrastructure for LoRaMesh protocol
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <vector>

#include "hardware/SPIMock.hpp"
#include "mock_radio.hpp"
#include "os/os_port.hpp"
#include "protocols/lora_mesh_protocol.hpp"
#include "types/error_codes/result.hpp"
#include "types/radio/radio_state.hpp"

// Forward declaration for RadioLibRadio
namespace loramesher {
namespace radio {
class RadioLibRadio;
}
}  // namespace loramesher

using ::testing::A;

namespace loramesher {
namespace test {

/**
 * @brief Calculate Time-on-Air for LoRa transmission
 * 
 * @param payload_length Length of payload in bytes
 * @param sf Spreading Factor (7-12)
 * @param bw Bandwidth in Hz (125000, 250000, 500000)
 * @param cr Coding Rate (5-8 for 4/5 to 4/8)
 * @param preamble_length Preamble length in symbols (default: 8)
 * @param header_enabled Whether explicit header is enabled (default: true)
 * @param crc_enabled Whether CRC is enabled (default: true)
 * @return Time-on-Air in milliseconds
 */
inline uint32_t CalculateLoRaTimeOnAir(
    uint8_t payload_length, uint8_t sf = 7, uint32_t bw = 125000,
    uint8_t cr = 5,  // 5=4/5, 6=4/6, 7=4/7, 8=4/8
    uint8_t preamble_length = 8, bool header_enabled = true,
    bool crc_enabled = true) {
    // Symbol duration in seconds
    double symbol_duration = (1 << sf) / static_cast<double>(bw);

    // Preamble duration in seconds
    double preamble_duration = (preamble_length + 4.25) * symbol_duration;

    double payload_symbols = 0;
    if (payload_length > 0) {
        // Standard Semtech formula (SX1276 datasheet §4.1.1.6)
        // LDRO (DE=1) required when symbol time > 16 ms (SF11/12 at 125kHz)
        int de = (symbol_duration > 0.016) ? 1 : 0;
        int ih = header_enabled ? 0 : 1;  // IH=1 for implicit header
        int crc = crc_enabled ? 1 : 0;
        // Library cr=5..8 equals (CR_raw+4) directly
        int cr_mult = cr;

        int numerator = 8 * payload_length - 4 * sf + 28 + 16 * crc - 20 * ih;
        int denominator = 4 * (sf - 2 * de);
        int ceil_val = std::max(0, (numerator + denominator - 1) / denominator);

        double sym = 8.0 + static_cast<double>(ceil_val) * cr_mult;
        payload_symbols = sym * symbol_duration;
    }

    // Total time in seconds
    double total_time = preamble_duration + payload_symbols;

    // Convert to milliseconds and round up
    return static_cast<uint32_t>(std::ceil(total_time * 1000));
}

/**
 * @brief Get Time-on-Air overhead for virtual network simulation
 * 
 * Uses realistic LoRa parameters for testing:
 * - SF7, BW125kHz, CR4/5, 8 symbol preamble
 * 
 * @param payload_length Length of payload in bytes
 * @return Time-on-Air in milliseconds
 */
inline uint32_t GetTimeOnAirOverhead(uint8_t payload_length) {
    return CalculateLoRaTimeOnAir(payload_length, 7, 125000, 5, 8, true, true);
}

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
    virtual void ReceiveMessage(const std::vector<uint8_t>& data, float rssi,
                                float snr) = 0;

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
        RegisterNode(address, radio, RadioConfig());
    }

    void RegisterNode(uint32_t address, IRadioReceiver* radio,
                      const RadioConfig& config) {
        if (nodes_.find(address) != nodes_.end()) {
            std::cerr << "Node with address " << address
                      << " already registered" << std::endl;
            return;
        }

        NodeInfo node_info;
        node_info.radio = radio;
        node_info.radio_config = config;
        nodes_[address] = node_info;
    }

    /**
     * @brief Unregister a node from the network
     * 
     * @param address Address of the node to remove
     */
    void UnregisterNode(uint32_t address) {
        nodes_.erase(address);
        std::lock_guard<std::mutex> lock(sent_messages_mutex_);
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
                         float rssi = -65.0f, float snr = 8.0f) {
        // Store the sent message for testing purposes
        {
            std::lock_guard<std::mutex> lock(sent_messages_mutex_);
            sent_messages_[source].push_back(data);
        }

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
        const auto& src_config = nodes_[source].radio_config;
        uint32_t toa = CalculateLoRaTimeOnAir(
            static_cast<uint8_t>(data.size()), src_config.getSpreadingFactor(),
            static_cast<uint32_t>(src_config.getBandwidth() * 1000),
            src_config.getCodingRate(), src_config.getPreambleLength(), true,
            src_config.getCRC());
        LOG_DEBUG("Time-on-Air for message: %u ms", toa);

        // Determine which nodes should receive the message
        for (auto& node_pair : nodes_) {
            uint32_t dest_address = node_pair.first;

            // Skip the source node
            if (dest_address == source) {
                LOG_DEBUG("Skipping transmission to self (0x%04X)", source);
                continue;
            }

            // Check if link is active
            if (!IsLinkActive(source, dest_address)) {
                continue;
            }

            // Check for per-link packet loss
            if (ShouldDropPacketForLink(source, dest_address)) {
                continue;
            }

            // Check for global packet loss
            if (ShouldDropPacket()) {
                continue;
            }

            // Calculate delivery time
            uint32_t delay = GetLinkDelay(source, dest_address);
            uint32_t delivery_time = current_time_ + delay + toa;

            // Queue the message for delivery with timing metadata
            QueueMessageDelivery(source, dest_address, data, current_time_, toa,
                                 delivery_time, rssi, snr);
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
        std::lock_guard<std::mutex> lock(sent_messages_mutex_);
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
        std::lock_guard<std::mutex> lock(sent_messages_mutex_);
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
        std::lock_guard<std::mutex> lock(sent_messages_mutex_);
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
        std::lock_guard<std::mutex> lock(sent_messages_mutex_);
        auto it = sent_messages_.find(node_address);
        if (it != sent_messages_.end()) {
            it->second.clear();
        }
    }

    /**
     * @brief Clear all sent messages for all nodes
     */
    void ClearAllSentMessages() {
        std::lock_guard<std::mutex> lock(sent_messages_mutex_);
        sent_messages_.clear();
    }

    /**
     * @brief Get the number of messages sent by a specific node
     *
     * @param node_address Address of the node
     * @return Number of messages sent by the node
     */
    size_t GetSentMessageCount(uint32_t node_address) const {
        std::lock_guard<std::mutex> lock(sent_messages_mutex_);
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
     * @brief Set a unidirectional link status between two nodes
     *
     * Only affects packets transmitted by 'from'. When active=true,
     * 'to' will receive transmissions from 'from'. Does not affect
     * the reverse direction.
     */
    void SetDirectionalLink(uint32_t from, uint32_t to, bool active) {
        if (nodes_.find(from) != nodes_.end()) {
            nodes_[from].active_links[to] = active;
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

        // If explicit link status not set, default to inactive
        if (it2 == links.end()) {
            return false;
        }

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
     * @brief Set per-link packet loss rate (one direction)
     *
     * @param from_addr Source node address
     * @param to_addr Destination node address
     * @param rate Loss rate (0.0 = no loss, 1.0 = all packets lost)
     */
    void SetDirectionalLinkLoss(uint32_t from_addr, uint32_t to_addr,
                                float rate) {
        auto it = nodes_.find(from_addr);
        if (it != nodes_.end()) {
            it->second.link_loss_rates[to_addr] =
                std::min(1.0f, std::max(0.0f, rate));
        }
    }

    /**
     * @brief Set per-link packet loss rate (both directions)
     */
    void SetLinkLoss(uint32_t node1, uint32_t node2, float rate) {
        SetDirectionalLinkLoss(node1, node2, rate);
        SetDirectionalLinkLoss(node2, node1, rate);
    }

    /**
     * @brief Advance the network simulation time
     * 
     * @param time_ms Time to advance in milliseconds
     */
    bool AdvanceTime(uint32_t time_ms) {
        current_time_ += time_ms;
        return ProcessPendingMessages() > 0;
    }

    /**
     * @brief Get current simulation time
     * 
     * @return Current time in milliseconds
     */
    uint32_t GetCurrentTime() const { return current_time_; }

    uint32_t GetDroppedMessageCount() const {
        return dropped_message_count_.load();
    }

    void ResetDroppedMessageCount() { dropped_message_count_ = 0; }

   private:
    /**
     * @brief Information about a node in the network
     */
    struct NodeInfo {
        IRadioReceiver* radio;
        std::map<uint32_t, bool> active_links;
        std::map<uint32_t, uint32_t> link_delays;
        std::map<uint32_t, float> link_loss_rates;
        RadioConfig radio_config;
    };

    /**
     * @brief Information about a pending message
     */
    struct PendingMessage {
        uint32_t source;
        uint32_t destination;
        std::vector<uint8_t> data;
        uint32_t transmission_start_time;  ///< When transmission started
        uint32_t time_on_air;              ///< Duration of transmission in ms
        uint32_t delivery_time;            ///< transmission_start + delay + toa
        float rssi;
        float snr;

        /**
         * @brief Get the end time of this transmission's on-air window
         * @return Time when this transmission ends (start + toa)
         */
        uint32_t GetTransmissionEndTime() const {
            return transmission_start_time + time_on_air;
        }

        /**
         * @brief Check if this message's on-air window overlaps with another
         * @param other The other pending message to check against
         * @return true if the on-air windows overlap
         */
        bool OverlapsWith(const PendingMessage& other) const {
            // Two windows overlap if: start_A < end_B AND start_B < end_A
            return transmission_start_time < other.GetTransmissionEndTime() &&
                   other.transmission_start_time < GetTransmissionEndTime();
        }
    };

    std::map<uint32_t, NodeInfo> nodes_;
    std::vector<PendingMessage> pending_messages_;
    mutable std::mutex
        pending_messages_mutex_;  ///< Mutex for thread-safe access to pending_messages_
    std::map<uint32_t, std::vector<std::vector<uint8_t>>>
        sent_messages_;  ///< Store sent messages per node
    mutable std::mutex
        sent_messages_mutex_;  ///< Mutex for thread-safe access to sent_messages_
    uint32_t current_time_;
    float packet_loss_rate_;
    std::mt19937 rng_;
    std::mt19937 link_loss_rng_{
        42};  ///< Fixed seed for deterministic per-link loss
    std::atomic<uint32_t> dropped_message_count_{0};

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
     * @brief Check if packet should be dropped based on per-link loss rate
     */
    bool ShouldDropPacketForLink(uint32_t from_addr, uint32_t to_addr) {
        auto it = nodes_.find(from_addr);
        if (it == nodes_.end())
            return false;
        auto loss_it = it->second.link_loss_rates.find(to_addr);
        if (loss_it == it->second.link_loss_rates.end() ||
            loss_it->second <= 0.0f)
            return false;
        if (loss_it->second >= 1.0f)
            return true;
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(link_loss_rng_) < loss_it->second;
    }

    /**
     * @brief Check if packet should be dropped based on global loss rate
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
                              uint32_t transmission_start_time,
                              uint32_t time_on_air, uint32_t delivery_time,
                              float rssi, float snr) {
        PendingMessage msg;
        msg.source = source;
        msg.destination = destination;
        msg.data = data;
        msg.transmission_start_time = transmission_start_time;
        msg.time_on_air = time_on_air;
        msg.delivery_time = delivery_time;
        msg.rssi = rssi;
        msg.snr = snr;

        {
            std::lock_guard<std::mutex> lock(pending_messages_mutex_);
            pending_messages_.push_back(msg);
        }

        LOG_DEBUG(
            "[%u ms] - Queued message from 0x%04X to 0x%04X for delivery at %u "
            "ms (tx_start: %u, toa: %u)",
            current_time_, source, destination, delivery_time,
            transmission_start_time, time_on_air);
    }

    /**
     * @brief Detect collisions among messages targeting the same destination
     *
     * @param messages Vector of messages for a single destination
     * @return Vector of messages that did not collide (safe to deliver)
     */
    std::vector<PendingMessage> DetectAndFilterCollisions(
        std::vector<PendingMessage>& messages) {
        if (messages.size() <= 1) {
            return messages;  // No collision possible with 0 or 1 message
        }

        // Track which messages are involved in collisions
        std::vector<bool> collided(messages.size(), false);

        // Check each pair of messages for overlapping on-air windows
        for (size_t i = 0; i < messages.size(); ++i) {
            for (size_t j = i + 1; j < messages.size(); ++j) {
                if (messages[i].OverlapsWith(messages[j])) {
                    // Both messages in the collision are affected
                    collided[i] = true;
                    collided[j] = true;

                    LOG_WARNING(
                        "[COLLISION] Messages from 0x%04X and 0x%04X collided "
                        "at destination 0x%04X (windows: [%u-%u] vs [%u-%u])",
                        messages[i].source, messages[j].source,
                        messages[i].destination,
                        messages[i].transmission_start_time,
                        messages[i].GetTransmissionEndTime(),
                        messages[j].transmission_start_time,
                        messages[j].GetTransmissionEndTime());
                }
            }
        }

        // Collect non-collided messages
        std::vector<PendingMessage> non_collided;
        for (size_t i = 0; i < messages.size(); ++i) {
            if (!collided[i]) {
                non_collided.push_back(messages[i]);
            } else {
                LOG_DEBUG(
                    "[COLLISION] Dropping message from 0x%04X to 0x%04X due to "
                    "collision",
                    messages[i].source, messages[i].destination);
            }
        }

        return non_collided;
    }

    /**
     * @brief Process any pending messages that are due for delivery
     *
     * This method groups messages by destination and performs collision
     * detection. Messages with overlapping on-air windows at the same
     * receiver are dropped (simulating real radio behavior).
     */
    size_t ProcessPendingMessages() {
        // Extract messages due for delivery under lock
        std::vector<PendingMessage> messages_to_deliver;
        {
            std::lock_guard<std::mutex> lock(pending_messages_mutex_);
            auto it = pending_messages_.begin();
            while (it != pending_messages_.end()) {
                if (it->delivery_time <= current_time_) {
                    messages_to_deliver.push_back(*it);
                    it = pending_messages_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Group messages by destination for collision detection
        std::map<uint32_t, std::vector<PendingMessage>> by_destination;
        for (auto& msg : messages_to_deliver) {
            by_destination[msg.destination].push_back(msg);
        }

        // For each destination, detect collisions and deliver non-collided messages
        size_t delivered = 0;
        for (auto& [dest, dest_messages] : by_destination) {
            auto non_collided = DetectAndFilterCollisions(dest_messages);
            for (const auto& msg : non_collided) {
                if (DeliverMessage(msg)) {
                    ++delivered;
                }
            }
        }
        return delivered;
    }

    /**
     * @brief Deliver a message to its destination
     *
     * @return true if the message was accepted by the radio, false if the
     *         radio was busy (message dropped)
     */
    bool DeliverMessage(const PendingMessage& msg) {
        // Set thread address to destination node for correct log attribution
        char addr_str[8];
        snprintf(addr_str, sizeof(addr_str), "0x%04X", msg.destination);
        GetRTOS().SetCurrentTaskNodeAddress(addr_str);

        auto it = nodes_.find(msg.destination);
        if (it == nodes_.end()) {
            LOG_ERROR(
                "Message delivery failed - Node 0x%04X not found in network",
                msg.destination);
            GetRTOS().SetCurrentTaskNodeAddress("0xFFFF");
            return false;
        }

        auto* radio = it->second.radio;
        if (!radio) {
            LOG_ERROR("Message delivery failed - Node 0x%04X radio not found",
                      msg.destination);
            GetRTOS().SetCurrentTaskNodeAddress("0xFFFF");
            return false;
        }

        // Drop message if receiver can't accept it — matches real LoRa PHY
        // where packets arriving while the radio is off/busy are lost
        if (!radio->CanReceive()) {
            LOG_DEBUG(
                "[%u ms] Message from 0x%04X dropped at 0x%04X - receiver "
                "unavailable (state=%d)",
                current_time_, msg.source, msg.destination,
                static_cast<int>(radio->GetRadioState()));
            ++dropped_message_count_;
            GetRTOS().SetCurrentTaskNodeAddress("0xFFFF");
            return false;
        }

        radio->ReceiveMessage(msg.data, msg.rssi, msg.snr);
        GetRTOS().SetCurrentTaskNodeAddress("0xFFFF");
        return true;
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
        if (instance_ == this)
            instance_ = nullptr;
#ifdef LORAMESHER_BUILD_NATIVE
        os::RTOSMock* rtos_mock = dynamic_cast<os::RTOSMock*>(&GetRTOS());
        if (rtos_mock) {
            rtos_mock->setTimeMode(os::RTOSMock::TimeMode::kRealTime);
        }
#endif
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

        ProcessTimeDependentEvents();

#ifdef LORAMESHER_BUILD_NATIVE
        network_.AdvanceTime(time_ms);

        os::RTOSMock* rtos_mock = dynamic_cast<os::RTOSMock*>(&GetRTOS());

        if (rtos_mock) {
            rtos_mock->advanceTime(time_ms);
        } else {
            throw std::runtime_error("RTOS is not an RTOSMock instance");
        }
#else
        network_.AdvanceTime(time_ms);
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
inline VirtualTimeController* VirtualTimeController::instance_ = nullptr;

/**
 * @brief Adapter class to connect MockRadio to VirtualNetwork
 */
class RadioToNetworkAdapter : public IRadioReceiver {
   public:
    RadioToNetworkAdapter(radio::test::MockRadio* radio,
                          VirtualNetwork& network, AddressType address,
                          radio::RadioLibRadio* radio_lib_instance)
        : radio_(radio), network_(network), address_(address) {

        // Set the RadioLibRadio instance on the MockRadio for instance-aware notifications
        radio_->SetRadioLibInstance(radio_lib_instance);
        LOG_DEBUG(
            "RadioToNetworkAdapter: Set RadioLibRadio instance %p on "
            "MockRadio %p",
            static_cast<void*>(radio_lib_instance), static_cast<void*>(radio_));

        // Set up original callback saving
        EXPECT_CALL(*radio_, setActionReceive(A<void (*)(void)>()))
            .WillRepeatedly(
                testing::DoAll(testing::SaveArg<0>(&original_callback_),
                               testing::Return(Result::Success())));

        // Set up packet data storage
        EXPECT_CALL(*radio_, getPacketLength())
            .WillRepeatedly(testing::Invoke([this]() -> size_t {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (message_queue_.empty()) {
                    LOG_ERROR(
                        "MockRadio: getPacketLength() - No messages "
                        "in queue (queue empty)");
                    return 0;
                }
                size_t packet_size = message_queue_.front().data.size();
                LOG_DEBUG(
                    "MockRadio: getPacketLength() - Queue size: %zu, "
                    "packet size: %zu",
                    message_queue_.size(), packet_size);
                return packet_size;
            }));

        EXPECT_CALL(*radio_, getRSSI())
            .WillRepeatedly(testing::Invoke([this]() -> int8_t {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (message_queue_.empty()) {
                    return -100;  // Default RSSI value when no message
                }
                return message_queue_.front().rssi;
            }));

        EXPECT_CALL(*radio_, getSNR())
            .WillRepeatedly(testing::Invoke([this]() -> int8_t {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (message_queue_.empty()) {
                    return 0;  // Default SNR value when no message
                }
                return message_queue_.front().snr;
            }));

        EXPECT_CALL(*radio_, readData(testing::_, testing::_))
            .WillRepeatedly(
                testing::Invoke([this](uint8_t* data, size_t len) -> Result {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    if (message_queue_.empty()) {
                        LOG_ERROR("MockRadio: readData() - No data received");
                        return Result(LoraMesherErrorCode::kHardwareError,
                                      "No data received");
                    }

                    auto current_message = message_queue_.front();
                    message_queue_.pop();
                    LOG_DEBUG(
                        "MockRadio: readData() - Consumed message, "
                        "queue size after pop: %zu",
                        message_queue_.size());

                    if (len < current_message.data.size()) {
                        LOG_ERROR(
                            "MockRadio: readData() - Buffer too small "
                            "for received message: "
                            "expected %zu, got %zu",
                            current_message.data.size(), len);
                        return Result(LoraMesherErrorCode::kBufferOverflow,
                                      "Buffer too small");
                    }

                    std::copy(current_message.data.begin(),
                              current_message.data.end(), data);
                    return Result::Success();
                }));
        EXPECT_CALL(*radio_, getTimeOnAir(testing::_))
            .WillRepeatedly(testing::Invoke([this](uint8_t length) -> uint32_t {
                // Use the stored radio configuration for ToA calculation
                return CalculateLoRaTimeOnAir(
                    length, radio_config_.getSpreadingFactor(),
                    static_cast<uint32_t>(radio_config_.getBandwidth() *
                                          1000),  // Convert kHz to Hz
                    radio_config_.getCodingRate(),
                    radio_config_.getPreambleLength(),
                    true,  // header enabled
                    radio_config_.getCRC());
            }));
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
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            while (!message_queue_.empty()) {
                message_queue_.pop();
            }
        }

        // Unregister from network
        network_.UnregisterNode(address_);
    }

    void ReceiveMessage(const std::vector<uint8_t>& data, float rssi,
                        float snr) override {
        // Queue the message to prevent race conditions with multiple simultaneous messages
        QueuedMessage msg;
        msg.data = data;
        msg.rssi = rssi;
        msg.snr = snr;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            message_queue_.push(msg);
        }

        // Use instance-aware notification instead of static ISR callback
        LOG_DEBUG(
            "RadioToNetworkAdapter: Notifying processing task via "
            "MockRadio");
        radio_->NotifyProcessingTask();
    }

    /**
         * @brief Check if the radio can currently receive messages
         * 
         * @return true if radio is in receive mode, false otherwise
         */
    bool CanReceive() const override {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return current_radio_state_ ==
                   loramesher::radio::RadioState::kReceive &&
               message_queue_.empty();
    }

    /**
         * @brief Get current radio state for debugging
         * 
         * @return Current radio state
         */
    loramesher::radio::RadioState GetRadioState() const override {
        return current_radio_state_;
    }

    /**
     * @brief Set radio configuration for ToA calculations
     * 
     * @param config Radio configuration with LoRa parameters
     */
    void SetRadioConfig(const RadioConfig& config) { radio_config_ = config; }

   private:
    struct QueuedMessage {
        std::vector<uint8_t> data;
        float rssi;
        float snr;
    };

    radio::test::MockRadio* radio_;
    VirtualNetwork& network_;
    AddressType address_;
    std::queue<QueuedMessage> message_queue_;
    mutable std::mutex queue_mutex_;
    std::function<void()> original_callback_ = nullptr;  ///< Original callback
    std::atomic<loramesher::radio::RadioState> current_radio_state_{
        loramesher::radio::RadioState::kIdle};  ///< Track current radio state
    RadioConfig radio_config_;  ///< Radio configuration for ToA calculations
};

}  // namespace test
}  // namespace loramesher