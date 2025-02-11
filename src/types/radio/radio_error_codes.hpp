// src/radio/radio_error_codes.hpp
#pragma once

#include <string>
#include <system_error>

namespace loramesher {
namespace radio {

// Error codes specific to radio operations
enum class RadioErrorCode {
    kSuccess = 0,
    kConfigurationError,
    kTransmissionError,
    kReceptionError,
    kInvalidState,
    kHardwareError,
    kTimeout,
    kInvalidParameter,
    kBufferOverflow,
    kNotInitialized
};

// Custom error category for radio operations
class RadioErrorCategory : public std::error_category {
   public:
    // Singleton pattern following Google Style Guide
    static const RadioErrorCategory& GetInstance() {
        static RadioErrorCategory instance;
        return instance;
    }

    // Required override: provides string representation of the error
    const char* name() const noexcept override { return "radio_error"; }

    // Required override: provides detailed error message
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
            default:
                return "Unknown error";
        }
    }

   private:
    RadioErrorCategory() = default;
};

// Result class for radio operations
class Result {
   public:
    // Constructor for success
    Result() : error_code_(RadioErrorCode::kSuccess) {}

    // Constructor for error
    explicit Result(RadioErrorCode code) : error_code_(code) {}

    // Check if operation was successful
    bool IsSuccess() const { return error_code_ == RadioErrorCode::kSuccess; }

    // Get the error code
    RadioErrorCode GetErrorCode() const { return error_code_; }

    // Get error message
    std::string GetErrorMessage() const {
        return RadioErrorCategory::GetInstance().message(
            static_cast<int>(error_code_));
    }

    // Convert to std::error_code for standard error handling
    std::error_code AsErrorCode() const {
        return {static_cast<int>(error_code_),
                RadioErrorCategory::GetInstance()};
    }

    // Implicit conversion to bool for easy checking
    operator bool() const { return IsSuccess(); }

   private:
    RadioErrorCode error_code_;
};

// Factory functions for common results
inline Result Success() {
    return Result();
}

inline Result Error(RadioErrorCode code) {
    return Result(code);
}

}  // namespace radio
}  // namespace loramesher