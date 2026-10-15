/**
 * @file test_network_stress.cpp
 * @brief Main entry point for the network mixed-traffic stress suite.
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
