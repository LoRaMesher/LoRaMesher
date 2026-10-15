// src/hardware/radiolib/radiolib_modules/radiolib_module_base.hpp
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include <memory>
#include <stdexcept>
#include <string>

#include "RadioLib.h"

#include "radio_lib_code_errors.hpp"
#include "types/radio/radio.hpp"
#include "utils/logger.hpp"

namespace loramesher {
namespace radio {

/**
 * @brief Shared RadioLib implementation of the IRadio interface
 *
 * The four supported Semtech modules (SX1262/SX1268 of the SX126x family and
 * SX1276/SX1278 of the SX127x family) share an identical RadioLib control flow
 * except for three operations whose RadioLib signatures differ per family:
 * the modem `begin()` call, CRC configuration, and the OCP current limit. This
 * template centralizes everything else; family bases provide the three
 * customization points and the concrete leaf classes supply only their
 * identity (RadioType + name).
 *
 * @tparam RadioLibType Concrete RadioLib driver type (e.g. SX1262, SX1276)
 */
template <typename RadioLibType>
class RadioLibModuleBase : public IRadio {
   public:
    RadioLibModuleBase(int8_t cs_pin, int8_t irq_pin, int8_t reset_pin,
                       int8_t busy_pin, SPIClass& spi, RadioType radio_type,
                       const char* module_name)
        : cs_pin_(cs_pin),
          irq_pin_(irq_pin),
          reset_pin_(reset_pin),
          busy_pin_(busy_pin),
          spi_(spi),
          radio_type_(radio_type),
          module_name_(module_name) {}

    RadioLibModuleBase(const RadioLibModuleBase&) = delete;
    RadioLibModuleBase& operator=(const RadioLibModuleBase&) = delete;
    RadioLibModuleBase(RadioLibModuleBase&&) = default;
    RadioLibModuleBase& operator=(RadioLibModuleBase&&) = delete;
    ~RadioLibModuleBase() override = default;

    Result Begin(const RadioConfig& config) override {
        if (!config.IsValid()) {
            return Result::InvalidArgument(config.Validate());
        }

        Result result = InitializeHardware();
        if (!result) {
            return result;
        }

        int16_t status = ModemBegin(config);
        if (status != RADIOLIB_ERR_NONE) {
            return RadioLibCodeErrors::ConvertStatus(status);
        }

        // Re-apply the bandwidth so the low-data-rate optimization and
        // time-on-air state are recomputed for the configured spreading factor.
        status = radio_module_->setBandwidth(config.getBandwidth());
        if (status != RADIOLIB_ERR_NONE) {
            return RadioLibCodeErrors::ConvertStatus(status);
        }

        // Set OCP current limit (begin() resets it to 60 mA default)
        auto_current_limit_ = config.IsCurrentLimitAuto();
        float limit = auto_current_limit_
                          ? RadioConfig::RecommendedCurrentLimit(
                                radio_type_, config.getPower())
                          : config.getCurrentLimit();
        status = ApplyCurrentLimit(limit);
        if (status != RADIOLIB_ERR_NONE) {
            return RadioLibCodeErrors::ConvertStatus(status);
        }

        status = ApplyCrc(config.getCRC());
        if (status != RADIOLIB_ERR_NONE) {
            return RadioLibCodeErrors::ConvertStatus(status);
        }

        initialized_ = true;
        return Result::Success();
    }

    Result Send(const uint8_t* data, size_t len) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }

        int status = radio_module_->transmit(data, len);
        if (status == RADIOLIB_ERR_NONE) {
            return Result::Success();
        }
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    Result StartReceive() override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }

