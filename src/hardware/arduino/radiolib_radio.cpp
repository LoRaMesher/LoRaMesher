#include "radiolib_radio.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include <queue>

namespace loramesher {
namespace radio {
namespace {

// Constants for internal queue management
constexpr size_t kMaxQueueSize = 32;
constexpr uint32_t kReceiveTimeout = 100;  // ms

// RadioLib error code to Result mapping
Result MapRadioLibError(int code) {
    if (code == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    switch (code) {
        case RADIOLIB_ERR_PACKET_TOO_LONG:
        case RADIOLIB_ERR_TX_TIMEOUT:
            return Result::Error(LoraMesherErrorCode::kBufferOverflow);
        case RADIOLIB_ERR_RX_TIMEOUT:
            return Result::Error(LoraMesherErrorCode::kTimeout);
        case RADIOLIB_ERR_CRC_MISMATCH:
            return Result::Error(LoraMesherErrorCode::kReceptionError);
        case RADIOLIB_ERR_INVALID_BANDWIDTH:
        case RADIOLIB_ERR_INVALID_SPREADING_FACTOR:
        case RADIOLIB_ERR_INVALID_CODING_RATE:
            return Result::Error(LoraMesherErrorCode::kInvalidParameter);
        case RADIOLIB_ERR_CHIP_NOT_FOUND:
            return Result::Error(LoraMesherErrorCode::kHardwareError);
        default:
            return Result::Error(LoraMesherErrorCode::kHardwareError);
    }
}

}  // namespace

RadioLibRadio::RadioLibRadio(int cs_pin, int di0_pin, int rst_pin,
                             SPIClass& spi)
    : cs_pin_(cs_pin),
      di0_pin_(di0_pin),
      rst_pin_(rst_pin),
      spi_(spi),
      current_module_(nullptr),
      receive_callback_(nullptr),
      receive_queue_(),
      radio_mutex_() {}

Result RadioLibRadio::Configure(const RadioConfig& config) {
    std::lock_guard<std::mutex> lock(radio_mutex_);

    // Create appropriate radio module based on type
    if (!CreateRadioModule(config.getRadioType())) {
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    // Begin radio operation
    int state = current_module_->Begin();
    if (state != RADIOLIB_ERR_NONE) {
        return MapRadioLibResult::Error(state);
    }

    // Configure radio parameters
    state = current_module_->setFrequency(config.getFrequency());
    if (state != RADIOLIB_ERR_NONE) {
        return MapRadioLibResult::Error(state);
    }

    state = current_module_->setSpreadingFactor(config.getSpreadingFactor());
    if (state != RADIOLIB_ERR_NONE) {
        return MapRadioLibResult::Error(state);
    }

    state = current_module_->setBandwidth(config.getBandwidth() * 1000);
    if (state != RADIOLIB_ERR_NONE) {
        return MapRadioLibResult::Error(state);
    }

    state = current_module_->setCodingRate(config.getCodingRate());
    if (state != RADIOLIB_ERR_NONE) {
        return MapRadioLibResult::Error(state);
    }

    state = current_module_->setOutputPower(config.getPower());
    if (state != RADIOLIB_ERR_NONE) {
        return MapRadioLibResult::Error(state);
    }

    // Set up interrupt handling
    current_module_->setDio0Action(std::bind(&RadioLibRadio::HandleInterrupt,
                                             this, std::placeholders::_1));

    return Result::Success();
}

Result RadioLibRadio::Send(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Transmit data
    int state = current_module_->transmit(data, len);
    return MapRadioLibResult::Error(state);
}

Result RadioLibRadio::StartReceive() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Start receive mode
    int state = current_module_->StartReceive();
    return MapRadioLibResult::Error(state);
}

Result RadioLibRadio::Sleep() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Enter sleep mode
    int state = current_module_->Sleep();
    return MapRadioLibResult::Error(state);
}

int8_t RadioLibRadio::getRSSI() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return -255;  // Invalid RSSI value
    }
    return static_cast<int8_t>(current_module_->getRSSI());
}

int8_t RadioLibRadio::getSNR() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return -128;  // Invalid SNR value
    }
    return static_cast<int8_t>(current_module_->getSNR());
}

void RadioLibRadio::setReceiveCallback(
    std::function<void(RadioEvent&)> callback) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    receive_callback_ = std::move(callback);

    // Process any queued messages
    ProcessQueuedMessages();
}

bool RadioLibRadio::CreateRadioModule(RadioType type) {
    // Clean up existing module
    current_module_.reset();

    // Create new module based on type
    switch (type) {
        case RadioType::kSx1276:
            current_module_ =
                std::make_unique<SX1276>(cs_pin_, di0_pin_, rst_pin_, spi_);
            break;
        case RadioType::kSx1278:
            current_module_ =
                std::make_unique<SX1278>(cs_pin_, di0_pin_, rst_pin_, spi_);
            break;
        // Add more radio types here
        default:
            return false;
    }

    return true;
}

void RadioLibRadio::HandleInterrupt(void* context) {
    if (!current_module_) {
        return;
    }

    // Check if we received data
    size_t length = current_module_->getPacketLength();
    if (length == 0) {
        return;
    }

    // Read the received data
    std::vector<uint8_t> buffer(length);
    int state = current_module_->readData(buffer.data(), length);

    if (state == RADIOLIB_ERR_NONE) {
        // Try to deserialize the received data into a message
        auto message = BaseMessage::deserialize(buffer);
        if (message) {
            std::lock_guard<std::mutex> lock(radio_mutex_);
            // Add to queue if there's space
            if (receive_queue_.size() < kMaxQueueSize) {
                auto event = CreateReceivedEvent(std::move(message), getRSSI(),
                                                 getSNR());
                receive_queue_.push(std::move(event));
            }
            // Process queued messages if we have a callback
            if (receive_callback_) {
                ProcessQueuedMessages();
            }
        }
    }

    // Restart receive mode
    current_module_->StartReceive();
}

void RadioLibRadio::ProcessQueuedMessages() {
    while (!receive_queue_.empty() && receive_callback_) {
        auto& event = receive_queue_.front();
        receive_callback_(*event);
        receive_queue_.pop();
    }
}

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO