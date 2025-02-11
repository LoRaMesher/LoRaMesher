// test/types/test_radio/radio_error_test.cpp
#include <gtest/gtest.h>

#include "types/radio/radio_error_codes.hpp"

namespace loramesher {
namespace radio {
namespace test {

class RadioErrorTest : public ::testing::Test {
   protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(RadioErrorTest, SuccessResultTest) {
    Result result = Success();
    EXPECT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.GetErrorCode(), RadioErrorCode::kSuccess);
    EXPECT_EQ(result.GetErrorMessage(), "Operation completed successfully");
}

TEST_F(RadioErrorTest, ErrorResultTest) {
    Result result = Error(RadioErrorCode::kConfigurationError);
    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.GetErrorCode(), RadioErrorCode::kConfigurationError);
    EXPECT_EQ(result.GetErrorMessage(), "Failed to configure radio parameters");
}

TEST_F(RadioErrorTest, ErrorCategoryTest) {
    const auto& category = RadioErrorCategory::GetInstance();
    EXPECT_EQ(category.name(), std::string("radio_error"));

    // Test various error messages
    EXPECT_EQ(category.message(static_cast<int>(RadioErrorCode::kTimeout)),
              "Operation timed out");
    EXPECT_EQ(
        category.message(static_cast<int>(RadioErrorCode::kInvalidParameter)),
        "Invalid parameter provided");
    EXPECT_EQ(
        category.message(static_cast<int>(RadioErrorCode::kBufferOverflow)),
        "Buffer overflow detected");
}

TEST_F(RadioErrorTest, ErrorCodeConversionTest) {
    Result result = Error(RadioErrorCode::kHardwareError);
    std::error_code error_code = result.AsErrorCode();

    EXPECT_EQ(error_code.value(),
              static_cast<int>(RadioErrorCode::kHardwareError));
    EXPECT_EQ(error_code.category().name(), std::string("radio_error"));
    EXPECT_EQ(error_code.message(), "Hardware-level error occurred");
}

}  // namespace test
}  // namespace radio
}  // namespace loramesher