// src/hardware/hardware_manager.cpp
#include "hardware_manager.hpp"

#include <stdexcept>

#include "hal_factory.hpp"
#include "radiolib/radiolib_radio.hpp"

namespace loramesher {
namespace hardware {

HardwareManager::HardwareManager(const PinConfig& pin_config,
                                 const RadioConfig& radio_config) {
    pin_config_ = pin_config;
    radio_config_ = radio_config;
}

Result HardwareManager::Initialize() {
    if (initialized_) {
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

    initialized_ = true;
    return Result::Success();
}

Result HardwareManager::SendMessage() {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    const uint8_t* data = new uint8_t(1);

    Result result = radio_->Send(data, 1);

    delete data;
    return result;
}

Result HardwareManager::setPinConfig(const PinConfig& pin_config) {
    if (!pin_config.IsValid()) {
        return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    pin_config_ = pin_config;
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
        return Result::Error(LoraMesherErrorCode::kHardwareError);
    }

    return Result::Success();
}

Result HardwareManager::InitializeRadioModule() {
    // Create radio module
    radio_ = radio::CreateRadio(pin_config_.getNss(), pin_config_.getDio0(),
                                pin_config_.getReset(), pin_config_.getDio1(),
                                hal_->getSPI());
    if (!radio_) {
        return Result::Error(LoraMesherErrorCode::kHardwareError);
    }

    // Configure radio
    if (!radio_->Configure(radio_config_)) {
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    if (!radio_->Begin(radio_config_)) {
        return Result::Error(LoraMesherErrorCode::kConfigurationError);
    }

    // TODO: Remove this line
    radio_->StartReceive();

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