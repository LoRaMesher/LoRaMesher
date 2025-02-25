#include "loramesher.hpp"

namespace loramesher {

LoraMesher::LoraMesher(const Config& config) : config_(config) {}

LoraMesher::~LoraMesher() {
    Stop();
}

Result LoraMesher::Initialize() {
    // Create HAL
    hardware_manager_ = std::make_unique<hardware::HardwareManager>(
        config_.getPinConfig(), config_.getRadioConfig());

    Result result = hardware_manager_->Initialize();
    if (!result) {
        return result;
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
    return Result::Success();
}

Result LoraMesher::Start() {
    Result result = Initialize();

    if (!result) {
        return result;
    }

    if (!isInitialized_) {
        return Result::Error(LoraMesherErrorCode::kNotInitialized);
    }

    isRunning_ = true;
    return Result::Success();
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

Result LoraMesher::sendMessage(const BaseMessage& msg) {
    // Prevent unused parameter warnings until implemented
    (void)msg;
    return hardware_manager_->SendMessage();
}

}  // namespace loramesher
