/**
 * @file protocol_configuration.hpp
 * @brief Configuration classes for communication protocols
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "types/messages/base_message.hpp"
#include "types/protocols/protocol.hpp"

namespace loramesher {

/**
 * @brief Common base class for all protocol configurations
 * 
 * This class provides common configuration parameters that apply to all protocol types,
 * while allowing protocol-specific configurations to extend it.
 */
class BaseProtocolConfig {
   public:
    /**
     * @brief Constructor with common protocol parameters
     * 
     * @param node_address The node's address in the network (0 means auto-assign)
     */
    explicit BaseProtocolConfig(AddressType node_address = 0)
        : node_address_(node_address) {}

    /**
     * @brief Virtual destructor
     */
    virtual ~BaseProtocolConfig() = default;

    /**
     * @brief Get the node address
     * 
     * @return AddressType The configured node address (0 if auto-assign)
     */
    AddressType getNodeAddress() const { return node_address_; }

    /**
     * @brief Set the node address
     * 
     * @param address The node address to set (0 for auto-assign)
     */
    void setNodeAddress(AddressType address) { node_address_ = address; }

    /**
     * @brief Get the protocol type this configuration applies to
     * 
     * @return protocols::ProtocolType The type of protocol
     */
    virtual protocols::ProtocolType getProtocolType() const = 0;

    /**
     * @brief Check if configuration is valid
     * 
     * @return bool True if configuration is valid
     */
    virtual bool IsValid() const { return true; }

    /**
     * @brief Validate the configuration and return error message if invalid
     * 
     * @return std::string Empty string if valid, otherwise error description
     */
    virtual std::string Validate() const { return ""; }

   protected:
    AddressType node_address_;  ///< The node's address in the network
};

/**
 * @brief Configuration for the PingPong protocol
 * 
 * Contains specific parameters needed for the PingPong protocol operation.
 */
class PingPongProtocolConfig : public BaseProtocolConfig {
   public:
    /**
     * @brief Constructor with PingPong-specific parameters
     * 
     * @param node_address The node's address in the network (0 means auto-assign)
     * @param default_timeout Default timeout for ping requests in milliseconds
     * @param retry_count Number of retries for failed pings
     */
    explicit PingPongProtocolConfig(AddressType node_address = 0,
                                    uint32_t default_timeout = 2000,
                                    uint8_t retry_count = 3)
        : BaseProtocolConfig(node_address),
          default_timeout_(default_timeout),
          retry_count_(retry_count) {}

    /**
     * @brief Get the protocol type
     * 
     * @return protocols::ProtocolType Always returns kPingPong
     */
    protocols::ProtocolType getProtocolType() const override {
        return protocols::ProtocolType::kPingPong;
    }

    /**
     * @brief Get the default timeout for ping requests
     * 
     * @return uint32_t Timeout in milliseconds
     */
    uint32_t getDefaultTimeout() const { return default_timeout_; }

    /**
     * @brief Set the default timeout for ping requests
     * 
     * @param timeout Timeout in milliseconds
     */
    void setDefaultTimeout(uint32_t timeout) { default_timeout_ = timeout; }

    /**
     * @brief Get the number of retries for failed pings
     * 
     * @return uint8_t Number of retries
     */
    uint8_t getRetryCount() const { return retry_count_; }

    /**
     * @brief Set the number of retries for failed pings
     * 
     * @param count Number of retries
     */
    void setRetryCount(uint8_t count) { retry_count_ = count; }

    /**
     * @brief Check if configuration is valid
     * 
     * @return bool True if configuration is valid
     */
    bool IsValid() const override {
        return default_timeout_ >= 100 &&    // At least 100ms timeout
               default_timeout_ <= 30000 &&  // Maximum 30 seconds
               retry_count_ <= 10;           // Maximum 10 retries
    }

    /**
     * @brief Validate the configuration and return error message if invalid
     * 
     * @return std::string Empty string if valid, otherwise error description
     */
    std::string Validate() const override {
        if (default_timeout_ < 100) {
            return "Default timeout too short (minimum 100ms)";
        }
        if (default_timeout_ > 30000) {
            return "Default timeout too long (maximum 30s)";
        }
        if (retry_count_ > 10) {
            return "Too many retries (maximum 10)";
        }
        return "";
    }

   private:
    uint32_t default_timeout_;  ///< Default timeout for ping requests in ms
    uint8_t retry_count_;       ///< Number of retries for failed pings
};

/**
 * @brief Configuration for the LoRaMesh protocol
 * 
 * Contains specific parameters needed for the LoRaMesh routing protocol.
 */
