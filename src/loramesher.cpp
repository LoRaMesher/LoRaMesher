#include "loramesher.hpp"

namespace loramesher {

LoraMesher::~LoraMesher() {
    Stop();
}

bool LoraMesher::Initialize() {
    try {
        // Create HAL
        hardware_manager_ = std::make_unique<hardware::HardwareManager>(
            config_.getPinConfig(), config_.getRadioConfig());
        if (!hardware_manager_->Initialize()) {
            throw std::runtime_error("Failed to Initialize HAL");
        }

        // Initialize managers
        // radioManager_ = std::make_unique<RadioManager>(config_.getRadioConfig(),
        //                                                hal_.get());
        // meshProtocol_ =
        //     std::make_unique<MeshProtocol>(config_.getProtocolConfig());
        // sleepManager_ = std::make_unique<SleepManager>(
        //     config_.getSleepDuration(), config_.getDeepSleepEnabled());
        // taskManager_ = std::make_unique<TaskManager>();

        isInitialized_ = true;
        return true;
    } catch (const std::exception& e) {
        // Log error
        return false;
    }
}

bool LoraMesher::Start() {
    if (!isInitialized_) {
        return false;
    }

    try {
        // Start all components in correct order
        // if (!radioManager_->Start())
        //     return false;
        // if (!meshProtocol_->Start())
        //     return false;
        // if (!taskManager_->Start())
        //     return false;

        isRunning_ = true;
        return true;
    } catch (const std::exception& e) {
        Stop();
        return false;
    }
}

void LoraMesher::Stop() {
    if (!isRunning_)
        return;

    // Stop components in reverse order
    // taskManager_->Stop();
    // meshProtocol_->Stop();
    // radioManager_->Stop();

    isRunning_ = false;
}

}  // namespace loramesher
