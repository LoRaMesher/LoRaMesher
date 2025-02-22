#include "sx1276.hpp"

#include "radio_lib_code_errors.hpp"

namespace loramesher {
namespace radio {

Result LoraMesherSX1276::InitializeHardware() {
#ifdef LORAMESHER_BUILD_ARDUINO
    // Create HAL module for SPI communication
    hal_module_ =
        std::make_unique<Module>(cs_pin_, irq_pin_, reset_pin_, busy_pin_);
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
    int status = radio_module_->begin(config.getFrequency());
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    // Configure LoRa parameters
    status = radio_module_->setBandwidth(config.getBandwidth());
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    status = radio_module_->setSpreadingFactor(config.getSpreadingFactor());
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    status = radio_module_->setCodingRate(config.getCodingRate());
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    status = radio_module_->setSyncWord(config.getSyncWord());
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    status = radio_module_->setOutputPower(config.getPower());
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    status = radio_module_->setPreambleLength(config.getPreambleLength());
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    // Enable/Disable CRC based on configuration
    status = radio_module_->setCRC(config.getCRC());
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    return Result::Success();
}

Result LoraMesherSX1276::Send(const uint8_t* data, size_t len) {

    // Attempt to transmit data
    int status = radio_module_->transmit(data, len);
    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::StartReceive() {
    // Start continuous receive mode
    int status = radio_module_->startReceive();

    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::StartTransmit() {
    radio_module_->clearDio0Action();

    return Result::Success();
}

Result LoraMesherSX1276::Sleep() {
    int status = radio_module_->sleep();
    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setFrequency(float frequency) {
    int status = radio_module_->setFrequency(frequency);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setSpreadingFactor(uint8_t sf) {
    int status = radio_module_->setSpreadingFactor(sf);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setBandwidth(float bandwidth) {
    int status = radio_module_->setBandwidth(bandwidth);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setCodingRate(uint8_t coding_rate) {
    int status = radio_module_->setCodingRate(coding_rate);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setPower(uint8_t power) {
    int status = radio_module_->setOutputPower(power);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setSyncWord(uint8_t sync_word) {
    int status = radio_module_->setSyncWord(sync_word);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setCRC(bool enable) {
    int status = radio_module_->setCRC(enable);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setPreambleLength(uint16_t length) {
    int status = radio_module_->setPreambleLength(length);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1276::setActionReceive(void (*callback)(void)) {
    if (!callback) {
        return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    // Use a lambda to bridge between std::function and raw function pointer
    radio_module_->setPacketReceivedAction(callback);

    return Result::Success();
}

int8_t LoraMesherSX1276::getRSSI() {
    return radio_module_->getRSSI();
}

int8_t LoraMesherSX1276::getSNR() {
    return radio_module_->getSNR();
}

uint8_t LoraMesherSX1276::getPacketLength() {
    return radio_module_->getPacketLength();
}

Result LoraMesherSX1276::readData(uint8_t* data, size_t len) {
    int status = radio_module_->readData(data, len);
    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

}  // namespace radio
}  // namespace loramesher
