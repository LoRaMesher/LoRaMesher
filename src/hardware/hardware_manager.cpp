// src/hardware/hardware_manager.cpp
#include "hardware_manager.hpp"

#include <stdexcept>

#include "hal_factory.hpp"
#include "radiolib/radiolib_radio.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace hardware {

HardwareManager::HardwareManager(const PinConfig& pin_config,
                                 const RadioConfig& radio_config) {
    pin_config_ = pin_config;
    radio_config_ = radio_config;
}

Result HardwareManager::Initialize() {
    if (is_initialized_) {
        return Result::Success();
    }

    Result result = InitializeHalModules();
    if (!result) {
        return result;
    }

    // Create radio module
    result = InitializeRadioModule();
    if (!result) {
        return result;
    }

    is_initialized_ = true;
    return Result::Success();
}

Result HardwareManager::Start() {
    if (!is_initialized_) {
        return Result(LoraMesherErrorCode::kNotInitialized,
                      "Hardware not initialized");
    }

    if (is_running_) {
        return Result::Success();
    }

    LOG_INFO("Starting hardware");

    Result result = radio_->Begin(radio_config_);
    if (!result) {
        return result;
    }

    result = StartReceive();
    if (!result) {
        return result;
    }

    is_running_ = true;
    return Result::Success();
}

Result HardwareManager::StartReceive() {
    if (!is_initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    Result result = radio_->StartReceive();
    if (!result) {
        return result;
    }

    return Result::Success();
}

Result HardwareManager::Stop() {
    if (!is_initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    if (!is_running_) {
        return Result::Success();
    }

    LOG_INFO("Stopping hardware");

    Result result = radio_->Sleep();
    if (!result) {
        return result;
    }

    is_running_ = false;
    return Result::Success();
}

Result HardwareManager::setActionReceive(EventCallback callback) {
    if (!is_initialized_) {
        return Result(LoraMesherErrorCode::kNotInitialized,
                      "Hardware not initialized");
    }

    Result result = radio_->setActionReceive(callback);
    if (!result) {
        return result;
    }

    event_callback_ = callback;
    return Result::Success();
}

Result HardwareManager::SendMessage(const BaseMessage& message) {
    if (!is_running_) {
        return Result(LoraMesherErrorCode::kInvalidState,
                      "Hardware not running");
    }

    LOG_DEBUG("Sending message to 0x%04X, type: %d",
              message.GetHeader().GetDestination(),
              static_cast<int>(message.GetHeader().GetType()));

    auto serialized_data = message.Serialize();
    if (!serialized_data) {
        return Result(LoraMesherErrorCode::kSerializationError,
                      "Serialization error when sending a message");
    }

    Result result =
        radio_->Send(serialized_data->data(), message.GetTotalSize());
    if (!result) {
        return result;
    }

    if (event_callback_) {
        auto event = radio::CreateTransmittedEvent(
            std::make_unique<BaseMessage>(message));

        event_callback_(std::move(event));
    }

    return Result::Success();
}

uint32_t HardwareManager::getTimeOnAir(uint8_t length) {
    if (!is_initialized_) {
        return 0;
    }

    return radio_->getTimeOnAir(length);
}

Result HardwareManager::setPinConfig(const PinConfig& pin_config) {
    if (!pin_config.IsValid()) {
        return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    pin_config_ = pin_config;
    return Result::Success();
}

Result HardwareManager::setState(radio::RadioState state) {
    if (!is_initialized_) {
        return Result(LoraMesherErrorCode::kNotInitialized,
                      "Hardware not initialized");
    }

    Result result = radio_->setState(state);
    if (!result) {
        return result;
    }

    return Result::Success();
}

Result HardwareManager::updateRadioConfig(const RadioConfig& radio_config) {
    if (!radio_config.IsValid()) {
        return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    radio_config_ = radio_config;
    return Result::Success();
}

Result HardwareManager::InitializeHalModules() {
    // Create HAL module
    hal_ = hal::HalFactory::createHal();
    if (!hal_) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Failed to create HAL module");
    }

    return Result::Success();
}

Result HardwareManager::InitializeRadioModule() {
    // Create radio module
    radio_ = radio::CreateRadio(pin_config_.getNss(), pin_config_.getDio0(),
                                pin_config_.getReset(), pin_config_.getDio1(),
                                hal_->getSPI());
    if (!radio_) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Failed to create radio module");
    }

    Result result = radio_->Configure(radio_config_);
    if (!result) {
        return result;
    }

    return Result::Success();
}

Result HardwareManager::ValidateConfiguration() const {
    if (!pin_config_.IsValid()) {
        return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    if (!radio_config_.IsValid()) {
        return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    return Result::Success();
}

}  // namespace hardware
}  // namespace loramesher