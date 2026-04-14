#include "sx1262.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include "radio_lib_code_errors.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace radio {

Result LoraMesherSX1262::InitializeHardware() {
#ifdef LORAMESHER_BUILD_ARDUINO
    // Create HAL module for SPI communication
    hal_module_ = std::make_unique<Module>(cs_pin_, irq_pin_, reset_pin_,
                                           busy_pin_, spi_);
    if (!hal_module_) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Hal module not initialized correctly, check for "
                      "correctly setted pins");
    }
#else
// TODO: Implement esp-idf HAL
#endif

    // Create RadioLib SX1262 instance
    radio_module_ = std::make_unique<SX1262>(hal_module_.get());
    if (!radio_module_) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Something went wrong when initializing SX1262, check "
                      "for correctly setted pins");
    }

    return Result::Success();
}

Result LoraMesherSX1262::Begin(const RadioConfig& config) {
    // Validate configuration first
    if (!config.IsValid()) {
        return Result::InvalidArgument(config.Validate());
    }

    // Initialize hardware first
    Result result = InitializeHardware();
    if (!result) {
        return result;
    }

    // Begin radio module with SX1262-specific parameters
    // useRegulatorLDO = false (use DC-DC)
    int16_t status = radio_module_->begin(
        config.getFrequency(), config.getBandwidth(),
        config.getSpreadingFactor(), config.getCodingRate(),
        config.getSyncWord(), config.getPower(), config.getPreambleLength(),
        config.getTcxoVoltage(), false);
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    // Set OCP current limit (begin() resets it to 60 mA default)
    auto_current_limit_ = config.IsCurrentLimitAuto();
    float limit = auto_current_limit_
                      ? RadioConfig::RecommendedCurrentLimit(RadioType::kSx1262,
                                                             config.getPower())
                      : config.getCurrentLimit();
    status = radio_module_->setCurrentLimit(limit);
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    // Enable/Disable CRC based on configuration
    // SX1262 uses uint8_t for CRC mode instead of bool
    uint8_t crc_mode = config.getCRC() ? RADIOLIB_SX126X_LORA_CRC_ON
                                       : RADIOLIB_SX126X_LORA_CRC_OFF;
    status = radio_module_->setCRC(crc_mode);
    if (status != RADIOLIB_ERR_NONE) {
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    initialized_ = true;

    return Result::Success();
}

Result LoraMesherSX1262::Send(const uint8_t* data, size_t len) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->transmit(data, len);
    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::StartReceive() {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->startReceive();
    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::Sleep() {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->sleep();
    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::Standby() {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->standby();
    if (status == RADIOLIB_ERR_NONE) {
        return Result::Success();
    }

    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::setFrequency(float frequency) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setFrequency(frequency);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::setSpreadingFactor(uint8_t sf) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setSpreadingFactor(sf);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::setBandwidth(float bandwidth) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setBandwidth(bandwidth);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::setCodingRate(uint8_t coding_rate) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setCodingRate(coding_rate);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::setPower(int8_t power) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    // Raise OCP ceiling before PA ramps up to new power level
    if (auto_current_limit_) {
        float limit =
            RadioConfig::RecommendedCurrentLimit(RadioType::kSx1262, power);
        int status = radio_module_->setCurrentLimit(limit);
        if (status != RADIOLIB_ERR_NONE) {
            return RadioLibCodeErrors::ConvertStatus(status);
        }
    }

    int status = radio_module_->setOutputPower(power);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::setCurrentLimit(float current_limit_ma) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    auto_current_limit_ = false;
    int status = radio_module_->setCurrentLimit(current_limit_ma);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::setSyncWord(uint8_t sync_word) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setSyncWord(sync_word);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::setCRC(bool enable) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    uint8_t crc_mode =
        enable ? RADIOLIB_SX126X_LORA_CRC_ON : RADIOLIB_SX126X_LORA_CRC_OFF;
    int status = radio_module_->setCRC(crc_mode);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::setPreambleLength(uint16_t length) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    int status = radio_module_->setPreambleLength(length);
    return RadioLibCodeErrors::ConvertStatus(status);
}

Result LoraMesherSX1262::setActionReceive(void (*callback)(void)) {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    if (!callback) {
        return Result::Error(LoraMesherErrorCode::kInvalidParameter);
    }

    radio_module_->setPacketReceivedAction(callback);

    return Result::Success();
}

Result LoraMesherSX1262::ClearActionReceive() {
    if (!initialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }
    radio_module_->clearPacketReceivedAction();

    return Result::Success();
}

float LoraMesherSX1262::getRSSI() {
    if (!initialized_) {
        return 0.0f;
    }

    return radio_module_->getRSSI();
}

float LoraMesherSX1262::getSNR() {
    if (!initialized_) {
        return 0.0f;
    }

    return radio_module_->getSNR();
}

uint8_t LoraMesherSX1262::getPacketLength() {
    if (!initialized_) {
        return 0;
    }

    return radio_module_->getPacketLength();
}

uint32_t LoraMesherSX1262::getTimeOnAir(uint8_t length) {
    if (!initialized_) {
        return 0;
    }

    RadioLibTime_t raw_us = radio_module_->getTimeOnAir(length);
    if (raw_us == 0) {
        return 0;
    }

    // RadioLib returns signed error codes (e.g. RADIOLIB_ERR_WRONG_MODEM
    // = -20) through its unsigned RadioLibTime_t return type. Any raw
    // value above 2^31 us (~35 minutes) is a negative error cast to
    // unsigned, not a real time-on-air.
    constexpr RadioLibTime_t kErrorThresholdUs = 0x7FFFFFFFul;
    if (raw_us > kErrorThresholdUs) {
        LOG_ERROR("getTimeOnAir error for %u bytes: RadioLib code %d", length,
                  static_cast<int16_t>(raw_us));
        return 0;
    }

    RadioLibTime_t time_on_air = raw_us / 1000;
    if (time_on_air > 10000) {
        LOG_ERROR("getTimeOnAir sanity fail: %lu ms for %u bytes",
                  static_cast<unsigned long>(time_on_air), length);
        return 0;
    }

    return static_cast<uint32_t>(time_on_air);
}

Result LoraMesherSX1262::readData(uint8_t* data, size_t len) {
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
