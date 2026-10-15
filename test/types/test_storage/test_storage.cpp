/**
 * @file test_storage.cpp
 * @brief Entry point for state store and snapshot codec tests.
 *
 * Run with:
 *   pio test -e test_native --filter "types/test_storage"
 */
#include <gtest/gtest.h>

#include "os/rtos.hpp"

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
