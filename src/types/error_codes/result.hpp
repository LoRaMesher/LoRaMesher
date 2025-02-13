// src/types/error_codes/result.hpp
#pragma once

#include "loramesher_error_codes.hpp"

namespace loramesher {

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
    Result() : error_code_(LoraMesherErrorCode::kSuccess) {}

    /**
      * @brief Construct a new Result with an error
      * 
      * @param code The error code
      */
    explicit Result(LoraMesherErrorCode code) : error_code_(code) {}

    /**
      * @brief Check if the operation was successful
      * 
      * @return bool True if operation succeeded
      */
    bool isSuccess() const {
        return error_code_ == LoraMesherErrorCode::kSuccess;
    }

    /**
      * @brief Get the error code
      * 
      * @return LoraMesherErrorCode The error code
      */
    LoraMesherErrorCode getErrorCode() const { return error_code_; }

    /**
      * @brief Get a human-readable error message
      * 
      * @return std::string The error message
      */
    std::string getErrorMessage() const {
        return LoraMesherErrorCategory::GetInstance().message(
            static_cast<int>(error_code_));
    }

    /**
      * @brief Convert to std::error_code for standard error handling
      * 
      * @return std::error_code Standard error code
      */
    std::error_code asErrorCode() const {
        return {static_cast<int>(error_code_),
                LoraMesherErrorCategory::GetInstance()};
    }

    /**
      * @brief Implicit conversion to bool for easy checking
      * 
      * @return bool True if operation succeeded
      */
    operator bool() const { return isSuccess(); }

    /**
     * @brief Create a successful Result
     * 
     * @return Result A successful Result object
     */
    static inline Result success() { return Result(); }

    /**
     * @brief Create a Result with an error
     * 
     * @param code The error code
     * @return Result A Result object with the specified error
     */
    static inline Result error(LoraMesherErrorCode code) {
        return Result(code);
    }

    /**
     * @brief Convert a bool to a Result
     * 
     * @param success True for success, false for generic error
     * @return Result Success or generic error Result
     */
    inline Result toResult(bool success) {
        return success ? Result::success()
                       : error(LoraMesherErrorCode::kHardwareError);
    }

   private:
    LoraMesherErrorCode error_code_;  ///< The error code for this result
};

}  // namespace loramesher