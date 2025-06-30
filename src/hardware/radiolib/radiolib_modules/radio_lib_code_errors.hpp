// src/hardware/arduino/radio_lib_modules/radio_lib_code_errors.hpp
#pragma once

#include "config/system_config.hpp"

#include <string>

#include "types/error_codes/result.hpp"

#include "RadioLib.h"

namespace loramesher {
namespace radio {

/**
 * @brief Convert RadioLib status code to Result
 */
class RadioLibCodeErrors {
   public:
    /**
     * @brief Convert RadioLib status code to Result
     * 
     * @param status RadioLib status code
     * @return Result Result code
     */
    static Result ConvertStatus(int status) {
        switch (status) {
            case RADIOLIB_ERR_NONE:
                return Result::Success();
            case RADIOLIB_ERR_INVALID_FREQUENCY:
                return Result::Error(LoraMesherErrorCode::kFrequencyError);
            case RADIOLIB_ERR_INVALID_BANDWIDTH:
                return Result::Error(LoraMesherErrorCode::kConfigurationError);
            case RADIOLIB_ERR_INVALID_SPREADING_FACTOR:
                return Result::Error(LoraMesherErrorCode::kConfigurationError);
            case RADIOLIB_ERR_INVALID_CODING_RATE:
                return Result::Error(LoraMesherErrorCode::kConfigurationError);
            case RADIOLIB_ERR_INVALID_BIT_RANGE:
                return Result::Error(LoraMesherErrorCode::kInvalidParameter);
            case RADIOLIB_ERR_INVALID_SYNC_WORD:
                return Result::Error(LoraMesherErrorCode::kSyncWordError);
            default:
                return Result(LoraMesherErrorCode::kHardwareError,
                              "RadioLib error: " + std::to_string(status) +
                                  " - Unknown error occurred");
        }
    }
};

}  // namespace radio
}  // namespace loramesher