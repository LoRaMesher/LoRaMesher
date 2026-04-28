/**
 * @file test_routing_unit.cpp
 * @brief Main entry point for isolated routing table unit tests
 *
 * This test suite tests the DistanceVectorRoutingTable in isolation,
 * without the full protocol stack, to help identify routing algorithm
 * bugs separately from integration/timing issues.
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
