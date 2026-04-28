/**
 * @file test/protocols/test_unit.cpp
 * @brief Test suite for lora_mesh unit tests
 */
#include <gtest/gtest.h>
#include "os/rtos.hpp"

#if defined(ARDUINO)
#include <Arduino.h>

void setup() {
    // should be the same value as for the `test_speed` option in "platformio.ini"
    // default value is test_speed=115200
    Serial.begin(115200);

    ::testing::InitGoogleTest();
    if (RUN_ALL_TESTS()) {}

    return;
}

void loop() {
    // Run tests

    // sleep 1 sec
}

#else
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    loramesher::os::RTOS::Init();
    if (RUN_ALL_TESTS()) {}
    // Always return zero-code and allow PlatformIO to parse results
    return 0;
}
#endif
