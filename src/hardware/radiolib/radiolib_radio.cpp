#include "radiolib_radio.hpp"

#include <queue>

#include "config/system_config.hpp"
#include "config/task_config.hpp"
#include "radiolib_modules/radio_lib_code_errors.hpp"
#include "utils/logger.hpp"
#include "utils/task_monitor.hpp"

#ifdef DEBUG
#include "mocks/mock_radio.hpp"
#endif  // DEBUG

#include "radiolib_modules/sx1276.hpp"

namespace loramesher {
namespace radio {

// Initialize static member
RadioLibRadio* RadioLibRadio::instance_ = nullptr;

// Constants for internal queue management
constexpr size_t kMaxQueueSize = 32;
constexpr uint32_t kReceiveTimeout = 100;  // ms

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

    Sleep();
}

Result RadioLibRadio::Configure(const RadioConfig& config) {
    std::lock_guard<std::mutex> lock(radio_mutex_);

    // Create event queue with size based on analysis
    receive_queue_ = GetRTOS().CreateQueue(kMaxQueueSize, sizeof(uint8_t));

    if (!receive_queue_) {
        return Result::Error(LoraMesherErrorCode::kMemoryError);
    }

    const char* taskName = config.getRadioTypeString().c_str();

    // Create processing task with monitored configuration
    bool task_created = GetRTOS().CreateTask(
        ProcessEvents, taskName,
        config::TaskConfig::kRadioEventStackSize / 4,  // FreeRTOS uses words
        this, config::TaskPriorities::kRadioEventPriority, &processing_task_);

    if (!task_created) {
        GetRTOS().DeleteQueue(receive_queue_);
        return Result::Error(LoraMesherErrorCode::kMemoryError);
    }

    // Create appropriate radio module based on type
    if (!CreateRadioModule(config.getRadioType())) {
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    // Copy the configuration
    current_config_ = config;

    // Wait until receive task is suspended
    GetRTOS().SuspendTask(processing_task_);

    LOG_DEBUG("Configurations set");

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
    return current_module_->Begin(current_config_);
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

    Result start_receive_result = StartReceive();
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
        LOG_DEBUG("Already receiving messages");
        return Result::Success();
    }

    // We might need to suspend the processing task temporarily
    if (processing_task_) {
        GetRTOS().SuspendTask(processing_task_);
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

int8_t RadioLibRadio::getRSSI() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return -128;  // Invalid RSSI value
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

int8_t RadioLibRadio::getLastPacketRSSI() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    return last_packet_rssi_;
}

int8_t RadioLibRadio::getLastPacketSNR() {
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
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return 0;
    }

    return current_module_->getTimeOnAir(length);
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
    // TODO: Clean up existing module
    // current_module_.reset();

    // Create new module based on type
    switch (type) {
        case RadioType::kSx1276:
#ifdef LORAMESHER_BUILD_ARDUINO
            current_module_ = std::make_unique<LoraMesherSX1276>(
                cs_pin_, di0_pin_, rst_pin_, busy_pin_);
            break;
#endif  // LORAMESHER_BUILD_ARDUINO
#ifdef DEBUG
        case RadioType::kMockRadio:
            current_module_ = std::make_unique<MockRadio>();
            break;
#endif  // DEBUG
        default:
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
        LOG_DEBUG("RadioLibRadio ISR: Notify task");
        GetRTOS().NotifyTaskFromISR(instance_->processing_task_);
    }
}

void RadioLibRadio::HandleInterrupt() {
    LOG_DEBUG("Handling interrupt");
    std::unique_lock<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        LOG_ERROR("No radio module initialized");
        lock.unlock();
        current_module_->StartReceive();
        return;
    }

    // Check if we received data
    size_t length = current_module_->getPacketLength();
    if (length == 0) {
        LOG_DEBUG("No data received");
        lock.unlock();
        current_module_->StartReceive();
        return;
    }

    // Read the received data
    std::vector<uint8_t> buffer(length);
    Result result = current_module_->readData(buffer.data(), length);
    if (!result) {
        LOG_WARNING(result.GetErrorMessage().c_str());
        lock.unlock();
        current_module_->StartReceive();
        return;
    }

    LOG_DEBUG("Received data with length %d", length);

    // Print buffer contents for debugging
    if (length > 0) {
        std::string hex_data;
        char hex_byte[4];  // Extra space for the format
        for (size_t i = 0; i < length; i++) {
            snprintf(hex_byte, sizeof(hex_byte), "%02X ", buffer[i]);
            hex_data += hex_byte;
        }
        LOG_DEBUG("Buffer data (hex): %s", hex_data.c_str());
    }

    // Update last packet info
    last_packet_rssi_ = current_module_->getRSSI();
    last_packet_snr_ = current_module_->getSNR();

    // Try to deserialize the received data into a message
    std::optional<BaseMessage> message_optional =
        BaseMessage::CreateFromSerialized(buffer);
    if (!message_optional) {
        LOG_ERROR("Failed to deserialize message");
        lock.unlock();
        current_module_->StartReceive();
        return;
    }

    LOG_DEBUG("Received message");
    // Add to queue if there's space
    if (GetRTOS().getQueueMessagesWaiting(receive_queue_) >= kMaxQueueSize) {
        LOG_ERROR("Receive queue full");
        lock.unlock();
        current_module_->StartReceive();
        return;
    }

    LOG_DEBUG("Adding message to queue");

    auto event = CreateReceivedEvent(
        std::make_unique<BaseMessage>(message_optional.value()),
        last_packet_rssi_, last_packet_snr_);
    if (event && receive_callback_) {
        LOG_DEBUG("Calling receive callback");
        receive_callback_(std::move(event));
    }

    lock.unlock();
    // Restart receive mode
    current_module_->StartReceive();
}

void RadioLibRadio::ProcessEvents(void* parameters) {
    auto* radio = static_cast<RadioLibRadio*>(parameters);
    if (!radio || !radio->receive_queue_) {
        GetRTOS().DeleteTask(nullptr);
        return;
    }

    LOG_DEBUG("Processing events");
    while (true) {
        if (GetRTOS().ShouldStopOrPause()) {
            break;
        }

        os::QueueResult result = GetRTOS().WaitForNotify(MAX_DELAY);
        LOG_DEBUG("Current State %d", radio->current_state_);
        if (result == os::QueueResult::kOk) {
            const char* taskName =
                radio->current_config_.getRadioTypeString().c_str();
            // Periodic monitoring
            utils::TaskMonitor::MonitorTask(
                radio->processing_task_, taskName,
                config::TaskConfig::kMinStackWatermark);

            LOG_DEBUG("Notification received");
            radio->HandleInterrupt();
            // Periodic monitoring
            utils::TaskMonitor::MonitorTask(
                radio->processing_task_, taskName,
                config::TaskConfig::kMinStackWatermark);
        } else {
            LOG_DEBUG("Notification timeout");
        }
        LOG_DEBUG("Finished processing event");
        GetRTOS().YieldTask();
    }
}

}  // namespace radio
}  // namespace loramesher