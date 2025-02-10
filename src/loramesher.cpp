#include "loramesher.hpp"

namespace loramesher {

#ifdef LORAMESHER_BUILD_ARDUINO
LoraMesher::LoraMesher(const Config& config) : config_(config) {
    hal_ = std::make_unique<hal::LoraMesherArduinoHal>();
    if (!initialize()) {
        throw std::runtime_error("Failed to initialize LoraMesher");
    }
}

#elif defined(LORAMESHER_BUILD_NATIVE)
LoraMesher::LoraMesher(const Config& config) {
    hal_ = std::make_unique<hal::NativeHal>();
    if (!initialize()) {
        throw std::runtime_error("Failed to initialize LoraMesher");
    }
}
#endif

LoraMesher::~LoraMesher() {
    stop();
}

bool LoraMesher::initialize() {
    try {
        // Create HAL
        if (!hal_) {
            return false;
        }

        // Initialize managers
        // radioManager_ = std::make_unique<RadioManager>(config_.getRadioConfig(),
        //                                                hal_.get());
        // meshProtocol_ =
        //     std::make_unique<MeshProtocol>(config_.getProtocolConfig());
        // sleepManager_ = std::make_unique<SleepManager>(
        //     config_.getSleepDuration(), config_.isDeepSleepEnabled());
        // taskManager_ = std::make_unique<TaskManager>();

        isInitialized_ = true;
        return true;
    } catch (const std::exception& e) {
        // Log error
        return false;
    }
}

bool LoraMesher::start() {
    if (!isInitialized_) {
        return false;
    }

    try {
        // Start all components in correct order
        // if (!radioManager_->start())
        //     return false;
        // if (!meshProtocol_->start())
        //     return false;
        // if (!taskManager_->start())
        //     return false;

        isRunning_ = true;
        return true;
    } catch (const std::exception& e) {
        stop();
        return false;
    }
}

void LoraMesher::stop() {
    if (!isRunning_)
        return;

    // Stop components in reverse order
    // taskManager_->stop();
    // meshProtocol_->stop();
    // radioManager_->stop();

    isRunning_ = false;
}

}  // namespace loramesher
