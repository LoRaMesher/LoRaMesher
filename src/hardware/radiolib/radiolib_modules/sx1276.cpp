#include "sx1276.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include "radio_lib_code_errors.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace radio {

Result LoraMesherSX1276::InitializeHardware() {
#ifdef LORAMESHER_BUILD_ARDUINO
    // Create HAL module for SPI communication
    hal_module_ =
        std::make_unique<Module>(cs_pin_, irq_pin_, reset_pin_, busy_pin_);
    if (!hal_module_) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Hal module not initialized correctly, check for "
                      "correctly setted pins");
    }
#else
// TODO: Implement esp-idf HAL
// Create HAL module for SPI communication
// include the hardware abstraction layer
// #include "hal/ESP-IDF/EspHal.h"

// // create a new instance of the HAL class
// EspHal* hal = new EspHal(5, 19, 27);

//     hal_module_ = std::make_unique<Module>(hal, cs_pin_, irq_pin_, reset_pin_, busy_pin_);
#endif

    // Create RadioLib SX1276 instance
    radio_module_ = std::make_unique<SX1276>(hal_module_.get());
    if (!radio_module_) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Something when wrong when initializing SX1276, check "
                      "for correctly setted pins");
    }

    return Result::Success();
}

Result LoraMesherSX1276::Begin(const RadioConfig& config) {
    // Validate configuration first
    if (!config.IsValid()) {
        return Result::InvalidArgument(config.Validate());
    }

    // Initialize hardware first
    Result result = InitializeHardware();
    if (!result) {
        return result;
    }

    // Begin radio module with basic frequency
    int16_t status = radio_module_->begin(
        config.getFrequency(), config.getBandwidth(),
        config.getSpreadingFactor(), config.getCodingRate(),
        config.getSyncWord(), config.getPower(), config.getPreambleLength());
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    // Enable/Disable CRC based on configuration
    status = radio_module_->setCRC(config.getCRC());
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    initialized_ = true;

    return Result::Success();
}

Result LoraMesherSX1276::Send(const uint8_t* data, size_t len) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Attempt to transmit data
    int status = radio_module_->transmit(data, len);
    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::StartReceive() {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Start continuous receive mode
    int status = radio_module_->startReceive();

    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::Sleep() {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->sleep();
    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setFrequency(float frequency) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setFrequency(frequency);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setSpreadingFactor(uint8_t sf) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setSpreadingFactor(sf);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setBandwidth(float bandwidth) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setBandwidth(bandwidth);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setCodingRate(uint8_t coding_rate) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setCodingRate(coding_rate);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setPower(int8_t power) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setOutputPower(power);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setSyncWord(uint8_t sync_word) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setSyncWord(sync_word);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setCRC(bool enable) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setCRC(enable);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setPreambleLength(uint16_t length) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setPreambleLength(length);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setActionReceive(void (*callback)(void)) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    if (!callback) {
        return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    radio_module_->setPacketReceivedAction(callback);

    return Result::Success();
}

Result LoraMesherSX1276::ClearActionReceive() {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }
    radio_module_->clearPacketReceivedAction();

    return Result::Success();
}

int8_t LoraMesherSX1276::getRSSI() {
    if (!initialized_) {
        return 0;
    }

    return radio_module_->getRSSI();
}

int8_t LoraMesherSX1276::getSNR() {
    if (!initialized_) {
        return 0;
    }

    return radio_module_->getSNR();
}

uint8_t LoraMesherSX1276::getPacketLength() {
    if (!initialized_) {
        return 0;
    }

    return radio_module_->getPacketLength();
}

uint32_t LoraMesherSX1276::getTimeOnAir(uint8_t length) {
    if (!initialized_) {
        return 0;
    }

    RadioLibTime_t time_on_air = radio_module_->getTimeOnAir(length) / 1000;
    if (time_on_air == 0) {
        return 0;
    }

    return static_cast<uint32_t>(time_on_air);
}

Result LoraMesherSX1276::readData(uint8_t* data, size_t len) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->readData(data, len);
    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO
