// test/types/test_radio/radio_error_test.cpp
#include <gtest/gtest.h>

#include "types/error_codes/result.hpp"

namespace loramesher {
namespace test {

class LoraMesherErrorTest : public ::testing::Test {
   protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(LoraMesherErrorTest, SuccessResultTest) {
    Result result = Result::success();
    EXPECT_TRUE(result.isSuccess());
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kSuccess);
    EXPECT_EQ(result.getErrorMessage(), "Operation completed successfully");
}

TEST_F(LoraMesherErrorTest, ErrorResultTest) {
    Result result = Result::error(LoraMesherErrorCode::kConfigurationError);
    EXPECT_FALSE(result.isSuccess());
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kConfigurationError);
    EXPECT_EQ(result.getErrorMessage(), "Failed to configure radio parameters");
}

TEST_F(LoraMesherErrorTest, ErrorCategoryTest) {
    const auto& category = LoraMesherErrorCategory::GetInstance();
    EXPECT_EQ(category.name(), std::string("radio_error"));

    // Test various error messages
    EXPECT_EQ(category.message(static_cast<int>(LoraMesherErrorCode::kTimeout)),
              "Operation timed out");
    EXPECT_EQ(category.message(
                  static_cast<int>(LoraMesherErrorCode::kInvalidParameter)),
              "Invalid parameter provided");
    EXPECT_EQ(category.message(
                  static_cast<int>(LoraMesherErrorCode::kBufferOverflow)),
              "Buffer overflow detected");
}

TEST_F(LoraMesherErrorTest, ErrorCodeConversionTest) {
    Result result = Result::error(LoraMesherErrorCode::kHardwareError);
    std::error_code error_code = result.asErrorCode();

    EXPECT_EQ(error_code.value(),
              static_cast<int>(LoraMesherErrorCode::kHardwareError));
    EXPECT_EQ(error_code.category().name(), std::string("radio_error"));
    EXPECT_EQ(error_code.message(), "Hardware-level error occurred");
}

}  // namespace test
}  // namespace loramesher