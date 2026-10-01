// Native asset streaming for applications: one NativeResourceLoader mounted on
// a project-relative package directory. File reads and decode/upload happen
// only inside elisa_application_v1_asset_stream_pump on the owner thread, so
// callers observe the real queued/decoding/uploading/resident/failed/cancelled
// stages per request. Handles pack generation * 32 + slot and are always > 0.
#include "bundle_dependencies.h"
#include "native_resource_loader.h"

#include <memory>

namespace {

struct AssetStreamState {
    std::mutex mutex;
    std::unique_ptr<probe::NativeResourceLoader> loader;
    uint64_t mount_generation = 0;
};

AssetStreamState& asset_stream_state() {
    static AssetStreamState state;
    return state;
}

constexpr int64_t ASSET_STREAM_SLOT_SPAN = 32;

probe::NativeAssetHandle unpack_asset_stream_handle(int64_t packed) {
    if (packed <= 0) return {};
    const int64_t generation = packed / ASSET_STREAM_SLOT_SPAN;
    const int64_t slot = packed % ASSET_STREAM_SLOT_SPAN;
    if (generation <= 0 || generation > static_cast<int64_t>(UINT32_MAX) ||
        slot >= static_cast<int64_t>(probe::NativeResourceLoader::MAX_ASSETS)) return {};
    return {static_cast<uint32_t>(slot), static_cast<uint32_t>(generation)};
}

bool asset_stream_relative_path(const char* text, size_t limit) {
    if (text == nullptr) return false;
    size_t length = 0;
    while (length <= limit && text[length] != '\0') ++length;
    if (length == 0 || length > limit) return false;
    const std::string value(text, length);
    if (value.front() == '/' || value.find('\\') != std::string::npos) return false;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t end = value.find('/', start);
        const std::string part = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

// Checks the application is initialized on its owner thread. Lock order is
// always the application service mutex, then the asset stream mutex.
int32_t asset_stream_ready() {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    return ELISA_APPLICATION_OK;
}

} // namespace

extern "C" int32_t elisa_application_v1_asset_stream_mount(const char* directory) {
    const int32_t ready = asset_stream_ready();
    if (ready != ELISA_APPLICATION_OK) return ready;
    if (!asset_stream_relative_path(directory, 1024)) return ELISA_APPLICATION_INVALID_ARGUMENT;
    std::filesystem::path root;
    if (!elisa::assets::project_asset_root(root)) return ELISA_APPLICATION_PROFILE_UNAVAILABLE;
    std::error_code error;
    const std::filesystem::path base = root / std::filesystem::u8path(directory);
    if (!std::filesystem::is_directory(base, error) || error) return ELISA_APPLICATION_INVALID_ARGUMENT;
    AssetStreamState& state = asset_stream_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    try {
        if (!state.loader) {
            wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
            if (device == nullptr) return ELISA_APPLICATION_PROFILE_UNAVAILABLE;
            state.loader = std::make_unique<probe::NativeResourceLoader>(device);
        }
        if (state.mount_generation == UINT64_MAX) return ELISA_APPLICATION_INVALID_STATE;
        ++state.mount_generation;
        return state.loader->mount(base, {}, state.mount_generation) ?
            ELISA_APPLICATION_OK : ELISA_APPLICATION_INVALID_STATE;
    } catch (...) {
        return ELISA_APPLICATION_INITIALIZATION_FAILED;
    }
}

extern "C" int64_t elisa_application_v1_asset_stream_request(const char* package, const char* section) {
    const int32_t ready = asset_stream_ready();
    if (ready != ELISA_APPLICATION_OK) return ready;
    if (!asset_stream_relative_path(package, 1024) || !asset_stream_relative_path(section, 64)) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    AssetStreamState& state = asset_stream_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.loader) return ELISA_APPLICATION_INVALID_STATE;
    try {
        const probe::NativeAssetHandle handle = state.loader->request(package, section);
        if (handle.slot == probe::NativeAssetHandle::INVALID_SLOT) return ELISA_APPLICATION_QUEUE_FULL;
        return static_cast<int64_t>(handle.generation) * ASSET_STREAM_SLOT_SPAN + handle.slot;
    } catch (...) {
        return ELISA_APPLICATION_FRAME_FAILED;
    }
}

// Reads up to `io_budget` files and decodes/uploads up to `upload_budget`
// requests. Returns the number uploaded, or an error.
extern "C" int32_t elisa_application_v1_asset_stream_pump(int32_t io_budget, int32_t upload_budget) {
    const int32_t ready = asset_stream_ready();
    if (ready != ELISA_APPLICATION_OK) return ready;
    if (io_budget < 1 || io_budget > 16 || upload_budget < 1 || upload_budget > 16) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    AssetStreamState& state = asset_stream_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.loader) return ELISA_APPLICATION_INVALID_STATE;
    try {
        return static_cast<int32_t>(state.loader->pump(static_cast<uint32_t>(io_budget),
            static_cast<uint32_t>(upload_budget)));
    } catch (...) {
        return ELISA_APPLICATION_FRAME_FAILED;
    }
}

// Stage codes: 0 empty/unknown, 1 queued, 2 decoding, 3 uploading,
// 4 resident, 5 failed, 6 cancelled. `bytes` is the decoded size once past
// decode; `texel` is the uploaded 0xAABBGGRR pixel once resident.
extern "C" int32_t elisa_application_v1_asset_stream_query(int64_t handle,
    int32_t* stage, int64_t* bytes, int64_t* texel) {
    if (stage == nullptr || bytes == nullptr || texel == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    const int32_t ready = asset_stream_ready();
    if (ready != ELISA_APPLICATION_OK) return ready;
    AssetStreamState& state = asset_stream_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.loader) return ELISA_APPLICATION_INVALID_STATE;
    const probe::NativeAssetHandle native = unpack_asset_stream_handle(handle);
    *stage = static_cast<int32_t>(state.loader->state(native));
    *bytes = static_cast<int64_t>(state.loader->byte_count(native));
    *texel = static_cast<int64_t>(state.loader->texel(native));
    return ELISA_APPLICATION_OK;
}

// Releases a resident request (its GPU texture and bytes) or cancels a
// queued one. Returns INVALID_STATE for any other stage.
extern "C" int32_t elisa_application_v1_asset_stream_drop(int64_t handle) {
    const int32_t ready = asset_stream_ready();
    if (ready != ELISA_APPLICATION_OK) return ready;
    AssetStreamState& state = asset_stream_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.loader) return ELISA_APPLICATION_INVALID_STATE;
    const probe::NativeAssetHandle native = unpack_asset_stream_handle(handle);
    const probe::NativeAssetState current = state.loader->state(native);
    const bool dropped = current == probe::NativeAssetState::Resident ? state.loader->release(native) :
        current == probe::NativeAssetState::Queued && state.loader->cancel(native);
    return dropped ? ELISA_APPLICATION_OK : ELISA_APPLICATION_INVALID_STATE;
}

// Number of requests still holding a slot (queued through resident).
extern "C" int32_t elisa_application_v1_asset_stream_live(void) {
    const int32_t ready = asset_stream_ready();
    if (ready != ELISA_APPLICATION_OK) return ready;
    AssetStreamState& state = asset_stream_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.loader ? static_cast<int32_t>(state.loader->live_count()) : 0;
}

// Destroys the loader and its GPU textures. Application shutdown calls this
// before the graphics device goes away.
extern "C" int32_t elisa_application_v1_asset_stream_reset(void) {
    AssetStreamState& state = asset_stream_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.loader.reset();
    return ELISA_APPLICATION_OK;
}
