#include "sx1276.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include <functional>

#include "config/task_config.hpp"
#include "radio_lib_code_errors.hpp"
#include "utils/task_monitor.hpp"

namespace loramesher {
namespace radio {

// Static task tag for FreeRTOS
static const char* kTask_Tag = "SX1276_Task";

// Initialize static member
LoraMesherSX1276* LoraMesherSX1276::instance_ = nullptr;

LoraMesherSX1276::LoraMesherSX1276(int8_t cs_pin, int8_t irq_pin,
                                   int8_t reset_pin, int8_t busy_pin)
    : cs_pin_(cs_pin),
      irq_pin_(irq_pin),
      reset_pin_(reset_pin),
      busy_pin_(busy_pin) {}

LoraMesherSX1276::~LoraMesherSX1276() {
    // Clean up FreeRTOS resources
    if (processing_task_) {
        vTaskDelete(processing_task_);
    }
    if (event_queue_) {
        vQueueDelete(event_queue_);
    }

    sleep();
}

Result LoraMesherSX1276::initializeHardware() {
    // Create event queue with size based on analysis
    constexpr size_t kQueueSize = 10;  //TODO: Determine through testing
    event_queue_ = xQueueCreate(kQueueSize, sizeof(uint8_t));

    if (!event_queue_) {
        return Result::error(LoraMesherErrorCode::kMemoryError);
    }

    // Create processing task with monitored configuration
    BaseType_t task_created = xTaskCreate(
        processEvents, kTask_Tag,
        config::TaskConfig::kRadioEventStackSize / 4,  // FreeRTOS uses words
        this, config::TaskPriorities::kRadioEventPriority, &processing_task_);

    if (task_created != pdPASS) {
        vQueueDelete(event_queue_);
        return Result::error(LoraMesherErrorCode::kMemoryError);
    }

    // Create HAL module for SPI communication
    hal_module_ =
        std::make_unique<Module>(cs_pin_, irq_pin_, reset_pin_, busy_pin_);

    // Create RadioLib SX1276 instance
    radio_module_ = std::make_unique<SX1276>(hal_module_.get());

    // Set up static instance pointer for interrupt handling
    instance_ = this;

    // Attach interrupt handler if IRQ pin is specified
    if (irq_pin_ >= 0) {
        attachInterrupt(digitalPinToInterrupt(irq_pin_), handleInterruptStatic,
                        RISING);
    }

    return Result::success();
}

Result LoraMesherSX1276::begin(const RadioConfig& config) {
    // Initialize hardware first
    Result result = initializeHardware();
    if (!result)
        return result;

    // Begin radio module with default settings
    int status = radio_module_->begin();
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::convertStatus(status);
    }

    // Configure with provided settings
    return configure(config);
}

Result LoraMesherSX1276::configure(const RadioConfig& config) {
    // Store current configuration
    current_config_ = config;

    return Result::success();
}

Result LoraMesherSX1276::send(const uint8_t* data, size_t len) {
    // Check if radio is already transmitting
    if (current_state_ == RadioState::kTransmit) {
        return Result::error(LoraMesherErrorCode::kBusyError);
    }

    // Attempt to transmit data
    int status = radio_module_->transmit(data, len);
    if (status == RADIOLIB_ERR_NONE) {
        current_state_ = RadioState::kTransmit;
        return Result::success();
    }

    return RadioLibCodeErrors::convertStatus(status);
}

Result LoraMesherSX1276::startReceive() {
    // Check if already in receive mode
    if (current_state_ == RadioState::kReceive) {
        return Result::success();
    }

    // We might need to suspend the processing task temporarily
    if (processing_task_) {
        vTaskSuspend(processing_task_);
    }

    // Start continuous receive mode
    int status = radio_module_->startReceive();

    if (status == RADIOLIB_ERR_NONE) {
        current_state_ = RadioState::kReceive;
        // Resume the processing task
        if (processing_task_) {
            vTaskResume(processing_task_);
        }
        return Result::success();
    }

    // If we failed, still resume the task
    if (processing_task_) {
        vTaskResume(processing_task_);
    }
    return RadioLibCodeErrors::convertStatus(status);
}

Result LoraMesherSX1276::sleep() {
    // Suspend the processing task before sleep
    if (processing_task_) {
        vTaskSuspend(processing_task_);
    }

    int status = radio_module_->sleep();
    if (status == RADIOLIB_ERR_NONE) {
        current_state_ = RadioState::kSleep;
        // Note: We don't resume the task in sleep mode
        return Result::success();
    }

    // If sleep failed, resume the task
    if (processing_task_) {
        vTaskResume(processing_task_);
    }

    return RadioLibCodeErrors::convertStatus(status);
}

Result LoraMesherSX1276::setFrequency(float frequency) {
    int status = radio_module_->setFrequency(frequency);
    return RadioLibCodeErrors::convertStatus(status);
}

Result LoraMesherSX1276::setSpreadingFactor(uint8_t sf) {
    int status = radio_module_->setSpreadingFactor(sf);
    return RadioLibCodeErrors::convertStatus(status);
}

Result LoraMesherSX1276::setBandwidth(float bandwidth) {
    int status = radio_module_->setBandwidth(bandwidth);
    return RadioLibCodeErrors::convertStatus(status);
}

Result LoraMesherSX1276::setCodingRate(uint8_t coding_rate) {
    int status = radio_module_->setCodingRate(coding_rate);
    return RadioLibCodeErrors::convertStatus(status);
}

Result LoraMesherSX1276::setPower(uint8_t power) {
    int status = radio_module_->setOutputPower(power);
    return RadioLibCodeErrors::convertStatus(status);
}

Result LoraMesherSX1276::setSyncWord(uint8_t sync_word) {
    int status = radio_module_->setSyncWord(sync_word);
    return RadioLibCodeErrors::convertStatus(status);
}

Result LoraMesherSX1276::setCRC(bool enable) {
    int status = radio_module_->setCRC(enable);
    return RadioLibCodeErrors::convertStatus(status);
}

Result LoraMesherSX1276::setPreambleLength(uint16_t length) {
    int status = radio_module_->setPreambleLength(length);
    return RadioLibCodeErrors::convertStatus(status);
}

int8_t LoraMesherSX1276::getRSSI() {
    return radio_module_->getRSSI();
}

int8_t LoraMesherSX1276::getSNR() {
    return radio_module_->getSNR();
}

int8_t LoraMesherSX1276::getLastPacketRSSI() {
    return last_packet_rssi_;
}

int8_t LoraMesherSX1276::getLastPacketSNR() {
    return last_packet_snr_;
}

bool LoraMesherSX1276::isTransmitting() {
    return current_state_ == RadioState::kTransmit;
}

float LoraMesherSX1276::getFrequency() {
    return current_config_.getFrequency();
}

uint8_t LoraMesherSX1276::getSpreadingFactor() {
    return current_config_.getSpreadingFactor();
}

float LoraMesherSX1276::getBandwidth() {
    return current_config_.getBandwidth();
}

uint8_t LoraMesherSX1276::getCodingRate() {
    return current_config_.getCodingRate();
}

uint8_t LoraMesherSX1276::getPower() {
    return current_config_.getPower();
}

void LoraMesherSX1276::setReceiveCallback(
    std::function<void(RadioEvent&)> callback) {
    receive_callback_ = std::move(callback);
}

Result LoraMesherSX1276::setState(RadioState state) {
    switch (state) {
        case RadioState::kReceive:
            return startReceive();
        case RadioState::kSleep:
            return sleep();
        case RadioState::kIdle: {
            int status = radio_module_->standby();
            if (status == RADIOLIB_ERR_NONE) {
                current_state_ = RadioState::kIdle;
                return Result::success();
            }
            return RadioLibCodeErrors::convertStatus(status);
        }
        default:
            return Result::error(LoraMesherErrorCode::kInvalidParameter);
    }
}

RadioState LoraMesherSX1276::getState() {
    return current_state_;
}

void ICACHE_RAM_ATTR LoraMesherSX1276::handleInterruptStatic() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (instance_) {
        uint8_t event = 1;
        xQueueSendFromISR(instance_->event_queue_, &event,
                          &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void LoraMesherSX1276::processEvents(void* parameters) {
    auto radio = static_cast<LoraMesherSX1276*>(parameters);
    uint8_t event;

// For development/testing: Monitor stack usage
#ifdef DEBUG
    UBaseType_t minStackWords = 0;
#endif

    while (true) {
        if (xQueueReceive(radio->event_queue_, &event, portMAX_DELAY) ==
            pdTRUE) {
            // Periodic monitoring
            utils::TaskMonitor::monitorTask(
                radio->processing_task_, kTask_Tag,
                config::TaskConfig::kMinStackWatermark);

            radio->handleInterrupt();
        }
    }
}

void LoraMesherSX1276::handleInterrupt() {
    if (!receive_callback_)
        return;

    // Check if we received data
    size_t len = radio_module_->getPacketLength();
    if (len == 0)
        return;

    // Read the received data
    std::vector<uint8_t> buffer(len);
    int status = radio_module_->readData(buffer.data(), len);

    if (status == RADIOLIB_ERR_NONE) {
        // Create a message from the received data
        auto message = std::make_unique<BaseMessage>(std::move(buffer));

        // Update last packet info
        last_packet_rssi_ = radio_module_->getRSSI();
        last_packet_snr_ = radio_module_->getSNR();

        // Create and dispatch event
        auto event = CreateReceivedEvent(std::move(message), last_packet_rssi_,
                                         last_packet_snr_);
        receive_callback_(*event);
    }
}

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO