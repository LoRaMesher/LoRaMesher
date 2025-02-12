// src/hardware/arduino/radioLibModules/radio_lib_code_errors.hpp
#pragma once

#include "build_options.hpp"

#ifdef LORAMESHER_BUILD_ARDUINO

#include <string>

#include "types/radio/radio_error_codes.hpp"

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
                return Success();
            case RADIOLIB_ERR_INVALID_FREQUENCY:
                return Error(RadioErrorCode::kFrequencyError);
            case RADIOLIB_ERR_INVALID_BANDWIDTH:
                return Error(RadioErrorCode::kConfigurationError);
            case RADIOLIB_ERR_INVALID_SPREADING_FACTOR:
                return Error(RadioErrorCode::kConfigurationError);
            case RADIOLIB_ERR_INVALID_CODING_RATE:
                return Error(RadioErrorCode::kConfigurationError);
            case RADIOLIB_ERR_INVALID_BIT_RANGE:
                return Error(RadioErrorCode::kInvalidParameter);
            case RADIOLIB_ERR_INVALID_SYNC_WORD:
                return Error(RadioErrorCode::kSyncWordError);
            default:
                return Error(RadioErrorCode::kHardwareError);
        }
    }
};

}  // namespace radio
}  // namespace loramesher

#endif  // LORAMESHER_BUILD_ARDUINO