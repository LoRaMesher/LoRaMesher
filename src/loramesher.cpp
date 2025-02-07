#include "loramesher.hpp"

using namespace loramesher::hal;

#ifdef LORAMESHER_BUILD_ARDUINO
LoraMesher::LoraMesher(const Config& config) {
    m_hal = std::make_unique<LoraMesherArduinoHal>();
    init();
}

#elif defined(LORAMESHER_BUILD_NATIVE)
LoraMesher::LoraMesher(const Config& config) {
    m_hal = std::make_unique<NativeHal>();
    init();
}
#endif

void LoraMesher::init() {
    // ESP_LOGI(LM_TAG, "Initializing at time: %lu", m_hal->millis());
}

LoraMesher::~LoraMesher() {
    ESP_LOGI(LM_TAG, "Destroying LoraMesher instance");
}
