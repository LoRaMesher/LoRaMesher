// src/types/error_codes/result.hpp
#pragma once

#include <sstream>
#include <string>
#include <vector>
#include "loramesher_error_codes.hpp"

namespace loramesher {

/**
 * @brief Struct representing a single error with code and message
 */
struct ErrorInfo {
    LoraMesherErrorCode code;  ///< The error code
    std::string message;       ///< The error message

    /**
   * @brief Construct a new Error Info object
   * 
   * @param error_code The error code
   * @param error_message The error message
   */
    ErrorInfo(LoraMesherErrorCode error_code, std::string error_message)
        : code(error_code), message(std::move(error_message)) {}
};

/**
 * @brief Class representing the result of a radio operation
 * 
 * Provides a way to handle both successful and failed radio operations
 * with support for multiple detailed error information when needed.
 */
class Result {
   public:
    /**
    * @brief Construct a new successful Result
    */
    Result() : has_errors_(false) {}

    /**
    * @brief Construct a new Result with a single error
    * 
    * @param code The error code
    */
    explicit Result(LoraMesherErrorCode code) {
        if (code == LoraMesherErrorCode::kSuccess) {
            has_errors_ = false;
            return;
        }
        AddError(code, LoraMesherErrorCategory::GetInstance().message(
                           static_cast<int>(code)));
    }

    /**
    * @brief Construct a new Result with a single error and custom message
    * 
    * @param code The error code
    * @param message Custom error message
    */
    Result(LoraMesherErrorCode code, std::string message) : has_errors_(true) {
        AddError(code, std::move(message));
    }

    /**
    * @brief Check if the operation was successful
    * 
    * @return bool True if operation succeeded (no errors)
    */
    bool IsSuccess() const { return !has_errors_; }

    /**
    * @brief Get the primary error code (first error if multiple exist)
    * 
    * @return LoraMesherErrorCode The error code
    */
    LoraMesherErrorCode getErrorCode() const {
        return has_errors_ ? errors_[0].code : LoraMesherErrorCode::kSuccess;
    }

    /**
    * @brief Get all error codes
    * 
    * @return std::vector<LoraMesherErrorCode> Vector of all error codes
    */
    std::vector<LoraMesherErrorCode> GetAllErrorCodes() const {
        std::vector<LoraMesherErrorCode> codes;
        for (const auto& error : errors_) {
            codes.push_back(error.code);
        }
        return codes;
    }

    /**
    * @brief Get a human-readable error message combining all errors
    * 
    * @return std::string The combined error message
    */
    std::string GetErrorMessage() const {
        if (!has_errors_) {
            return "Success";
        }
        std::stringstream ss;
        for (size_t i = 0; i < errors_.size(); ++i) {
            if (i > 0) {
                ss << "\n";
            }
            ss << errors_[i].message;
        }
        return ss.str();
    }

    /**
    * @brief Add an error to the result
    * 
    * @param code The error code
    * @param message The error message
    */
    void AddError(LoraMesherErrorCode code, std::string message) {
        has_errors_ = true;
        errors_.emplace_back(code, std::move(message));
    }

    /**
    * @brief Check if result contains a specific error code
    * 
    * @param code The error code to check for
    * @return bool True if the error code is present
    */
    bool HasError(LoraMesherErrorCode code) const {
        for (const auto& error : errors_) {
            if (error.code == code) {
                return true;
            }
        }
        return false;
    }

    /**
    * @brief Get the number of errors
    * 
    * @return size_t Number of errors
    */
    size_t GetErrorCount() const { return errors_.size(); }

    /**
    * @brief Merge another result's errors into this one
    * 
    * @param other The other result to merge from
    */
    void MergeErrors(const Result& other) {
        if (!other.IsSuccess()) {
            has_errors_ = true;
            errors_.insert(errors_.end(), other.errors_.begin(),
                           other.errors_.end());
        }
    }

    /**
    * @brief Convert to std::error_code for standard error handling
    * 
    * @return std::error_code Standard error code (returns first error if multiple exist)
    */
    std::error_code AsErrorCode() const {
        return {static_cast<int>(getErrorCode()),
                LoraMesherErrorCategory::GetInstance()};
    }

    /**
    * @brief Implicit conversion to bool for easy checking
    * 
    * @return bool True if operation succeeded (no errors)
    */
    operator bool() const { return IsSuccess(); }

    /**
    * @brief Create a successful Result
    * 
    * @return Result A successful Result object
    */
    static inline Result Success() { return Result(); }

    /**
    * @brief Create a Result with an error
    * 
    * @param code The error code
    * @return Result A Result object with the specified error
    */
    static inline Result Error(LoraMesherErrorCode code) {
        return Result(code);
    }

    /**
    * @brief Create a Result for invalid argument error with custom message
    * 
    * @param message Description of why the argument is invalid
    * @return Result A Result object with invalid argument error
    */
    static inline Result InvalidArgument(const std::string& message) {
        return Result(LoraMesherErrorCode::kInvalidArgument, message);
    }

   private:
    bool has_errors_;  ///< Flag indicating if there are any errors
    std::vector<ErrorInfo> errors_;  ///< List of errors
};

}  // namespace loramesher