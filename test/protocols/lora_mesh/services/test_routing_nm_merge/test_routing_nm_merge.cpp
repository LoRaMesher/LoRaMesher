/**
 * @file test_routing_nm_merge.cpp
 * @brief Entry point for Network Manager merge tests.
 *
 * Separated from test_routing to allow fast routing tests to run
 * independently.  These tests exercise the Path-A lite-merge mechanism
 * and take ~4 minutes of virtual time per run.
 *
 * Run with:
 *   pio test -e test_native --filter "protocols/lora_mesh/services/test_routing_nm_merge"
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
