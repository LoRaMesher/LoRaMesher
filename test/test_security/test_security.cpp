/**
 * @file test_security.cpp
 * @brief Entry point for the crypto core, frame counter and replay tests.
 *
 * Run with:
 *   pio test -e test_native --filter "test_security"
 */
#include <gtest/gtest.h>

#if defined(ARDUINO)
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    ::testing::InitGoogleTest();
    if (RUN_ALL_TESTS()) {}
}

void loop() {}

#else
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {}
    return 0;
}
#endif
