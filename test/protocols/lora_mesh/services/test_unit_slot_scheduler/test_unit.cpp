/**
 * @file test_unit.cpp
 * @brief GoogleTest entry point for the SlotScheduler unit tests.
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

void loop() {
    // Tests run once in setup().
}

#else
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {}
    return 0;
}
#endif