class LoRaMeshProtocolConfig : public BaseProtocolConfig {
   public:
    /**
     * @brief Constructor with LoRaMesh-specific parameters
     * 
     * @param node_address The node's address in the network (0 means auto-assign)
     * @param hello_interval Interval between hello messages in milliseconds
     * @param route_timeout Time after which routes are considered stale in milliseconds
     * @param max_hops Maximum number of hops for message routing
     * @param max_packet_size Maximum packet size for messages
     * @param data_slots Number of data slots in the superframe
     * @param joining_timeout_ms Timeout for joining the network in milliseconds
     * @param max_network_nodes Maximum number of nodes in the network
     * @param guard_time_ms TX guard time for RX readiness in milliseconds
     */
    explicit LoRaMeshProtocolConfig(
        AddressType node_address = 0, uint32_t hello_interval = 60000,
        uint32_t route_timeout = 180000, uint8_t max_hops = 5,
        uint8_t max_packet_size = 255, uint8_t default_data_slots = 1,
        uint32_t joining_timeout_ms = 30000, uint8_t max_network_nodes = 50,
        uint32_t guard_time_ms = 50)
        : BaseProtocolConfig(node_address),
          hello_interval_(hello_interval),
          route_timeout_(route_timeout),
          max_hops_(max_hops),
          max_packet_size_(max_packet_size),
          default_data_slots_(default_data_slots),
          joining_timeout_ms_(joining_timeout_ms),
          max_network_nodes_(max_network_nodes),
          guard_time_ms_(guard_time_ms) {}

    /**
     * @brief Get the protocol type
     * 
     * @return protocols::ProtocolType Always returns kLoraMesh
     */
    protocols::ProtocolType getProtocolType() const override {
        return protocols::ProtocolType::kLoraMesh;
    }

    /**
     * @brief Get the hello message interval
     * 
     * @return uint32_t Interval in milliseconds
     */
    uint32_t getHelloInterval() const { return hello_interval_; }

    /**
     * @brief Set the hello message interval
     * 
     * @param interval Interval in milliseconds
     */
    void setHelloInterval(uint32_t interval) { hello_interval_ = interval; }

    /**
     * @brief Get the route timeout
     * 
     * @return uint32_t Timeout in milliseconds
     */
    uint32_t getRouteTimeout() const { return route_timeout_; }

    /**
     * @brief Set the route timeout
     * 
     * @param timeout Timeout in milliseconds
     */
    void setRouteTimeout(uint32_t timeout) { route_timeout_ = timeout; }

    /**
     * @brief Get the maximum number of hops
     * 
     * @return uint8_t Maximum hop count
     */
    uint8_t getMaxHops() const { return max_hops_; }

    /**
     * @brief Set the maximum number of hops
     * 
     * @param hops Maximum hop count
     */
    void setMaxHops(uint8_t hops) { max_hops_ = hops; }

    /**
     * @brief Get the maximum packet size
     * 
     * @return uint8_t Maximum packet size
     */
    uint8_t getMaxPacketSize() const { return max_packet_size_; }

    /**
     * @brief Set the maximum packet size
     * 
     * @param size Maximum packet size
     */
    void setMaxPacketSize(uint8_t size) { max_packet_size_ = size; }

    /**
     * @brief Get the default number of data slots in the superframe
     * 
     * @return uint8_t Number of data slots
     */
    uint8_t getDefaultDataSlots() const { return default_data_slots_; }

    /**
     * @brief Sets the default number of slots to request when joining
     * 
     * @param num_slots Number of slots to request
     */
    void setDefaultDataSlots(uint8_t num_slots) {
        default_data_slots_ = num_slots;
    }

    /**
     * @brief Gets the joining timeout in milliseconds
     * 
     * @return uint32_t Timeout in milliseconds
     */
    uint32_t getJoiningTimeout() const { return joining_timeout_ms_; }

    /**
     * @brief Sets the joining timeout in milliseconds
     * 
     * @param timeout_ms Timeout in milliseconds
     */
    void setJoiningTimeout(uint32_t timeout_ms) {
        joining_timeout_ms_ = timeout_ms;
    }

    /**
     * @brief Get the maximum number of nodes in the network
     * 
     * @return uint8_t Maximum number of nodes
     */
    uint8_t getMaxNetworkNodes() const { return max_network_nodes_; }

    /**
     * @brief Set the maximum number of nodes in the network
     * 
     * @param max_nodes Maximum number of nodes
     */
    void setMaxNetworkNodes(uint8_t max_nodes) {
        max_network_nodes_ = max_nodes;
    }

    /**
     * @brief Get the TX guard time for RX readiness
     * 
     * @return uint32_t Guard time in milliseconds
     */
    uint32_t getGuardTime() const { return guard_time_ms_; }

