#include "radiolib_radio.hpp"

#include <queue>

#include "config/task_config.hpp"
#include "radiolib_modules/radio_lib_code_errors.hpp"
#include "utils/task_monitor.hpp"

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
    constexpr size_t kQueueSize = 10;  //TODO: Determine through testing
    receive_queue_ = GetRTOS().CreateQueue(kQueueSize, sizeof(uint8_t));

    if (!receive_queue_) {
        return Result::Error(LoraMesherErrorCode::kMemoryError);
    }

    // Create processing task with monitored configuration
    bool task_created = GetRTOS().CreateTask(
        ProcessEvents, config.getRadioTypeString().c_str(),
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

    // Begin radio operation
    Result result = current_module_->Begin(config);
    if (!result) {
        return result;
    }

    // Configure radio parameters
    result = current_module_->setFrequency(config.getFrequency());
    if (!result) {
        return result;
    }

    result = current_module_->setSpreadingFactor(config.getSpreadingFactor());
    if (!result) {
        return result;
    }

    result = current_module_->setBandwidth(config.getBandwidth() * 1000);
    if (!result) {
        return result;
    }

    result = current_module_->setCodingRate(config.getCodingRate());
    if (!result) {
        return result;
    }

    result = current_module_->setPower(config.getPower());
    if (!result) {
        return result;
    }

    // Set up interrupt handling
    current_module_->setActionReceive(HandleInterruptStatic);

    return Result::Success();
}

Result RadioLibRadio::Send(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Check if radio is already transmitting
    if (current_state_ == RadioState::kTransmit) {
        return Result::Error(LoraMesherErrorCode::kBusyError);
    }

    // Attempt to transmit data
    Result status = current_module_->Send(data, len);
    if (status) {
        current_state_ = RadioState::kTransmit;
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);

    // Transmit data
    int state = current_module_->Send(data, len);
    return RadioLibCodeErrors::ConvertStatus(state);
}

Result RadioLibRadio::StartReceive() {
    std::lock_guard<std::mutex> lock(radio_mutex_);
    if (!current_module_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Check if already in receive mode
    if (current_state_ == RadioState::kReceive) {
        return Result::Success();
    }

    // We might need to suspend the processing task temporarily
    if (processing_task_) {
        GetRTOS().SuspendTask(processing_task_);
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
    }

    // If we failed, still resume the task
    if (processing_task_) {
        GetRTOS().ResumeTask(processing_task_);
    }
    return status;
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
    return last_packet_rssi_;
}

int8_t RadioLibRadio::getLastPacketSNR() {
    return last_packet_snr_;
}

bool RadioLibRadio::IsTransmitting() {
    return current_state_ == RadioState::kTransmit;
}

float RadioLibRadio::getFrequency() {
    return current_config_.getFrequency();
}

uint8_t RadioLibRadio::getSpreadingFactor() {
    return current_config_.getSpreadingFactor();
}

float RadioLibRadio::getBandwidth() {
    return current_config_.getBandwidth();
}

uint8_t RadioLibRadio::getCodingRate() {
    return current_config_.getCodingRate();
}

uint8_t RadioLibRadio::getPower() {
    return current_config_.getPower();
}

Result RadioLibRadio::setFrequency(float frequency) {
    current_config_.setFrequency(frequency);
    return current_module_->setFrequency(frequency);
}

Result RadioLibRadio::setSpreadingFactor(uint8_t sf) {
    current_config_.setSpreadingFactor(sf);
    return current_module_->setSpreadingFactor(sf);
}

Result RadioLibRadio::setBandwidth(float bandwidth) {
    current_config_.setBandwidth(bandwidth);
    return current_module_->setBandwidth(bandwidth);
}

Result RadioLibRadio::setCodingRate(uint8_t coding_rate) {
    current_config_.setCodingRate(coding_rate);
    return current_module_->setCodingRate(coding_rate);
}

Result RadioLibRadio::setPower(uint8_t power) {
    current_config_.setPower(power);
    return current_module_->setPower(power);
}

Result RadioLibRadio::setSyncWord(uint8_t sync_word) {
    Result result = current_config_.setSyncWord(sync_word);
    if (!result) {
        return result;
    }
    return current_module_->setSyncWord(sync_word);
}

Result RadioLibRadio::setCRC(bool enable) {
    Result result = current_config_.setCRC(enable);
    if (!result) {
        return result;
    }
    return current_module_->setCRC(enable);
}

Result RadioLibRadio::setPreambleLength(uint16_t length) {
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
            current_module_ = std::make_unique<LoraMesherSX1276>(
                cs_pin_, di0_pin_, rst_pin_, busy_pin_);
            break;
        // case RadioType::kSx1278:
        //     current_module_ =
        //         std::make_unique<SX1278>(cs_pin_, di0_pin_, rst_pin_, spi_);
        //     break;
        // Add more radio types here
        default:
            return false;
    }

    return true;
}

ISR_ATTR RadioLibRadio::HandleInterruptStatic() {
    if (instance_) {
        uint8_t event = 1;
        GetRTOS().SendToQueueISR(instance_->receive_queue_, &event);
    }
}

void RadioLibRadio::HandleInterrupt() {
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
    Result result = current_module_->readData(buffer.data(), length);
    if (result) {
        // Update last packet info
        last_packet_rssi_ = current_module_->getRSSI();
        last_packet_snr_ = current_module_->getSNR();

        // Try to deserialize the received data into a message
        auto message_optional = BaseMessage::CreateFromSerialized(buffer);
        if (message_optional) {
            std::lock_guard<std::mutex> lock(radio_mutex_);
            // Add to queue if there's space
            if (GetRTOS().getQueueMessagesWaiting(receive_queue_) <
                kMaxQueueSize) {
                auto event = CreateReceivedEvent(
                    std::make_unique<BaseMessage>(std::move(*message_optional)),
                    last_packet_rssi_, last_packet_snr_);
                if (event && receive_callback_) {
                    receive_callback_(std::move(event));
                }
            }
        }
    } else {
        // Log error
    }

    // Restart receive mode
    current_module_->StartReceive();
}

void RadioLibRadio::ProcessEvents(void* parameters) {
    auto radio = static_cast<RadioLibRadio*>(parameters);
    uint8_t event;

    while (true) {
        if (GetRTOS().ReceiveFromQueue(radio->receive_queue_, &event,
                                       MAX_DELAY) == os::QueueResult::kOk) {
            // Periodic monitoring
            utils::TaskMonitor::MonitorTask(
                radio->processing_task_,
                radio->current_config_.getRadioTypeString().c_str(),
                config::TaskConfig::kMinStackWatermark);

            radio->HandleInterrupt();
        } else {
            // TODO: Handle error/timeout
        }
    }
}

}  // namespace radio
}  // namespace loramesher