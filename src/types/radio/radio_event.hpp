// src/radio/radio_event.hpp
#pragma once

#include <cstdint>
#include <memory>

#include "types/messages/message.hpp"

namespace loramesher {
namespace radio {

/**
 * @brief Event types that can be generated by the radio
 * 
 * Enumeration of all possible radio events that can occur during
 * normal operation of the LoRa radio module.
 */
enum class RadioEventType {
    kReceived,          ///< Message received
    kTransmitted,       ///< Message transmitted successfully
    kTimeout,           ///< Reception/transmission timeout
    kCrcError,          ///< CRC check failed
    kPreambleDetected,  ///< Preamble detected during reception
    kSyncWordValid,     ///< Valid sync word detected
    kHeaderValid,       ///< Valid header received
    kHeaderError,       ///< Header CRC error
    kNoise,             ///< Noise floor detected
    kCadDone,           ///< Channel activity detection completed
    kCadDetected,       ///< Channel activity detected
    kRxError,           ///< Reception error
    kTxError            ///< Transmission error
};

/**
 * @brief Class to encapsulate radio events and their associated messages
 * 
 * Represents radio events with associated metadata such as signal strength,
 * SNR, timestamp, and optional message payload. Provides methods for
 * accessing and manipulating this data.
 */
class RadioEvent {
   public:
    /**
     * @brief Constructor for events with message
     * 
     * @param type Type of radio event
     * @param message Unique pointer to the message associated with the event
     */
    RadioEvent(RadioEventType type, std::unique_ptr<BaseMessage> message)
        : type_(type), message_(std::move(message)) {}

    /**
     * @brief Constructor for events without message
     * 
     * @param type Type of radio event
     */
    explicit RadioEvent(RadioEventType type) : type_(type), message_(nullptr) {}

    /**
     * @brief Deleted copy constructor to avoid unintended message duplication
     */
    RadioEvent(const RadioEvent&) = delete;

    /**
     * @brief Deleted copy assignment to avoid unintended message duplication
     */
    RadioEvent& operator=(const RadioEvent&) = delete;

    /**
     * @brief Default move constructor
     */
    RadioEvent(RadioEvent&&) = default;

    /**
     * @brief Default move assignment operator
     */
    RadioEvent& operator=(RadioEvent&&) = default;

    /**
     * @brief Gets the event type
     * @return Type of radio event
     */
    RadioEventType getType() const { return type_; }

    /**
     * @brief Gets the associated message without transferring ownership
     * @return Pointer to the message, or nullptr if no message is associated
     */
    const BaseMessage* getMessage() const { return message_.get(); }

    /**
     * @brief Takes ownership of the message from the event
     * @return Unique pointer to the message, leaving the event's message as nullptr
     */
    std::unique_ptr<BaseMessage> TakeMessage() { return std::move(message_); }

    /**
     * @brief Sets the RSSI value
     * @param rssi Received Signal Strength Indicator value
     */
    void setRssi(int8_t rssi) { rssi_ = rssi; }

    /**
     * @brief Gets the RSSI value
     * @return Current RSSI value
     */
    int8_t getRssi() const { return rssi_; }

    /**
     * @brief Sets the SNR value
     * @param snr Signal-to-Noise Ratio value
     */
    void setSnr(int8_t snr) { snr_ = snr; }

    /**
     * @brief Gets the SNR value
     * @return Current SNR value
     */
    int8_t getSnr() const { return snr_; }

    /**
     * @brief Sets the event timestamp
     * @param timestamp Event occurrence time
     */
    void setTimestamp(uint32_t timestamp) { timestamp_ = timestamp; }

    /**
     * @brief Gets the event timestamp
     * @return Current timestamp value
     */
    uint32_t getTimestamp() const { return timestamp_; }

    /**
     * @brief Checks if event has a valid message
     * @return True if event has a message, false otherwise
     */
    bool HasMessage() const { return message_ != nullptr; }

    /**
     * @brief Converts event type to string representation
     * 
     * Utility method for logging and debugging purposes.
     * 
     * @param type Radio event type to convert
     * @return String representation of the event type
     */
    static const char* EventTypeToString(RadioEventType type) {
        switch (type) {
            case RadioEventType::kReceived:
                return "Received";
            case RadioEventType::kTransmitted:
                return "Transmitted";
            case RadioEventType::kTimeout:
                return "Timeout";
            case RadioEventType::kCrcError:
                return "CRC Error";
            case RadioEventType::kPreambleDetected:
                return "Preamble Detected";
            case RadioEventType::kSyncWordValid:
                return "Sync Word Valid";
            case RadioEventType::kHeaderValid:
                return "Header Valid";
            case RadioEventType::kHeaderError:
                return "Header Error";
            case RadioEventType::kNoise:
                return "Noise Detected";
            case RadioEventType::kCadDone:
                return "CAD Done";
            case RadioEventType::kCadDetected:
                return "CAD Detected";
            case RadioEventType::kRxError:
                return "Reception Error";
            case RadioEventType::kTxError:
                return "Transmission Error";
            default:
                return "Unknown Event";
        }
    }

   private:
    RadioEventType type_;  ///< Type of radio event
    std::unique_ptr<BaseMessage>
        message_;             ///< Optional message associated with event
    int8_t rssi_ = 0;         ///< Received Signal Strength Indicator
    int8_t snr_ = 0;          ///< Signal-to-Noise Ratio
    uint32_t timestamp_ = 0;  ///< Event timestamp
};

/**
 * @brief Factory function for creating received message events
 * 
 * Creates a RadioEvent with type kReceived and initializes RSSI and SNR values.
 * 
 * @param message Unique pointer to the received message
 * @param rssi Received Signal Strength Indicator value
 * @param snr Signal-to-Noise Ratio value
 * @return Unique pointer to the created RadioEvent
 */
inline std::unique_ptr<RadioEvent> CreateReceivedEvent(
    std::unique_ptr<BaseMessage> message, int8_t rssi, int8_t snr) {
    auto event = std::make_unique<RadioEvent>(RadioEventType::kReceived,
                                              std::move(message));
    event->setRssi(rssi);
    event->setSnr(snr);
    // TODO: event->setTimestamp(/* Get current timestamp */);
    return event;
}

/**
 * @brief Factory function for creating transmitted message events
 * 
 * Creates a RadioEvent with type kTransmitted.
 * 
 * @param message Unique pointer to the transmitted message
 * @return Unique pointer to the created RadioEvent
 */
inline std::unique_ptr<RadioEvent> CreateTransmittedEvent(
    std::unique_ptr<BaseMessage> message) {
    auto event = std::make_unique<RadioEvent>(RadioEventType::kTransmitted,
                                              std::move(message));
    // event->setTimestamp(/* Get current timestamp */);
    return event;
}

/**
 * @brief Factory function for creating timeout events
 * 
 * Creates a RadioEvent with type kTimeout.
 * 
 * @return Unique pointer to the created RadioEvent
 */
inline std::unique_ptr<RadioEvent> CreateTimeoutEvent() {
    auto event = std::make_unique<RadioEvent>(RadioEventType::kTimeout);
    // TODO: event->setTimestamp(/* Get current timestamp */);
    return event;
}

}  // namespace radio
}  // namespace loramesher