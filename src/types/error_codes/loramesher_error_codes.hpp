// src/types/error_codes/loramesher_error_codes.hpp
#pragma once

#include <string>
#include <system_error>

namespace loramesher {

/**
 * @brief Error codes specific to radio operations
 * 
 * Defines all possible error conditions that can occur during radio operations.
 */
enum class LoraMesherErrorCode {
    kSuccess = 0,         ///< Operation completed successfully
    kConfigurationError,  ///< Failed to configure radio parameters
    kTransmissionError,   ///< Failed to transmit data
    kReceptionError,      ///< Failed to receive data
    kInvalidState,        ///< Radio in invalid state for operation
    kHardwareError,       ///< Hardware-level error occurred
    kTimeout,             ///< Operation timed out
    kInvalidParameter,    ///< Invalid parameter provided
    kBufferOverflow,      ///< Buffer overflow detected
    kNotInitialized,      ///< Radio not initialized
    kCrcError,            ///< CRC check failed
    kPreambleError,       ///< Preamble detection failed
    kSyncWordError,       ///< Sync word validation failed
    kFrequencyError,      ///< Frequency setting error
    kCalibrationError,    ///< Calibration failed
    kMemoryError,         ///< Memory allocation/access error
    kBusyError,           ///< Radio busy with another operation
    kInterruptError,      ///< Interrupt handling error
    kModulationError,     ///< Modulation parameter error
    kInvalidArgument      ///< Invalid argument error
};

/**
 * @brief Custom error category for radio operations
 * 
 * Provides string representations and error handling for radio-specific errors.
 */
class LoraMesherErrorCategory : public std::error_category {
   public:
    /**
     * @brief Get the singleton instance of the error category
     * 
     * @return const LoraMesherErrorCategory& Reference to the singleton instance
     */
    static const LoraMesherErrorCategory& GetInstance() {
        static LoraMesherErrorCategory instance;
        return instance;
    }

    /**
     * @brief Get the name of the error category
     * 
     * @return const char* Name of the error category
     */
    const char* name() const noexcept override { return "radio_error"; }

    /**
     * @brief Get a human-readable error message for a given error code
     * 
     * @param condition The error code to get the message for
     * @return std::string Human-readable error message
     */
    std::string message(int condition) const override {
        switch (static_cast<LoraMesherErrorCode>(condition)) {
            case LoraMesherErrorCode::kSuccess:
                return "Success";
            case LoraMesherErrorCode::kConfigurationError:
                return "Failed to configure radio parameters";
            case LoraMesherErrorCode::kTransmissionError:
                return "Failed to transmit data";
            case LoraMesherErrorCode::kReceptionError:
                return "Failed to receive data";
            case LoraMesherErrorCode::kInvalidState:
                return "Radio in invalid state for operation";
            case LoraMesherErrorCode::kHardwareError:
                return "Hardware-level error occurred";
            case LoraMesherErrorCode::kTimeout:
                return "Operation timed out";
            case LoraMesherErrorCode::kInvalidParameter:
                return "Invalid parameter provided";
            case LoraMesherErrorCode::kBufferOverflow:
                return "Buffer overflow detected";
            case LoraMesherErrorCode::kNotInitialized:
                return "Radio not initialized";
            case LoraMesherErrorCode::kCrcError:
                return "CRC check failed";
            case LoraMesherErrorCode::kPreambleError:
                return "Preamble detection failed";
            case LoraMesherErrorCode::kSyncWordError:
                return "Sync word validation failed";
            case LoraMesherErrorCode::kFrequencyError:
                return "Frequency setting error";
            case LoraMesherErrorCode::kCalibrationError:
                return "Calibration failed";
            case LoraMesherErrorCode::kMemoryError:
                return "Memory allocation or access error";
            case LoraMesherErrorCode::kBusyError:
                return "Radio busy with another operation";
            case LoraMesherErrorCode::kInterruptError:
                return "Interrupt handling error";
            case LoraMesherErrorCode::kModulationError:
                return "Modulation parameter error";
            // TODO: Add more here.
            default:
                return "Unknown error";
        }
    }

   private:
    LoraMesherErrorCategory() = default;  // Private constructor for singleton
};

}  // namespace loramesher