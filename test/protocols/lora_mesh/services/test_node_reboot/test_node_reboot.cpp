/**
 * @file test_node_reboot.cpp
 * @brief Entry point for node reboot (warm restart) integration tests.
 *
 * Run with:
 *   pio test -e test_native --filter "protocols/lora_mesh/services/test_node_reboot"
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