    /**
     * @brief Set the TX guard time for RX readiness
     * 
     * @param guard_time_ms Guard time in milliseconds
     */
    void setGuardTime(uint32_t guard_time_ms) {
        guard_time_ms_ = guard_time_ms;
    }

    /**
     * @brief Check if configuration is valid
     * 
     * @return bool True if configuration is valid
     */
    bool IsValid() const override {
        return hello_interval_ >= 5000 &&     // At least 5s interval
               hello_interval_ <= 3600000 &&  // Maximum 1 hour
               route_timeout_ >
                   hello_interval_ &&  // Route timeout must be greater than hello interval
               max_hops_ > 0 &&         // At least 1 hop
               max_hops_ <= 16 &&       // Maximum 16 hops
               guard_time_ms_ >= 10 &&  // At least 10ms guard time
               guard_time_ms_ <= 500;   // Maximum 500ms guard time
    }

    /**
     * @brief Validate the configuration and return error message if invalid
     * 
     * @return std::string Empty string if valid, otherwise error description
     */
    std::string Validate() const override {
        if (hello_interval_ < 5000) {
            return "Hello interval too short (minimum 5s)";
        }
        if (hello_interval_ > 3600000) {
            return "Hello interval too long (maximum 1h)";
        }
        if (route_timeout_ <= hello_interval_) {
            return "Route timeout must be greater than hello interval";
        }
        if (max_hops_ == 0) {
            return "Max hops must be at least 1";
        }
        if (max_hops_ > 16) {
            return "Max hops too large (maximum 16)";
        }
        if (guard_time_ms_ < 10) {
            return "Guard time too short (minimum 10ms)";
        }
        if (guard_time_ms_ > 500) {
            return "Guard time too long (maximum 500ms)";
        }
        return "";
    }

   private:
    uint32_t hello_interval_ =
        60000;  ///< Interval between hello messages in ms
    uint32_t route_timeout_ =
        60000;               ///< Time after which routes are considered stale
    uint8_t max_hops_ = 10;  ///< Maximum number of hops for routing
    uint8_t max_packet_size_ = 255;  ///< Maximum packet size
    uint8_t default_data_slots_ =
        1;  ///< Default Number of data slots in the superframe
    uint32_t joining_timeout_ms_ =
        hello_interval_ * 3;  ///< Joining timeout in ms
    uint8_t max_network_nodes_ =
        50;                        ///< Maximum number of nodes in the network
    uint32_t guard_time_ms_ = 50;  ///< TX guard time for RX readiness in ms
};

/**
 * @brief Main protocol configuration class
 * 
 * This class serves as a container for the active protocol configuration
 * and provides methods to access and modify it.
 */
class ProtocolConfig {
   public:
    /**
     * @brief Default constructor with PingPong protocol
     */
    ProtocolConfig()
        : protocol_type_(protocols::ProtocolType::kPingPong),
          config_(std::make_unique<PingPongProtocolConfig>()) {}

    /**
     * @brief Copy constructor
     * 
     * Creates a deep copy of the protocol configuration
     * 
     * @param other The ProtocolConfig to copy from
     */
    ProtocolConfig(const ProtocolConfig& other)
        : protocol_type_(other.protocol_type_) {
        // Deep copy the configuration based on protocol type
        if (other.config_) {
            if (protocol_type_ == protocols::ProtocolType::kPingPong) {
                const auto& ping_pong_config =
                    static_cast<const PingPongProtocolConfig&>(*other.config_);
                config_ =
                    std::make_unique<PingPongProtocolConfig>(ping_pong_config);
            } else if (protocol_type_ == protocols::ProtocolType::kLoraMesh) {
                const auto& lora_mesh_config =
                    static_cast<const LoRaMeshProtocolConfig&>(*other.config_);
                config_ =
                    std::make_unique<LoRaMeshProtocolConfig>(lora_mesh_config);
            } else {
                // Default to PingPong if unknown type
                config_ = std::make_unique<PingPongProtocolConfig>();
            }
        }
    }

