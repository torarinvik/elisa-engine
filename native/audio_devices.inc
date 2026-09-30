// Playback-device enumeration (included from audio_service_abi.cpp). Each call
// opens a short-lived miniaudio context so enumeration works whether or not
// the mixer is running and never disturbs the open device.

namespace {

// Calls `visit(info, index)` for every playback device; false when the
// platform backend cannot enumerate.
template <typename Visit>
bool visit_playback_devices(Visit visit) {
    ma_context context{};
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) return false;
    ma_device_info* playback = nullptr;
    ma_uint32 count = 0;
    const bool ok = ma_context_get_devices(&context, &playback, &count, nullptr, nullptr) == MA_SUCCESS;
    if (ok) {
        for (ma_uint32 i = 0; i < count; i++) visit(playback[i], i, count);
    }
    ma_context_uninit(&context);
    return ok;
}

}  // namespace

extern "C" int32_t elisa_audio_v1_playback_device_count(uint32_t* count) {
    if (count == nullptr) return ELISA_AUDIO_INVALID_ARGUMENT;
    *count = 0;
    uint32_t found = 0;
    if (!visit_playback_devices([&](const ma_device_info&, ma_uint32, ma_uint32 total) { found = total; })) {
        return ELISA_AUDIO_DEVICE_UNAVAILABLE;
    }
    *count = found;
    return ELISA_AUDIO_OK;
}

// Writes the device's UTF-8 name (without NUL) and whether it is the system
// default. CAPACITY means `capacity` was too small; `length` then holds the
// needed size. An index past the current device list is INVALID_ARGUMENT.
extern "C" int32_t elisa_audio_v1_playback_device(uint32_t index, uint8_t* name, uint32_t capacity,
    uint32_t* length, int32_t* is_default) {
    if (length == nullptr || is_default == nullptr || (name == nullptr && capacity != 0)) {
        return ELISA_AUDIO_INVALID_ARGUMENT;
    }
    *length = 0;
    *is_default = 0;
    int32_t status = ELISA_AUDIO_INVALID_ARGUMENT;
    const bool ok = visit_playback_devices([&](const ma_device_info& info, ma_uint32 i, ma_uint32) {
        if (i != index) return;
        const size_t size = std::strlen(info.name);
        *length = static_cast<uint32_t>(size);
        *is_default = info.isDefault ? 1 : 0;
        if (size > capacity) { status = ELISA_AUDIO_CAPACITY; return; }
        std::memcpy(name, info.name, size);
        status = ELISA_AUDIO_OK;
    });
    return ok ? status : ELISA_AUDIO_DEVICE_UNAVAILABLE;
}

// Opens the mixer on the playback device whose name matches exactly. Settings
// persist the name, which survives reordering; DEVICE_UNAVAILABLE means the
// device is gone and the caller should fall back to the default.
extern "C" int32_t elisa_audio_v1_initialize_named(uint32_t sample_rate, uint32_t channels,
    const uint8_t* name, uint32_t length) {
    if (name == nullptr || length == 0) return ELISA_AUDIO_INVALID_ARGUMENT;
    ma_device_id chosen{};
    bool found = false;
    const bool ok = visit_playback_devices([&](const ma_device_info& info, ma_uint32, ma_uint32) {
        if (!found && std::strlen(info.name) == length && std::memcmp(info.name, name, length) == 0) {
            chosen = info.id;
            found = true;
        }
    });
    if (!ok || !found) return ELISA_AUDIO_DEVICE_UNAVAILABLE;
    return initialize_audio(sample_rate, channels, false, &chosen);
}
