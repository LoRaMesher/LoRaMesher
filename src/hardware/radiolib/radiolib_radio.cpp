#include "radiolib_radio.hpp"

#include <queue>

#include "config/system_config.hpp"
#include "config/task_config.hpp"
#include "radiolib_modules/radio_lib_code_errors.hpp"
#include "utils/compat/span.hpp"
#include "utils/logger.hpp"
#include "utils/task_monitor.hpp"

#ifdef DEBUG
#include "mocks/mock_radio.hpp"
#endif  // DEBUG

#include "radiolib_modules/sx1262.hpp"
#include "radiolib_modules/sx1276.hpp"

namespace loramesher {
namespace radio {

// Initialize static member
RadioLibRadio* RadioLibRadio::instance_ = nullptr;

// Constants for internal queue management
constexpr size_t kMaxQueueSize = 32;

RadioLibRadio::RadioLibRadio(int cs_pin, int di0_pin, int rst_pin, int busy_pin,
                             SPIClass& spi)
    : cs_pin_(cs_pin),
      di0_pin_(di0_pin),
      rst_pin_(rst_pin),
      busy_pin_(busy_pin),
      spi_(spi),
      current_module_(nullptr),
      receive_callback_(nullptr),
      receive_queue_(),
      radio_mutex_() {
    instance_ = this;
}

RadioLibRadio::~RadioLibRadio() {
    // Stop any processing tasks first
    if (processing_task_ != nullptr) {
        GetRTOS().DeleteTask(processing_task_);
        processing_task_ = nullptr;
    }

    // Delete queues (with mutex protection)
    {
        std::lock_guard<std::mutex> lock(radio_mutex_);

        if (receive_queue_ != nullptr) {
            GetRTOS().DeleteQueue(receive_queue_);
            receive_queue_ = nullptr;
        }
    }

    // Note: Sleep() not called in destructor to avoid virtual function call during destruction
}

Result RadioLibRadio::Configure(const RadioConfig& config) {
    std::lock_guard<std::mutex> lock(radio_mutex_);

    // Create event queue with size based on analysis
    receive_queue_ = GetRTOS().CreateQueue(kMaxQueueSize, sizeof(uint8_t));

    if (!receive_queue_) {
        return Result::Error(LoraMesherErrorCode::kMemoryError);
    }

    std::string taskName = config.getRadioTypeString();

    // Create processing task with monitored configuration
    bool task_created = GetRTOS().CreateTask(
        ProcessEvents, taskName.c_str(),
        config::TaskConfig::kRadioEventStackSize / 4,  // FreeRTOS uses words
        this, config::TaskPriorities::kRadioEventPriority, &processing_task_);

    if (!task_created) {
        GetRTOS().DeleteQueue(receive_queue_);
        return Result::Error(LoraMesherErrorCode::kMemoryError);
    }

    if (processing_task_ == nullptr) {
        return Result(LoraMesherErrorCode::kUnknownError,
                      "Processing task not created");
    }

    // Create appropriate radio module based on type
    if (!CreateRadioModule(config.getRadioType())) {
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    // Copy the configuration
    current_config_ = config;

    // Suspend the processing task until we are fully configured
    GetRTOS().SuspendTask(processing_task_);

    LOG_DEBUG("RadioLib configurations set");

    return Result::Success();
}

Result RadioLibRadio::Begin(const RadioConfig& config) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Copy the configuration
    current_config_ = config;

    LOG_DEBUG("Begin radio operation");

    // Begin radio operation
    Result result = current_module_->Begin(current_config_);
    if (!result) {
        return result;
    }

    // Cache ToA while the radio is awake so getTimeOnAir() never
    // touches SPI during sleep (avoids accidental SX1262 wake-ups).
    for (size_t i = 0; i < toa_cache_ms_.size(); ++i) {
        toa_cache_ms_[i] =
            current_module_->getTimeOnAir(static_cast<uint8_t>(i));
    }

    return Result::Success();
}

Result RadioLibRadio::Send(const uint8_t* data, size_t len) {
    Result status = Result::Success();

    {
        std::lock_guard<std::mutex> lock(radio_mutex_);

        if (!current_module_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }

        // Check if radio is already transmitting
        if (current_state_ == RadioState::kTransmit) {
            return Result::Error(LoraMesherErrorCode::kBusyError);
        }

        status = current_module_->ClearActionReceive();
        if (!status) {
            return status;
        }

        // Attempt to transmit data
        status = current_module_->Send(data, len);
        if (status) {
            current_state_ = RadioState::kTransmit;
        }
    }

    // TODO: What to do here?
    // Result start_receive_result = StartReceive();
    // status.MergeErrors(start_receive_result);
    Result start_receive_result = Sleep();
    status.MergeErrors(start_receive_result);

    return status;
}

Result RadioLibRadio::StartReceive() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Check if already in receive mode
    if (current_state_ == RadioState::kReceive) {
        // LOG_DEBUG("Already receiving messages");
        return Result::Success();
    }

    // We might need to suspend the processing task temporarily
    if (processing_task_) {
        GetRTOS().SuspendTask(processing_task_);
    }

    // SX1262 can enter a corrupt state during sleep→active transitions
    // (RadioLib issues #575, #1207). Reset its state machine before
    // configuring DIO1.
    if (current_state_ == RadioState::kSleep) {
        current_module_->Standby();
    }

    if (current_state_ != RadioState::kIdle) {
        Result result = current_module_->ClearActionReceive();
        if (!result) {
            return result;
        }
    }

    Result result = current_module_->setActionReceive(HandleInterruptStatic);
    if (!result) {
        return result;
    }

    // Start continuous receive mode
    Result status = current_module_->StartReceive();
    if (status) {
        current_state_ = RadioState::kReceive;
        // Resume the processing task
        if (processing_task_) {
            GetRTOS().ResumeTask(processing_task_);
        }
        return Result::Success();
    } else {
        LOG_ERROR("Start receiving failed");
    }

    // If we failed, still resume the task
    if (processing_task_) {
        GetRTOS().ResumeTask(processing_task_);
    }

    return result;
}

Result RadioLibRadio::Sleep() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    if (current_state_ == RadioState::kSleep) {
        LOG_DEBUG("Already Sleeping");
        return Result::Success();
    }

    // Suspend the processing task before sleep
    if (processing_task_) {
        GetRTOS().SuspendTask(processing_task_);
    }

    Result status = current_module_->Sleep();
    if (status) {
        current_state_ = RadioState::kSleep;
        // Note: We don't resume the task in sleep mode
        return Result::Success();
    }

    // If sleep failed, resume the task
    if (processing_task_) {
        GetRTOS().ResumeTask(processing_task_);
    }

    return status;
}

RadioState RadioLibRadio::getState() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    return current_state_;
}

float RadioLibRadio::getRSSI() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return -255.0f;
    }
    return current_module_->getRSSI();
}

float RadioLibRadio::getSNR() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return -128.0f;
    }
    return current_module_->getSNR();
}

float RadioLibRadio::getLastPacketRSSI() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    return last_packet_rssi_;
}

float RadioLibRadio::getLastPacketSNR() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    return last_packet_snr_;
}

bool RadioLibRadio::IsTransmitting() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    return current_state_ == RadioState::kTransmit;
}

float RadioLibRadio::getFrequency() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    return current_config_.getFrequency();
}

uint8_t RadioLibRadio::getSpreadingFactor() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    return current_config_.getSpreadingFactor();
}

float RadioLibRadio::getBandwidth() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    return current_config_.getBandwidth();
}

uint8_t RadioLibRadio::getCodingRate() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    return current_config_.getCodingRate();
}

uint8_t RadioLibRadio::getPower() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    return current_config_.getPower();
}

uint32_t RadioLibRadio::getTimeOnAir(uint8_t length) {
    // Return cached value to avoid SPI access while the radio may be sleeping.
    // The cache is populated once in Begin() when the radio is awake.
    return toa_cache_ms_[length];
}

Result RadioLibRadio::setFrequency(float frequency) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    current_config_.setFrequency(frequency);
    return current_module_->setFrequency(frequency);
}

Result RadioLibRadio::setSpreadingFactor(uint8_t sf) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    current_config_.setSpreadingFactor(sf);
    return current_module_->setSpreadingFactor(sf);
}

Result RadioLibRadio::setBandwidth(float bandwidth) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    current_config_.setBandwidth(bandwidth);
    return current_module_->setBandwidth(bandwidth);
}

Result RadioLibRadio::setCodingRate(uint8_t coding_rate) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    current_config_.setCodingRate(coding_rate);
    return current_module_->setCodingRate(coding_rate);
}

Result RadioLibRadio::setPower(int8_t power) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    current_config_.setPower(power);
    return current_module_->setPower(power);
}

Result RadioLibRadio::setSyncWord(uint8_t sync_word) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    Result result = current_config_.setSyncWord(sync_word);
    if (!result) {
        return result;
    }
    return current_module_->setSyncWord(sync_word);
}

Result RadioLibRadio::setCRC(bool enable) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    Result result = current_config_.setCRC(enable);
    if (!result) {
        return result;
    }
    return current_module_->setCRC(enable);
}

Result RadioLibRadio::setPreambleLength(uint16_t length) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    Result result = current_config_.setPreambleLength(length);
    if (!result) {
        return result;
    }
    return current_module_->setPreambleLength(length);
}

Result RadioLibRadio::setCurrentLimit(float current_limit_ma) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    current_config_.setCurrentLimit(current_limit_ma);
    return current_module_->setCurrentLimit(current_limit_ma);
}

Result RadioLibRadio::setActionReceive(
    std::function<void(std::unique_ptr<RadioEvent>)> callback) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    receive_callback_ = std::move(callback);

    return Result::Success();
}

Result RadioLibRadio::setState(RadioState state) {
    switch (state) {
        case RadioState::kReceive:
            return StartReceive();
        case RadioState::kSleep:
            return Sleep();
        case RadioState::kIdle: {
            return Sleep();
        }
        default:
            return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }
}

