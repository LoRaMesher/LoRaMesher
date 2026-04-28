/**
 * @file test_routing_role_change.cpp
 * @brief Entry point for runtime NodeRole change tests.
 *
 * Run with:
 *   pio test -e test_native --filter "protocols/lora_mesh/services/test_routing_role_change"
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
    loramesher::os::RTOS::Init();
    if (RUN_ALL_TESTS()) {}
    return 0;
}
#endif