        int status = radio_module_->startReceive();
        if (status == RADIOLIB_ERR_NONE) {
            return Result::Success();
        }
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    Result Sleep() override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }

        int status = radio_module_->sleep();
        if (status == RADIOLIB_ERR_NONE) {
            return Result::Success();
        }
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    Result Standby() override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }

        int status = radio_module_->standby();
        if (status == RADIOLIB_ERR_NONE) {
            return Result::Success();
        }
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    Result setFrequency(float frequency) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }
        return RadioLibCodeErrors::ConvertStatus(
            radio_module_->setFrequency(frequency));
    }

    Result setSpreadingFactor(uint8_t sf) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }
        return RadioLibCodeErrors::ConvertStatus(
            radio_module_->setSpreadingFactor(sf));
    }

    Result setBandwidth(float bandwidth) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }
        return RadioLibCodeErrors::ConvertStatus(
            radio_module_->setBandwidth(bandwidth));
    }

    Result setCodingRate(uint8_t coding_rate) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }
        return RadioLibCodeErrors::ConvertStatus(
            radio_module_->setCodingRate(coding_rate));
    }

    Result setPower(int8_t power) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }

        // Raise OCP ceiling before PA ramps up to new power level
        if (auto_current_limit_) {
            float limit =
                RadioConfig::RecommendedCurrentLimit(radio_type_, power);
            int status = ApplyCurrentLimit(limit);
            if (status != RADIOLIB_ERR_NONE) {
                return RadioLibCodeErrors::ConvertStatus(status);
            }
        }

        return RadioLibCodeErrors::ConvertStatus(
            radio_module_->setOutputPower(power));
    }

    Result setCurrentLimit(float current_limit_ma) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }

        auto_current_limit_ = false;
        return RadioLibCodeErrors::ConvertStatus(
            ApplyCurrentLimit(current_limit_ma));
    }

    Result setSyncWord(uint8_t sync_word) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }
        return RadioLibCodeErrors::ConvertStatus(
            radio_module_->setSyncWord(sync_word));
    }

    Result setCRC(bool enable) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }
        return RadioLibCodeErrors::ConvertStatus(ApplyCrc(enable));
    }

    Result setPreambleLength(uint16_t length) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }
        return RadioLibCodeErrors::ConvertStatus(
            radio_module_->setPreambleLength(length));
    }

    Result setActionReceive(void (*callback)(void)) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }
        if (!callback) {
            return Result::Error(LoraMesherErrorCode::kInvalidParameter);
        }

        radio_module_->setPacketReceivedAction(callback);
        return Result::Success();
    }

    Result ClearActionReceive() override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }
        radio_module_->clearPacketReceivedAction();
        return Result::Success();
    }

    float getRSSI() override {
        return initialized_ ? radio_module_->getRSSI() : 0.0f;
    }

    float getSNR() override {
        return initialized_ ? radio_module_->getSNR() : 0.0f;
    }

    uint8_t getPacketLength() override {
        return initialized_ ? radio_module_->getPacketLength() : 0;
    }

    uint32_t getTimeOnAir(uint8_t length) override {
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
            LOG_ERROR("getTimeOnAir error for %u bytes: RadioLib code %d",
                      length, static_cast<int16_t>(raw_us));
            return 0;
        }

        RadioLibTime_t time_on_air = raw_us / 1000;
        // A full 255-byte payload at SF12/BW125/CR4-8 is legitimately ~11-12 s
        // on air, so only reject values beyond any real LoRa transmission as
        // garbage.
        constexpr RadioLibTime_t kMaxToaMs = 60000;
        if (time_on_air > kMaxToaMs) {
            LOG_ERROR("getTimeOnAir sanity fail: %lu ms for %u bytes",
                      static_cast<unsigned long>(time_on_air), length);
            return 0;
        }

        return static_cast<uint32_t>(time_on_air);
    }

    Result readData(uint8_t* data, size_t len) override {
        if (!initialized_) {
            return Result::Error(LoraMesherErrorCode::kNotInitialized);
        }

        int status = radio_module_->readData(data, len);
        if (status == RADIOLIB_ERR_NONE) {
            return Result::Success();
        }
        return RadioLibCodeErrors::ConvertStatus(status);
    }

    // Operations not supported by the low-level RadioLib modules.

    Result Configure(const RadioConfig& config) override {
        (void)config;
        throw Unsupported("Configure");
    }

    Result setActionReceive(
        std::function<void(std::unique_ptr<RadioEvent>)> callback) override {
        (void)callback;
        throw Unsupported("setActionReceive");
    }

    float getLastPacketRSSI() override {
        throw Unsupported("getLastPacketRSSI");
    }

    float getLastPacketSNR() override { throw Unsupported("getLastPacketSNR"); }

    bool IsTransmitting() override { throw Unsupported("IsTransmitting"); }

    float getFrequency() override { throw Unsupported("getFrequency"); }

    uint8_t getSpreadingFactor() override {
        throw Unsupported("getSpreadingFactor");
    }

    float getBandwidth() override { throw Unsupported("getBandwidth"); }

    uint8_t getCodingRate() override { throw Unsupported("getCodingRate"); }

    uint8_t getPower() override { throw Unsupported("getPower"); }

    Result setState(RadioState state) override {
        (void)state;
        throw Unsupported("setState");
    }

    RadioState getState() override { throw Unsupported("getState"); }

   protected:
    /// Family-specific RadioLib modem initialization (signatures differ).
    virtual int16_t ModemBegin(const RadioConfig& config) = 0;

    /// Family-specific CRC configuration (bool vs. uint8_t mode constant).
    virtual int16_t ApplyCrc(bool enable) = 0;

    /// Family-specific OCP current-limit configuration (float vs. uint8_t).
    virtual int16_t ApplyCurrentLimit(float current_limit_ma) = 0;

    const int8_t cs_pin_;
    const int8_t irq_pin_;
    const int8_t reset_pin_;
    const int8_t busy_pin_;
    SPIClass& spi_;  ///< SPI bus instance

    std::unique_ptr<Module> hal_module_;
    std::unique_ptr<RadioLibType> radio_module_;

    bool initialized_ = false;
    bool auto_current_limit_ = true;

   private:
    Result InitializeHardware() {
        hal_module_ = std::make_unique<Module>(cs_pin_, irq_pin_, reset_pin_,
                                               busy_pin_, spi_);
        if (!hal_module_) {
            return Result(LoraMesherErrorCode::kHardwareError,
                          "Hal module not initialized correctly, check for "
                          "correctly setted pins");
        }

        radio_module_ = std::make_unique<RadioLibType>(hal_module_.get());
        if (!radio_module_) {
            return Result(
                LoraMesherErrorCode::kHardwareError,
                std::string("Something went wrong when initializing ") +
                    module_name_ + ", check for correctly setted pins");
        }

        return Result::Success();
    }

    std::runtime_error Unsupported(const char* operation) const {
        return std::runtime_error(std::string(operation) +
                                  " not supported in " + module_name_);
    }

    const RadioType radio_type_;
    const char* const module_name_;
};

