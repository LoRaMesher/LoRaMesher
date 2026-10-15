/**
 * @file test_sync_beacon_subslot.cpp
 * @brief Main entry point for SYNC_BEACON subslot-assignment experiment tests
 */
#include <gtest/gtest.h>
#include "os/rtos.hpp"

#if defined(ARDUINO)
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    ::testing::InitGoogleTest();
    if (RUN_ALL_TESTS()) {}
    return;
}

void loop() {}

#else
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {}
    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}
#endif
