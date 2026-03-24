/**
 * @file radio_lib_code_errors_test.cpp
 * @brief Tests for RadioLibCodeErrors::ConvertStatus error code mapping
 */
#include <gtest/gtest.h>

#include "hardware/radiolib/radiolib_modules/radio_lib_code_errors.hpp"

namespace loramesher {
namespace radio {
namespace test {

class RadioLibCodeErrorsTest : public ::testing::Test {};

TEST_F(RadioLibCodeErrorsTest, NoneReturnsSuccess) {
    Result result = RadioLibCodeErrors::ConvertStatus(RADIOLIB_ERR_NONE);
    EXPECT_TRUE(result);
}

TEST_F(RadioLibCodeErrorsTest, InvalidFrequencyReturnsFrequencyError) {
    Result result =
        RadioLibCodeErrors::ConvertStatus(RADIOLIB_ERR_INVALID_FREQUENCY);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kFrequencyError);
}

TEST_F(RadioLibCodeErrorsTest, InvalidBandwidthReturnsConfigurationError) {
    Result result =
        RadioLibCodeErrors::ConvertStatus(RADIOLIB_ERR_INVALID_BANDWIDTH);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kConfigurationError);
}

TEST_F(RadioLibCodeErrorsTest,
       InvalidSpreadingFactorReturnsConfigurationError) {
    Result result = RadioLibCodeErrors::ConvertStatus(
        RADIOLIB_ERR_INVALID_SPREADING_FACTOR);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kConfigurationError);
}

TEST_F(RadioLibCodeErrorsTest, InvalidCodingRateReturnsConfigurationError) {
    Result result =
        RadioLibCodeErrors::ConvertStatus(RADIOLIB_ERR_INVALID_CODING_RATE);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kConfigurationError);
}

TEST_F(RadioLibCodeErrorsTest, InvalidBitRangeReturnsInvalidParameter) {
    Result result =
        RadioLibCodeErrors::ConvertStatus(RADIOLIB_ERR_INVALID_BIT_RANGE);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kInvalidParameter);
}

TEST_F(RadioLibCodeErrorsTest, InvalidSyncWordReturnsSyncWordError) {
    Result result =
        RadioLibCodeErrors::ConvertStatus(RADIOLIB_ERR_INVALID_SYNC_WORD);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kSyncWordError);
}

TEST_F(RadioLibCodeErrorsTest, CrcMismatchReturnsCrcError) {
    Result result =
        RadioLibCodeErrors::ConvertStatus(RADIOLIB_ERR_CRC_MISMATCH);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kCrcError);
}

TEST_F(RadioLibCodeErrorsTest, ChipNotFoundReturnsHardwareError) {
    Result result =
        RadioLibCodeErrors::ConvertStatus(RADIOLIB_ERR_CHIP_NOT_FOUND);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kHardwareError);
}

TEST_F(RadioLibCodeErrorsTest, UnknownCodeReturnsHardwareError) {
    Result result = RadioLibCodeErrors::ConvertStatus(-999);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.getErrorCode(), LoraMesherErrorCode::kHardwareError);
}

TEST_F(RadioLibCodeErrorsTest, UnknownCodeContainsStatusInMessage) {
    Result result = RadioLibCodeErrors::ConvertStatus(-42);
    EXPECT_FALSE(result);
    std::string msg = result.GetErrorMessage();
    EXPECT_NE(msg.find("-42"), std::string::npos);
}

}  // namespace test
}  // namespace radio
}  // namespace loramesher
