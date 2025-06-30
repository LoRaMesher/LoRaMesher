// src/types/radio/radio_state.hpp
#pragma once

namespace loramesher {
namespace radio {

/**
 * @brief Enumeration of possible radio states
 * 
 * Defines the various operational states that a radio module can be in.
 */
enum class RadioState {
    kIdle,      ///< Radio is idle, not transmitting or receiving
    kReceive,   ///< Radio is in receive mode
    kTransmit,  ///< Radio is transmitting
    kCad,       ///< Radio is performing Channel Activity Detection
    kSleep,     ///< Radio is in sleep mode
    kError      ///< Radio is in an error state
};

}  // namespace radio
}  // namespace loramesher