// src/hardware/arduino/radio_lib_modules/radio_lib_code_errors.hpp
#pragma once

#include "config/system_config.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include <string>

#include "types/error_codes/result.hpp"

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
    static Result convertStatus(int status) {
        switch (status) {
            case RADIOLIB_ERR_NONE:
                return Result::success();
            case RADIOLIB_ERR_INVALID_FREQUENCY:
                return Result::error(LoraMesherErrorCode::kFrequencyError);
            case RADIOLIB_ERR_INVALID_BANDWIDTH:
                return Result::error(LoraMesherErrorCode::kConfigurationError);
            case RADIOLIB_ERR_INVALID_SPREADING_FACTOR:
                return Result::error(LoraMesherErrorCode::kConfigurationError);
            case RADIOLIB_ERR_INVALID_CODING_RATE:
                return Result::error(LoraMesherErrorCode::kConfigurationError);
            case RADIOLIB_ERR_INVALID_BIT_RANGE:
                return Result::error(LoraMesherErrorCode::kInvalidParameter);
            case RADIOLIB_ERR_INVALID_SYNC_WORD:
                return Result::error(LoraMesherErrorCode::kSyncWordError);
            default:
                return Result::error(LoraMesherErrorCode::kHardwareError);
        }
    }
};

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO