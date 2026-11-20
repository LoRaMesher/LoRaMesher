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

std::optional<CounterRecord> NvsCounterStore::Load() {
    // Opening for writing creates a missing namespace, so a failure here is
    // a real error and never an empty store
    nvs_handle_t handle;
    if (nvs_open(name_space_.c_str(), NVS_READWRITE, &handle) != ESP_OK) {
        last_load_status_ = LoadStatus::kReadFailed;
        return std::nullopt;
    }
    uint8_t blob[kCounterRecordSize];
    size_t size = sizeof(blob);
    const esp_err_t status = nvs_get_blob(handle, key_.c_str(), blob, &size);
    nvs_close(handle);

    if (status == ESP_ERR_NVS_NOT_FOUND) {
        last_load_status_ = LoadStatus::kAbsent;
        return std::nullopt;
    }
    if (status == ESP_ERR_NVS_INVALID_LENGTH) {
        last_load_status_ = LoadStatus::kCorrupt;
        return std::nullopt;
    }
    if (status != ESP_OK) {
        last_load_status_ = LoadStatus::kReadFailed;
        return std::nullopt;
    }
    std::optional<CounterRecord> record =
        DecodeCounterRecord(std::span<const uint8_t>(blob, size));
    last_load_status_ = record ? LoadStatus::kOk : LoadStatus::kCorrupt;
    return record;
}

Result NvsCounterStore::Save(const CounterRecord& record) {
    if (!IsValidCounterRecord(record)) {
        return Result(LoraMesherErrorCode::kInvalidParameter,
                      "Counter record is not valid");
    }
    nvs_handle_t handle;
    if (nvs_open(name_space_.c_str(), NVS_READWRITE, &handle) != ESP_OK) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Cannot open NVS namespace");
    }
    const auto blob = EncodeCounterRecord(record);
    esp_err_t status =
        nvs_set_blob(handle, key_.c_str(), blob.data(), blob.size());
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    if (status != ESP_OK) {
        return Result(LoraMesherErrorCode::kHardwareError,
                      "Cannot write counter record to NVS");
    }
    return Result::Success();
}

}  // namespace security
}  // namespace loramesher

#endif  // ESP_PLATFORM