    /**
     * @brief Copy assignment operator
     * 
     * Performs a deep copy of the protocol configuration
     * 
     * @param other The ProtocolConfig to copy from
     * @return Reference to this object after assignment
     */
    ProtocolConfig& operator=(const ProtocolConfig& other) {
        if (this != &other) {
            protocol_type_ = other.protocol_type_;

            // Deep copy the configuration based on protocol type
            if (other.config_) {
                if (protocol_type_ == protocols::ProtocolType::kPingPong) {
                    const auto& ping_pong_config =
                        static_cast<const PingPongProtocolConfig&>(
                            *other.config_);
                    config_ = std::make_unique<PingPongProtocolConfig>(
                        ping_pong_config);
                } else if (protocol_type_ ==
                           protocols::ProtocolType::kLoraMesh) {
                    const auto& lora_mesh_config =
                        static_cast<const LoRaMeshProtocolConfig&>(
                            *other.config_);
                    config_ = std::make_unique<LoRaMeshProtocolConfig>(
                        lora_mesh_config);
                } else {
                    // Default to PingPong if unknown type
                    config_ = std::make_unique<PingPongProtocolConfig>();
                }
            } else {
                config_.reset();
            }
        }
        return *this;
    }

    /**
     * @brief Move constructor
     * 
     * @param other The ProtocolConfig to move from
     */
    ProtocolConfig(ProtocolConfig&& other) noexcept
        : protocol_type_(other.protocol_type_),
          config_(std::move(other.config_)) {}

    /**
     * @brief Move assignment operator
     * 
     * @param other The ProtocolConfig to move from
     * @return Reference to this object after assignment
     */
    ProtocolConfig& operator=(ProtocolConfig&& other) noexcept {
        if (this != &other) {
            protocol_type_ = other.protocol_type_;
            config_ = std::move(other.config_);
        }
        return *this;
    }

    /**
     * @brief Constructor with a specific protocol configuration
     * 
     * @param config Unique pointer to a protocol configuration
     */
    explicit ProtocolConfig(std::unique_ptr<BaseProtocolConfig> config)
        : protocol_type_(config->getProtocolType()),
          config_(std::move(config)) {}

    /**
     * @brief Get the active protocol type
     * 
     * @return protocols::ProtocolType The active protocol type
     */
    protocols::ProtocolType getProtocolType() const { return protocol_type_; }

    /**
     * @brief Set the PingPong protocol configuration
     * 
     * @param config The PingPong protocol configuration
     */
    void setPingPongConfig(const PingPongProtocolConfig& config) {
        protocol_type_ = protocols::ProtocolType::kPingPong;
        config_ = std::make_unique<PingPongProtocolConfig>(config);
    }

    /**
     * @brief Set the LoRaMesh protocol configuration
     * 
     * @param config The LoRaMesh protocol configuration
     */
    void setLoRaMeshConfig(const LoRaMeshProtocolConfig& config) {
        protocol_type_ = protocols::ProtocolType::kLoraMesh;
        config_ = std::make_unique<LoRaMeshProtocolConfig>(config);
    }

    /**
     * @brief Get the active protocol configuration as PingPong config
     * 
     * @return const PingPongProtocolConfig& Reference to the PingPong config
     * @throws std::bad_cast if current config is not PingPong
     */
    const PingPongProtocolConfig& getPingPongConfig() const {
        if (protocol_type_ != protocols::ProtocolType::kPingPong) {
            throw std::bad_cast();
        }
        return static_cast<const PingPongProtocolConfig&>(*config_);
    }

    /**
     * @brief Get the active protocol configuration as LoRaMesh config
     * 
     * @return const LoRaMeshProtocolConfig& Reference to the LoRaMesh config
     * @throws std::bad_cast if current config is not LoRaMesh
     */
    const LoRaMeshProtocolConfig& getLoRaMeshConfig() const {
        if (protocol_type_ != protocols::ProtocolType::kLoraMesh) {
            throw std::bad_cast();
        }
        return static_cast<const LoRaMeshProtocolConfig&>(*config_);
    }

    /**
     * @brief Get the node address
     * 
     * @return AddressType The configured node address
     */
    AddressType getNodeAddress() const { return config_->getNodeAddress(); }

    /**
     * @brief Set the node address
     * 
     * @param address The node address to set
     */
    void setNodeAddress(AddressType address) {
        config_->setNodeAddress(address);
    }

    /**
     * @brief Check if configuration is valid
     * 
     * @return bool True if configuration is valid
     */
    bool IsValid() const { return config_ && config_->IsValid(); }

    /**
     * @brief Validate the configuration and return error message if invalid
     * 
     * @return std::string Empty string if valid, otherwise error description
     */
    std::string Validate() const {
        if (!config_) {
            return "No protocol configuration set";
        }
        return config_->Validate();
    }

    /**
     * @brief Create default protocol configuration
     * 
     * @return ProtocolConfig Default protocol configuration (PingPong)
     */
    static ProtocolConfig CreateDefault() { return ProtocolConfig(); }

   private:
    protocols::ProtocolType protocol_type_;  ///< Active protocol type
    std::unique_ptr<BaseProtocolConfig>
        config_;  ///< Protocol-specific configuration
};

}  // namespace loramesher