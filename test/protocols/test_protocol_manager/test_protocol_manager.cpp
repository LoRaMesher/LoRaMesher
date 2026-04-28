/**
 * @file test_protocol_manager.cpp
 * @brief Test suite entry point for protocol manager tests
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
    loramesher::os::RTOS::Init();
    if (RUN_ALL_TESTS()) {}
    return 0;
}
#endif
