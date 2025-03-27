#pragma once

#include "config/system_config.hpp"

#ifdef DEBUG

#include "../test/mocks/mock_radio.hpp"
#include "hardware/radiolib/radiolib_radio.hpp"
#include "mocks/mock_radio.hpp"

namespace loramesher {
namespace radio {

class RadioLibRadio;

// Forward declare test::MockRadio
namespace test {
class MockRadio;
}

/**
 * @brief Get the mock radio from RadioLibRadio for testing purposes
 * 
 * This function allows tests to access the mock radio inside RadioLibRadio
 * to set expectations.
 * 
 * @param radio The RadioLibRadio instance
 * @return test::MockRadio& Reference to the mock radio for setting expectations
 * @throws std::runtime_error if the current module is not a MockRadio
 */
test::MockRadio& GetRadioLibMockForTesting(RadioLibRadio& radio) {
#if defined(PLATFORMIO)
    // PlatformIO specific code
    // Maybe use the static type checking approach when in PlatformIO
    auto* mock_radio = static_cast<MockRadio*>(radio.current_module_.get());
#else
    // Non-PlatformIO builds might have RTTI enabled
    auto* mock_radio = dynamic_cast<MockRadio*>(radio.current_module_.get());
#endif
    if (!mock_radio) {
        throw std::runtime_error("Current module is not a MockRadio");
    }
    return GetMockForTesting(*mock_radio);
}

}  // namespace radio
}  // namespace loramesher

#endif  // DEBUG