bool RadioLibRadio::CreateRadioModule(RadioType type) {
    // Create new module based on type
    switch (type) {
        case RadioType::kSx1276:
#ifdef LORAMESHER_BUILD_ARDUINO
            current_module_ = std::make_unique<LoraMesherSX1276>(
                cs_pin_, di0_pin_, rst_pin_, busy_pin_, spi_);
            break;
#endif  // LORAMESHER_BUILD_ARDUINO
        case RadioType::kSx1262:
#ifdef LORAMESHER_BUILD_ARDUINO
            current_module_ = std::make_unique<LoraMesherSX1262>(
                cs_pin_, di0_pin_, rst_pin_, busy_pin_, spi_);
            break;
#endif  // LORAMESHER_BUILD_ARDUINO
#ifdef DEBUG
        case RadioType::kMockRadio:
            current_module_ = std::make_unique<MockRadio>();
            break;
#endif  // DEBUG
        default:
            LOG_ERROR("Unsupported radio type");
            return false;
    }

    if (!current_module_) {
        LOG_ERROR("Failed to create radio module");
        return false;
    }

    return true;
}

ISR_ATTR RadioLibRadio::HandleInterruptStatic() {
    if (instance_ && instance_->receive_queue_) {
        GetRTOS().NotifyTaskFromISR(instance_->processing_task_);
    }
}

void RadioLibRadio::HandleInterrupt() {
    std::unique_lock<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        LOG_ERROR("No radio module initialized");
        lock.unlock();
        return;
    }

    // Check if we received data
    uint8_t length = current_module_->getPacketLength();
    if (length == 0) {
        LOG_DEBUG("No data received");
        current_module_->StartReceive();
        lock.unlock();
        return;
    }

    Result result = current_module_->readData(rx_buffer_, length);
    if (!result) {
        LOG_WARNING(result.GetErrorMessage().c_str());
        current_module_->StartReceive();
        lock.unlock();
        return;
    }

    // LOG_DEBUG("Received data with length %d", length);

    // // Print buffer contents for debugging
    // if (length > 0) {
    //     std::string hex_data;
    //     char hex_byte[4];  // Extra space for the format
    //     for (size_t i = 0; i < length; i++) {
    //         snprintf(hex_byte, sizeof(hex_byte), "%02X ", buffer[i]);
    //         hex_data += hex_byte;
    //     }
    //     LOG_DEBUG("Buffer data (hex): %s", hex_data.c_str());
    // }

    // Update last packet info
    last_packet_rssi_ = current_module_->getRSSI();
    last_packet_snr_ = current_module_->getSNR();

    // Try to deserialize the received data into a message (zero-copy span)
    std::optional<BaseMessage> message_optional =
        BaseMessage::CreateFromSerialized(
            std::span<const uint8_t>(rx_buffer_, length));
    if (!message_optional) {
        LOG_ERROR("Failed to deserialize message");
        current_module_->StartReceive();
        lock.unlock();
        return;
    }

    LOG_INFO(
        "PKT_RX src=0x%04X dst=0x%04X type=0x%02X size=%u rssi=%.1f snr=%.1f",
        static_cast<unsigned>(message_optional->GetHeader().GetSource()),
        static_cast<unsigned>(message_optional->GetHeader().GetDestination()),
        static_cast<int>(message_optional->GetHeader().GetType()),
        static_cast<unsigned>(length), static_cast<double>(last_packet_rssi_),
        static_cast<double>(last_packet_snr_));

    // Add to queue if there's space
    if (GetRTOS().getQueueMessagesWaiting(receive_queue_) >= kMaxQueueSize) {
        LOG_ERROR("Receive queue full");
        current_module_->StartReceive();
        lock.unlock();
        return;
    }

    auto event = CreateReceivedEvent(
        std::make_unique<BaseMessage>(std::move(*message_optional)),
        last_packet_rssi_, last_packet_snr_);
    if (event && receive_callback_) {
        receive_callback_(std::move(event));
    }

    // Restart receive mode under lock to prevent SuspendTask() in Sleep()
    // from freezing this task mid-SPI-transfer (SPI bus mutex deadlock)
    current_module_->StartReceive();
    lock.unlock();
}

void RadioLibRadio::ProcessEvents(void* parameters) {
    auto* radio = static_cast<RadioLibRadio*>(parameters);
    if (!radio || !radio->receive_queue_) {
        GetRTOS().DeleteTask(nullptr);
        return;
    }

    char address_str[8];
    snprintf(address_str, sizeof(address_str), "0x%04X", radio->local_address_);
    GetRTOS().SetCurrentTaskNodeAddress(address_str);

    // LOG_DEBUG("Processing events for radio %p", static_cast<void*>(radio));
    while (!GetRTOS().ShouldStopOrPause() && radio->processing_task_) {
        os::QueueResult result = GetRTOS().WaitForNotify(MAX_DELAY);
        if (result == os::QueueResult::kOk) {
            radio->HandleInterrupt();
        }

        GetRTOS().YieldTask();
    }

    LOG_DEBUG("RadioLibRadio event processing task ending");
}

}  // namespace radio
}  // namespace loramesher