/**
 * @brief SX126x-family customization (SX1262, SX1268)
 *
 * SX126x modules take a TCXO voltage + LDO regulator flag on begin(), use a
 * uint8_t CRC mode constant, and accept a float OCP limit directly.
 */
template <typename RadioLibType>
class RadioLibSx126xModule : public RadioLibModuleBase<RadioLibType> {
   public:
    RadioLibSx126xModule(int8_t cs_pin, int8_t irq_pin, int8_t reset_pin,
                         int8_t busy_pin, SPIClass& spi, RadioType radio_type,
                         const char* module_name)
        : RadioLibModuleBase<RadioLibType>(cs_pin, irq_pin, reset_pin, busy_pin,
                                           spi, radio_type, module_name) {}

   protected:
    int16_t ModemBegin(const RadioConfig& config) override {
        // useRegulatorLDO = false (use DC-DC)
        return this->radio_module_->begin(
            config.getFrequency(), config.getBandwidth(),
            config.getSpreadingFactor(), config.getCodingRate(),
            config.getSyncWord(), config.getPower(), config.getPreambleLength(),
            config.getTcxoVoltage(), false);
    }

    int16_t ApplyCrc(bool enable) override {
        uint8_t crc_mode =
            enable ? RADIOLIB_SX126X_LORA_CRC_ON : RADIOLIB_SX126X_LORA_CRC_OFF;
        return this->radio_module_->setCRC(crc_mode);
    }

    int16_t ApplyCurrentLimit(float current_limit_ma) override {
        return this->radio_module_->setCurrentLimit(current_limit_ma);
    }
};

/**
 * @brief SX127x-family customization (SX1276, SX1278)
 *
 * SX127x modules take only the basic LoRa parameters on begin(), use a bool
 * CRC flag, and require a uint8_t OCP limit.
 */
template <typename RadioLibType>
class RadioLibSx127xModule : public RadioLibModuleBase<RadioLibType> {
   public:
    RadioLibSx127xModule(int8_t cs_pin, int8_t irq_pin, int8_t reset_pin,
                         int8_t busy_pin, SPIClass& spi, RadioType radio_type,
                         const char* module_name)
        : RadioLibModuleBase<RadioLibType>(cs_pin, irq_pin, reset_pin, busy_pin,
                                           spi, radio_type, module_name) {}

   protected:
    int16_t ModemBegin(const RadioConfig& config) override {
        return this->radio_module_->begin(
            config.getFrequency(), config.getBandwidth(),
            config.getSpreadingFactor(), config.getCodingRate(),
            config.getSyncWord(), config.getPower(),
            config.getPreambleLength());
    }

    int16_t ApplyCrc(bool enable) override {
        return this->radio_module_->setCRC(enable);
    }

    int16_t ApplyCurrentLimit(float current_limit_ma) override {
        return this->radio_module_->setCurrentLimit(
            static_cast<uint8_t>(current_limit_ma));
    }
};

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO
