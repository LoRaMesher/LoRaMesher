/**
 * @file protocol_lifecycle_helpers.hpp
 * @brief Checks shared by tests that start, stop and restart protocols
 */
#pragma once

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#include "os/os_port.hpp"
#include "protocols/lora_mesh_protocol.hpp"

namespace loramesher {
namespace test {

/**
 * @brief Whether the protocol task processes a queued notification
 *
 * Queues a role change, which only the protocol task applies, and waits up
 * to @p timeout_ms of real time for it to take effect.
 *
 * @param protocol Started protocol (real-time RTOS mode)
 * @param timeout_ms Real-time bound on the wait
 * @return true if the role change was applied
 */
inline bool ProtocolTaskResponds(protocols::LoRaMeshProtocol& protocol,
                                 uint32_t timeout_ms = 2000) {
    NodeRole target = protocol.GetNodeRole() == NodeRole::NODE_ONLY
                          ? NodeRole::AUTO
                          : NodeRole::NODE_ONLY;
    if (!protocol.RequestNodeRoleChange(target)) {
        return false;
    }
    constexpr uint32_t kPollMs = 10;
    for (uint32_t waited = 0; waited < timeout_ms; waited += kPollMs) {
        if (protocol.GetNodeRole() == target) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return protocol.GetNodeRole() == target;
}

/**
 * @brief Whether the protocol's superframe service is running
 */
inline bool SuperframeRunning(protocols::LoRaMeshProtocol& protocol) {
    auto* superframe = protocol.GetSuperframeServiceForTest();
    return superframe != nullptr && superframe->IsRunning();
}

/**
 * @brief Reset the RTOS mock's task misuse counters
 */
inline void ResetTaskMisuseCounters() {
    if (auto* mock = dynamic_cast<os::RTOSMock*>(&GetRTOS())) {
        mock->resetTaskMisuseCounters();
    }
}

/**
 * @brief Expect that no task was deleted while running and that no task
 * call used an invalid or dangling handle since the last reset
 */
inline void ExpectNoTaskMisuse() {
    if (auto* mock = dynamic_cast<os::RTOSMock*>(&GetRTOS())) {
        EXPECT_EQ(mock->getExternalDeleteCount(), 0u)
            << "A running task was deleted from another thread";
        EXPECT_EQ(mock->getInvalidHandleCallCount(), 0u)
            << "A task call used an invalid or dangling handle";
    }
}

}  // namespace test
}  // namespace loramesher
