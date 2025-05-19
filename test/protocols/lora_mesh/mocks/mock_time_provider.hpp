/**
 * @file mock_time_provider.cpp
 * @brief Mock implementation of time provider for testing
 * 
 * Location: test/protocols/lora_mesh/mocks/mock_time_provider.cpp
 */

#include <GMock/gmock.h>

#include "protocols/lora_mesh/services/time_provider.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {
namespace test {

/**
     * @brief Mock time provider for testing
     */
class MockTimeProvider : public ITimeProvider {
   public:
    MOCK_METHOD(uint32_t, GetCurrentTime, (), (const, override));
    MOCK_METHOD(void, Sleep, (uint32_t ms), (const, override));
    MOCK_METHOD(uint32_t, GetElapsedTime, (uint32_t reference_time),
                (const, override));
};

}  // namespace test
}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher