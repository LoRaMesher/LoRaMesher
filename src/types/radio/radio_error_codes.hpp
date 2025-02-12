// src/types/radio/radio_error_codes.hpp
#pragma once

#include <string>
#include <system_error>

namespace loramesher {
namespace radio {

/**
 * @brief Error codes specific to radio operations
 * 
 * Defines all possible error conditions that can occur during radio operations.
 */
enum class RadioErrorCode {
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
    kModulationError      ///< Modulation parameter error
};

/**
 * @brief Custom error category for radio operations
 * 
 * Provides string representations and error handling for radio-specific errors.
 */
class RadioErrorCategory : public std::error_category {
   public:
    /**
     * @brief Get the singleton instance of the error category
     * 
     * @return const RadioErrorCategory& Reference to the singleton instance
     */
    static const RadioErrorCategory& GetInstance() {
        static RadioErrorCategory instance;
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
        switch (static_cast<RadioErrorCode>(condition)) {
            case RadioErrorCode::kSuccess:
                return "Operation completed successfully";
            case RadioErrorCode::kConfigurationError:
                return "Failed to configure radio parameters";
            case RadioErrorCode::kTransmissionError:
                return "Failed to transmit data";
            case RadioErrorCode::kReceptionError:
                return "Failed to receive data";
            case RadioErrorCode::kInvalidState:
                return "Radio in invalid state for operation";
            case RadioErrorCode::kHardwareError:
                return "Hardware-level error occurred";
            case RadioErrorCode::kTimeout:
                return "Operation timed out";
            case RadioErrorCode::kInvalidParameter:
                return "Invalid parameter provided";
            case RadioErrorCode::kBufferOverflow:
                return "Buffer overflow detected";
            case RadioErrorCode::kNotInitialized:
                return "Radio not initialized";
            case RadioErrorCode::kCrcError:
                return "CRC check failed";
            case RadioErrorCode::kPreambleError:
                return "Preamble detection failed";
            case RadioErrorCode::kSyncWordError:
                return "Sync word validation failed";
            case RadioErrorCode::kFrequencyError:
                return "Frequency setting error";
            case RadioErrorCode::kCalibrationError:
                return "Calibration failed";
            case RadioErrorCode::kMemoryError:
                return "Memory allocation or access error";
            case RadioErrorCode::kBusyError:
                return "Radio busy with another operation";
            case RadioErrorCode::kInterruptError:
                return "Interrupt handling error";
            case RadioErrorCode::kModulationError:
                return "Modulation parameter error";
            default:
                return "Unknown error";
        }
    }

   private:
    RadioErrorCategory() = default;  // Private constructor for singleton
};

/**
 * @brief Class representing the result of a radio operation
 * 
 * Provides a way to handle both successful and failed radio operations
 * with detailed error information when needed.
 */
class Result {
   public:
    /**
     * @brief Construct a new successful Result
     */
    Result() : error_code_(RadioErrorCode::kSuccess) {}

    /**
     * @brief Construct a new Result with an error
     * 
     * @param code The error code
     */
    explicit Result(RadioErrorCode code) : error_code_(code) {}

    /**
     * @brief Check if the operation was successful
     * 
     * @return bool True if operation succeeded
     */
    bool IsSuccess() const { return error_code_ == RadioErrorCode::kSuccess; }

    /**
     * @brief Get the error code
     * 
     * @return RadioErrorCode The error code
     */
    RadioErrorCode GetErrorCode() const { return error_code_; }

    /**
     * @brief Get a human-readable error message
     * 
     * @return std::string The error message
     */
    std::string GetErrorMessage() const {
        return RadioErrorCategory::GetInstance().message(
            static_cast<int>(error_code_));
    }

    /**
     * @brief Convert to std::error_code for standard error handling
     * 
     * @return std::error_code Standard error code
     */
    std::error_code AsErrorCode() const {
        return {static_cast<int>(error_code_),
                RadioErrorCategory::GetInstance()};
    }

    /**
     * @brief Implicit conversion to bool for easy checking
     * 
     * @return bool True if operation succeeded
     */
    operator bool() const { return IsSuccess(); }

   private:
    RadioErrorCode error_code_;  ///< The error code for this result
};

/**
 * @brief Create a successful Result
 * 
 * @return Result A successful Result object
 */
inline Result Success() {
    return Result();
}

/**
 * @brief Create a Result with an error
 * 
 * @param code The error code
 * @return Result A Result object with the specified error
 */
inline Result Error(RadioErrorCode code) {
    return Result(code);
}

/**
 * @brief Convert a bool to a Result
 * 
 * @param success True for success, false for generic error
 * @return Result Success or generic error Result
 */
inline Result ToResult(bool success) {
    return success ? Success() : Error(RadioErrorCode::kHardwareError);
}

}  // namespace radio
}  // namespace loramesher