/**
 * @file nvs_counter_store.cpp
 * @brief Counter store in ESP32 non-volatile storage (flash)
 */

#include "security/nvs_counter_store.hpp"

#if defined(ESP_PLATFORM)

#include <nvs.h>

#include <utility>

namespace loramesher {
namespace security {

NvsCounterStore::NvsCounterStore(std::string name_space, std::string key)
    : name_space_(std::move(name_space)), key_(std::move(key)) {}

Result NvsCounterStore::Load(uint32_t& mark) {
    // Opening for writing creates a missing namespace, so a failure here is
    // a real error and never an empty store
    nvs_handle_t handle;
    if (nvs_open(name_space_.c_str(), NVS_READWRITE, &handle) != ESP_OK) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Cannot open NVS namespace");
    }
    uint32_t value = 0;
    const esp_err_t status = nvs_get_u32(handle, key_.c_str(), &value);
    nvs_close(handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        mark = 0;
        return Result::Success();
    }
    if (status != ESP_OK) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Cannot read counter mark from NVS");
    }
    mark = value;
    return Result::Success();
}

Result NvsCounterStore::Store(uint32_t mark) {
    nvs_handle_t handle;
    if (nvs_open(name_space_.c_str(), NVS_READWRITE, &handle) != ESP_OK) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Cannot open NVS namespace");
    }
    esp_err_t status = nvs_set_u32(handle, key_.c_str(), mark);
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    if (status != ESP_OK) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Cannot write counter mark to NVS");
    }
    return Result::Success();
}

}  // namespace security
}  // namespace loramesher

#endif  // ESP_PLATFORM
