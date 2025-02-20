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

bool HardwareManager::Initialize() {
    if (initialized_) {
        return true;
    }

    // Initialize radio
    if (!InitializeHalModules()) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool HardwareManager::setPinConfig(const PinConfig& pin_config) {
    if (!pin_config.IsValid()) {
        return false;
    }

    pin_config_ = pin_config;
    return true;
}

bool HardwareManager::updateRadioConfig(const RadioConfig& radio_config) {
    if (!radio_config.IsValid()) {
        return false;
    }

    radio_config_ = radio_config;
    return true;
}

bool HardwareManager::InitializeHalModules() {
    // Create HAL module
    hal_ = hal::HalFactory::createHal();
    if (!hal_) {
        return false;
    }

    // Create radio module
    if (!InitializeRadioModule()) {
        return false;
    }

    return true;
}

bool HardwareManager::InitializeRadioModule() {
    // Create radio module
    radio_ = radio::CreateRadio(pin_config_.getNss(), pin_config_.getDio0(),
                                pin_config_.getReset(), pin_config_.getDio1(),
                                hal_->getSPI());
    if (!radio_) {
        return false;
    }

    // Configure radio
    if (!radio_->Configure(radio_config_)) {
        return false;
    }

    return true;
}

bool HardwareManager::ValidateConfiguration() const {
    return pin_config_.IsValid() && radio_config_.IsValid();
}

}  // namespace hardware
}  // namespace loramesher